/*
 * Copyright (c) Facebook, Inc. and its affiliates.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
#include <boost/algorithm/string.hpp>
#include <boost/algorithm/string/case_conv.hpp>
#include <algorithm>
#include <optional>
#include <unordered_map>

#include "velox/common/base/Exceptions.h"
#include "velox/expression/SignatureBinder.h"
#include "velox/expression/TypeSignature.h"
#include "velox/expression/type_calculation/TypeCalculation.h"
#include "velox/type/Type.h"
#include "velox/type/TypeCoercer.h"
#include "velox/type/TypeUtil.h"

namespace facebook::velox::exec {
namespace {

bool isAny(const TypeSignature& typeSignature) {
  return typeSignature.baseName() == "any";
}

/// Returns true only if 'str' contains digits.
bool isPositiveInteger(const std::string& str) {
  return !str.empty() &&
      std::find_if(str.begin(), str.end(), [](unsigned char c) {
        return !std::isdigit(c);
      }) == str.end();
}

std::optional<int> tryResolveLongLiteral(
    const TypeSignature& parameter,
    const std::unordered_map<std::string, SignatureVariable>& variables,
    std::unordered_map<std::string, int>& integerVariablesBindings) {
  const auto& variable = parameter.baseName();

  if (isPositiveInteger(variable)) {
    // Handle constant.
    return atoi(variable.c_str());
  };

  if (integerVariablesBindings.count(variable)) {
    return integerVariablesBindings.at(variable);
  }

  auto it = variables.find(variable);
  if (it == variables.end()) {
    return std::nullopt;
  }

  const auto& constraints = it->second.constraint();

  if (constraints.empty()) {
    return std::nullopt;
  }

  // Try to assign value based on constraints.
  // Check constraints and evaluate.
  const auto calculation = fmt::format("{}={}", variable, constraints);
  expression::calculation::evaluate(calculation, integerVariablesBindings);
  VELOX_CHECK(
      integerVariablesBindings.count(variable),
      "Variable {} calculation failed.",
      variable);
  return integerVariablesBindings.at(variable);
}

std::optional<LongEnumParameter> tryResolveLongEnumLiteral(
    const TypeSignature& parameter,
    const std::unordered_map<std::string, LongEnumParameter>&
        longEnumParameterVariableBindings) {
  auto it = longEnumParameterVariableBindings.find(parameter.baseName());
  if (it != longEnumParameterVariableBindings.end()) {
    return it->second;
  }
  return std::nullopt;
}

std::optional<VarcharEnumParameter> tryResolveVarcharEnumLiteral(
    const TypeSignature& parameter,
    const std::unordered_map<std::string, VarcharEnumParameter>&
        varcharEnumParameterVariableBindings) {
  auto it = varcharEnumParameterVariableBindings.find(parameter.baseName());
  if (it != varcharEnumParameterVariableBindings.end()) {
    return it->second;
  }
  return std::nullopt;
}

// If the parameter is a named field from a row, ensure the names are
// compatible. For example:
//
// > row(bigint) - binds any row with bigint as field.
// > row(foo bigint) - only binds rows where bigint field is named foo.
bool checkNamedRowField(
    const TypeSignature& signature,
    const TypePtr& actualType,
    size_t idx) {
  if (signature.rowFieldName().has_value() &&
      (*signature.rowFieldName() != asRowType(actualType)->nameOf(idx))) {
    return false;
  }
  return true;
}

bool checkSignatureProperties(
    const SignatureVariable& signature,
    const TypePtr& actualType) {
  if (!signature.isTypeParameter()) {
    return false;
  }
  if (signature.knownTypesOnly() && actualType->isUnKnown()) {
    return false;
  }
  if (signature.orderableTypesOnly() && !actualType->isOrderable()) {
    return false;
  }
  if (signature.comparableTypesOnly() && !actualType->isComparable()) {
    return false;
  }
  return true;
}

} // namespace

bool SignatureBinder::tryBindWithCoercions(std::vector<Coercion>& coercions) {
  return tryBind(&coercions);
}

bool SignatureBinder::tryBind() {
  return tryBind(nullptr);
}

// Traverse all types in signatures and deduces the least common type for each
// type parameter. Recursion is needed to traverse complex types like Map<K, V>,
// Array<Array<T>>, etc
bool SignatureBinderBase::resolveTypeVars(
    const TypeSignature& signature,
    const TypePtr& actualType,
    bool allowCoercions) {
  const auto& varName = signature.baseName();
  if (auto varIt = variables().find(varName); varIt != variables().end()) {
    const auto& variableSignature = varIt->second;
    auto& varType = typeVariablesBindings_[varName];
    if (!varType) {
      if (!checkSignatureProperties(variableSignature, actualType)) {
        return false;
      }
      varType = actualType;
      return true;
    }
    if (varType->equivalent(*actualType)) {
      return true;
    }
    if (!allowCoercions) {
      return actualType->equivalent(*UNKNOWN());
    }
    if (TypeCoercer::coercible(actualType, varType)) {
      return true;
    }
    if (auto c = TypeCoercer::coercible(varType, actualType)) {
      if (!checkSignatureProperties(variableSignature, actualType)) {
        return false;
      }
      varType = actualType;
      return true;
    }
    return false;
  }
  const auto& signatureParams = signature.parameters();
  const auto& actualParams = actualType->parameters();

  // Simple type in signature, no further resolving var types
  if (signatureParams.empty()) {
    return true;
  }

  for (size_t j = 0; j < actualParams.size(); ++j) {
    const auto& actualParam = actualParams[j];
    const auto& signatureParam = j < signatureParams.size()
        ? signatureParams[j]
        : signatureParams.back();
    if (actualParam.kind == TypeParameterKind::kType) {
      if (!resolveTypeVars(signatureParam, actualParam.type, allowCoercions)) {
        return false;
      }
    }
  }

  return true;
}

bool SignatureBinder::tryBind(std::vector<Coercion>* coercions) {
  if (coercions) {
    coercions->clear();
    coercions->resize(actualTypes_.size());
  }

  const auto& formalArgs = signature_.argumentTypes();
  const auto formalArgsCnt = formalArgs.size();

  if (signature_.variableArity()) {
    if (actualTypes_.size() + 1 < formalArgsCnt) {
      return false;
    }
    if (!coercions && !isAny(signature_.argumentTypes().back()) &&
        actualTypes_.size() > formalArgsCnt) {
      auto& type = actualTypes_[formalArgsCnt - 1];
      for (size_t i = formalArgsCnt; i < actualTypes_.size(); i++) {
        if (!type->equivalent(*actualTypes_[i]) &&
            actualTypes_[i]->kind() != TypeKind::UNKNOWN) {
          return false;
        }
      }
    }
  } else {
    if (formalArgsCnt != actualTypes_.size()) {
      return false;
    }
  }

  // Phase 1: Calculate certain types for each type var
  for (size_t i = 0; i < actualTypes_.size(); ++i) {
    const auto& actualType = actualTypes_[i];
    if (!actualType) {
      return false;
    }
    const auto& formalArgSignature =
        i < formalArgsCnt ? formalArgs[i] : formalArgs.back();
    if (!resolveTypeVars(formalArgSignature, actualType, coercions)) {
      return false;
    }
  }

  const auto bound = coercions ? actualTypes_.size()
                               : std::min(actualTypes_.size(), formalArgsCnt);

  for (size_t i = 0; i < bound; ++i) {
    const auto& formalArgSignature =
        i < formalArgsCnt ? formalArgs[i] : formalArgs.back();
    Coercion* coercion = coercions ? &(*coercions)[i] : nullptr;
    if (!SignatureBinderBase::tryBind(
            formalArgSignature, actualTypes_[i], coercion)) {
      return false;
    }
  }

  return true;
}

bool SignatureBinderBase::checkOrSetLongEnumParameter(
    const std::string& parameterName,
    const LongEnumParameter& params) {
  auto it = longEnumVariablesBindings_.find(parameterName);
  if (it != longEnumVariablesBindings_.end()) {
    if (longEnumVariablesBindings_[parameterName] != params) {
      return false;
    }
  }
  longEnumVariablesBindings_[parameterName] = params;
  return true;
}

bool SignatureBinderBase::checkOrSetVarcharEnumParameter(
    const std::string& parameterName,
    const VarcharEnumParameter& params) {
  auto it = varcharEnumVariablesBindings_.find(parameterName);
  if (it != varcharEnumVariablesBindings_.end()) {
    if (varcharEnumVariablesBindings_[parameterName] != params) {
      return false;
    }
  }
  varcharEnumVariablesBindings_[parameterName] = params;
  return true;
}

bool SignatureBinderBase::checkOrSetIntegerParameter(
    const std::string& parameterName,
    int value) {
  if (isPositiveInteger(parameterName)) {
    return atoi(parameterName.c_str()) == value;
  }
  if (!variables().count(parameterName)) {
    // Return false if the parameter is not found in the signature.
    return false;
  }

  const auto& constraint = variables().at(parameterName).constraint();
  if (isPositiveInteger(constraint) && atoi(constraint.c_str()) != value) {
    // Return false if the actual value does not match the constraint.
    return false;
  }

  if (integerVariablesBindings_.count(parameterName)) {
    // Return false if the parameter is found with a different value.
    if (integerVariablesBindings_[parameterName] != value) {
      return false;
    }
  }
  // Bind the variable.
  integerVariablesBindings_[parameterName] = value;
  return true;
}

bool SignatureBinderBase::tryBind(
    const exec::TypeSignature& typeSignature,
    const TypePtr& actualType,
    Coercion* coercion) {
  if (isAny(typeSignature)) {
    return true;
  }

  const auto& baseName = typeSignature.baseName();

  if (variables().contains(baseName)) {
    // Variables cannot have further parameters.
    VELOX_CHECK(
        typeSignature.parameters().empty(),
        "Variables with parameters are not supported");
    const auto& variable = variables().at(baseName);
    VELOX_CHECK(variable.isTypeParameter(), "Not expecting integer variable");

    const auto& varType = typeVariablesBindings_[variable.name()];
    VELOX_CHECK(varType, "Not expecting unbinded type variable");

    if (coercion) {
      if (!varType->equivalent(*actualType)) {
        *coercion = TypeCoercer::coercible(actualType, varType);
        return coercion->type != nullptr;
      }
      return true;
    }
    return varType->equivalent(*actualType);
  }

  // Type is not a variable.
  auto typeName = boost::algorithm::to_upper_copy(baseName);
  std::string actualTypeName = actualType->name();
  boost::algorithm::to_upper(actualTypeName);

  if (typeName != actualTypeName) {
    if (coercion) {
      // TODO: It's better to postpone this in case of Unknown type because of
      // processing params for complex types
      if (auto availableCoercion =
              TypeCoercer::coerceTypeBase(actualType, typeName)) {
        *coercion = availableCoercion;
        return true;
      }
    }
    return false;
  }

  const auto& params = typeSignature.parameters();

  // Handle homogeneous row case: row(T, ...)
  if (typeSignature.isHomogeneousRow()) {
    VELOX_CHECK_EQ(
        params.size(), 1, "Homogeneous row must have exactly one parameter");

    if (actualType->kind() != TypeKind::ROW) {
      return false;
    }

    if (actualType->size() == 0) {
      // Empty row is always compatible with homogeneous row.
      return true;
    }

    // All children must unify to the same type variable T
    const auto& typeParam = params[0];
    const auto& paramBaseName = typeParam.baseName();

    // First, check and extract the common child type if homogeneous.
    const auto actualChildType =
        velox::type::tryGetHomogeneousRowChild(actualType);
    if (!actualChildType) {
      return false;
    }

    if (variables().count(paramBaseName)) {
      auto it = typeVariablesBindings_.find(paramBaseName);
      if (it != typeVariablesBindings_.end()) {
        return it->second->equivalent(*actualChildType);
      } else {
        typeVariablesBindings_[paramBaseName] = actualChildType;
        return true;
      }
    } else {
      return tryBind(typeParam, actualChildType, nullptr);
    }
  }

  // Type Parameters can recurse.
  if (params.size() != actualType->parameters().size()) {
    return false;
  }

  bool needsCoercion = false;
  int32_t totalCost = 0;
  std::vector<TypePtr> newParameters;
  newParameters.reserve(params.size());

  for (auto i = 0; i < params.size(); i++) {
    const auto& actualParameter = actualType->parameters()[i];
    switch (actualParameter.kind) {
      case TypeParameterKind::kLongLiteral:
        if (!checkOrSetIntegerParameter(
                params[i].baseName(), actualParameter.longLiteral.value())) {
          return false;
        }
        break;
      case TypeParameterKind::kLongEnumLiteral:
        if (!checkOrSetLongEnumParameter(
                params[i].baseName(),
                actualParameter.longEnumLiteral.value())) {
          return false;
        }
        break;
      case TypeParameterKind::kVarcharEnumLiteral:
        if (!checkOrSetVarcharEnumParameter(
                params[i].baseName(),
                actualParameter.varcharEnumLiteral.value())) {
          return false;
        }
        break;
      case TypeParameterKind::kType: {
        if (!checkNamedRowField(params[i], actualType, i)) {
          return false;
        }
        Coercion childCoercion;
        if (!tryBind(
                params[i],
                actualParameter.type,
                coercion ? &childCoercion : nullptr)) {
          return false;
        }
        if (coercion && childCoercion.type) {
          needsCoercion = true;
          totalCost += childCoercion.cost;
          newParameters.emplace_back(childCoercion.type);
        } else {
          newParameters.emplace_back(actualParameter.type);
        }
        break;
      }
    }
  }

  if (coercion && needsCoercion) {
    std::vector<TypeParameter> typeParameters;
    typeParameters.reserve(newParameters.size());
    for (auto i = 0; i < newParameters.size(); ++i) {
      typeParameters.emplace_back(newParameters[i], params[i].rowFieldName());
    }
    coercion->type = getType(typeName, typeParameters);
    coercion->cost = totalCost;
    if (coercion->type->equivalent(*actualType)) {
      coercion->reset();
    }
  }

  return true;
}

TypePtr SignatureBinder::tryResolveType(
    const exec::TypeSignature& typeSignature,
    const std::unordered_map<std::string, SignatureVariable>& variables,
    const std::unordered_map<std::string, TypePtr>& typeVariablesBindings,
    std::unordered_map<std::string, int>& integerVariablesBindings,
    const std::unordered_map<std::string, LongEnumParameter>&
        longEnumParameterVariableBindings,
    const std::unordered_map<std::string, VarcharEnumParameter>&
        varcharEnumParameterVariableBindings) {
  const auto& baseName = typeSignature.baseName();

  if (variables.count(baseName)) {
    auto it = typeVariablesBindings.find(baseName);
    if (it == typeVariablesBindings.end()) {
      return nullptr;
    }
    return it->second;
  }

  // Type is not a variable.
  auto typeName = boost::algorithm::to_upper_copy(baseName);

  const auto& params = typeSignature.parameters();
  std::vector<TypeParameter> typeParameters;

  for (auto& param : params) {
    auto literal =
        tryResolveLongLiteral(param, variables, integerVariablesBindings);
    if (literal.has_value()) {
      typeParameters.emplace_back(literal.value());
      continue;
    }
    auto longEnumParameterliteral =
        tryResolveLongEnumLiteral(param, longEnumParameterVariableBindings);
    if (longEnumParameterliteral.has_value()) {
      typeParameters.emplace_back(longEnumParameterliteral.value());
      continue;
    }
    auto varcharEnumParameterliteral = tryResolveVarcharEnumLiteral(
        param, varcharEnumParameterVariableBindings);
    if (varcharEnumParameterliteral.has_value()) {
      typeParameters.emplace_back(varcharEnumParameterliteral.value());
      continue;
    }

    auto type = tryResolveType(
        param,
        variables,
        typeVariablesBindings,
        integerVariablesBindings,
        longEnumParameterVariableBindings,
        varcharEnumParameterVariableBindings);
    if (!type) {
      return nullptr;
    }
    typeParameters.emplace_back(type, param.rowFieldName());
  }

  try {
    if (auto type = getType(typeName, typeParameters)) {
      return type;
    }
  } catch (const std::exception&) {
    // TODO Perhaps, modify getType to add suppress-errors flag.
    return nullptr;
  }

  auto typeKind = TypeKindName::tryToTypeKind(typeName);
  if (!typeKind.has_value()) {
    return nullptr;
  }

  // getType(parameters) function doesn't support OPAQUE type.
  switch (*typeKind) {
    case TypeKind::OPAQUE:
      return OpaqueType::create<void>();
    default:
      return nullptr;
  }
}
} // namespace facebook::velox::exec
