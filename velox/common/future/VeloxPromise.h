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

#pragma once

#include <folly/Unit.h>
#include <folly/futures/Future.h>

namespace facebook::velox {
/// Simple wrapper around folly's promise to track down destruction of
/// unfulfilled promises.
template <class T>
class VeloxPromise : public folly::Promise<T> {
 public:
  struct ContextTag {
    explicit ContextTag() = default;
  };

  struct EmptyTag {
    explicit EmptyTag() = default;
  };

  explicit VeloxPromise(ContextTag, std::string&& context)
      : folly::Promise<T>{}, context_{std::move(context)} {
    if (context_.empty()) {
      LOG(WARNING)
          << "PROMISE: VeloxPromise must be constructed with a context.";
    }
  }

  explicit VeloxPromise(EmptyTag) noexcept
      : folly::Promise<T>{folly::Promise<T>::makeEmpty()} {}

  ~VeloxPromise() {
    if (!this->isFulfilled()) {
      LOG(WARNING) << "PROMISE: Unfulfilled promise is being deleted. Context: "
                   << context_;
    }
  }

  VeloxPromise(VeloxPromise<T>&& other) noexcept = default;
  VeloxPromise& operator=(VeloxPromise<T>&& other) noexcept = default;

  static VeloxPromise makeEmpty() noexcept {
    return VeloxPromise<T>{EmptyTag{}};
  }

 private:
  /// Optional parameter to understand where this promise was created.
  std::string context_;
};

using ContinuePromise = VeloxPromise<folly::Unit>;
using ContinueFuture = folly::SemiFuture<folly::Unit>;

/// Equivalent of folly's makePromiseContract for VeloxPromise.
///
/// NOTE: When you already have a valid promise, just call
/// Promise::getSemiFuture() on it to get the future, instead of using this
/// function to overwrite the promise.  Overwriting valid promise would cause
/// exception throwing and stack unwinding thus performance issue.  See
/// https://github.com/prestodb/presto/issues/26094 for details.
inline std::pair<ContinuePromise, ContinueFuture>
makeVeloxContinuePromiseContract(std::string&& promiseContext) {
  auto p =
      ContinuePromise{ContinuePromise::ContextTag{}, std::move(promiseContext)};
  auto f = p.getSemiFuture();
  return {std::move(p), std::move(f)};
}

} // namespace facebook::velox
