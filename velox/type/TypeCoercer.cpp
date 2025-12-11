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
#include "velox/type/TypeCoercer.h"
#include "velox/type/Cost.h"

namespace facebook::velox {

Cost Coercion::overallCost(const std::vector<Coercion>& coercions) {
  Cost cost = 0;
  for (const auto& coercion : coercions) {
    if (coercion.type != nullptr) {
      cost += coercion.cost;
    }
  }

  return cost;
}

namespace {

facebook::velox::AllowedCoercions kAllowedCoercions;

} // namespace

// static
Coercion TypeCoercer::coerceTypeBase(
    const TypePtr& fromType,
    const std::string& toTypeName) {
  if (fromType->name() == toTypeName) {
    return Coercion{fromType, 0};
  }

  if (fromType == UNKNOWN()) {
    // Cast Unknown to complex type is not supported yet
    return Coercion{getType(toTypeName, {}), kMinCoercionCost};
  }

  auto it = kAllowedCoercions.find({fromType->name(), toTypeName});
  if (it != kAllowedCoercions.end()) {
    return it->second;
  }

  return {};
}

// static
Coercion TypeCoercer::coercible(
    const TypePtr& fromType,
    const TypePtr& toType) {
  if (fromType == UNKNOWN()) {
    return Coercion{toType, kMinCoercionCost};
  }

  if (fromType->size() == 0) {
    return TypeCoercer::coerceTypeBase(fromType, toType->name());
  }

  if (fromType->name() != toType->name() ||
      fromType->size() != toType->size()) {
    return {};
  }

  Cost cost = 0;
  for (size_t i = 0; i < fromType->size(); i++) {
    if (auto c = coercible(fromType->childAt(i), toType->childAt(i))) {
      cost += c.cost;
    } else {
      return {};
    }
  }

  return Coercion{toType, cost};
}

// static
void TypeCoercer::registerCoercions(AllowedCoercions coercions) {
  kAllowedCoercions = std::move(coercions);
}

} // namespace facebook::velox
