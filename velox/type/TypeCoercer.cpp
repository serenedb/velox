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

namespace facebook::velox {

int64_t Coercion::overallCost(const std::vector<Coercion>& coercions) {
  int64_t cost = 0;
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
std::optional<Coercion> TypeCoercer::coerceTypeBase(
    const TypePtr& fromType,
    const std::string& toTypeName) {
  if (fromType->name() == toTypeName) {
    return Coercion{.type = fromType, .cost = 0};
  }

  auto it = kAllowedCoercions.find({fromType->name(), toTypeName});
  if (it != kAllowedCoercions.end()) {
    return it->second;
  }

  return std::nullopt;
}

// static
bool TypeCoercer::coercible(const TypePtr& fromType, const TypePtr& toType) {
  if (fromType->isUnKnown()) {
    return true;
  }

  if (fromType->size() == 0) {
    if (auto coercion = TypeCoercer::coerceTypeBase(fromType, toType->name())) {
      return true;
    }

    return false;
  }

  if (fromType->name() != toType->name() ||
      fromType->size() != toType->size()) {
    return false;
  }

  for (auto i = 0; i < fromType->size(); i++) {
    if (!coercible(fromType->childAt(i), toType->childAt(i))) {
      return false;
    }
  }

  return true;
}

// static
void TypeCoercer::registerCoercions(AllowedCoercions coercions) {
  kAllowedCoercions = std::move(coercions);
}

} // namespace facebook::velox
