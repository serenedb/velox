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
class VeloxPromise {
 public:
  struct EmptyTag {
    explicit EmptyTag() = default;
  };

  explicit VeloxPromise(folly::Promise<T> promise, std::string&& context)
      : promise_{std::move(promise)}, context_{std::move(context)} {
    if (context_.empty()) {
      LOG(WARNING)
          << "PROMISE: VeloxPromise must be constructed with a context.";
    }
  }

  explicit VeloxPromise(EmptyTag) noexcept
      : promise_{folly::Promise<T>::makeEmpty()} {}

  ~VeloxPromise() {
    if (!promise_.isFulfilled()) {
      LOG(WARNING) << "PROMISE: Unfulfilled promise is being deleted. Context: "
                   << context_;
    }
  }

  VeloxPromise(VeloxPromise<T>&& other) noexcept = default;
  VeloxPromise& operator=(VeloxPromise<T>&& other) noexcept = default;

  static VeloxPromise makeEmpty() noexcept {
    return VeloxPromise<T>{EmptyTag{}};
  }

  void setValue() {
    promise_.setValue();
  }

  bool valid() const noexcept {
    return promise_.valid();
  }

 private:
  folly::Promise<T> promise_;
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
makeVeloxContinuePromiseContract(std::string&& context) {
  auto [p, f] = folly::makePromiseContract<folly::Unit>();
  return {ContinuePromise{std::move(p), std::move(context)}, std::move(f)};
}

} // namespace facebook::velox
