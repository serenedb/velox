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

#include <folly/Executor.h>
#include <glog/logging.h>
#include <yaclib/async/contract.hpp>
#include <yaclib/async/future.hpp>
#include <yaclib/async/make.hpp>
#include <yaclib/async/promise.hpp>
#include <yaclib/async/wait_for.hpp>

namespace facebook::velox {

template <class T>
class VeloxPromise {
  struct EmptyTag {
    explicit EmptyTag() = default;
  };

 public:
  VeloxPromise(VeloxPromise<T>&&) noexcept = default;
  VeloxPromise& operator=(VeloxPromise<T>&&) noexcept = default;

  VeloxPromise(EmptyTag) noexcept {}

  VeloxPromise(yaclib::Promise<T> promise) : promise{std::move(promise)} {}

  static VeloxPromise makeEmpty() noexcept {
    return VeloxPromise<T>{EmptyTag{}};
  }

  void setValue() {
    std::move(promise).Set();
  }

  bool valid() const noexcept {
    return promise.Valid();
  }

  yaclib::Promise<T> promise;
};

template <typename T>
class VeloxFuture {
  struct EmptyTag {
    explicit EmptyTag() = default;
  };

 public:
  using Base = yaclib::Future<T>;

  VeloxFuture(VeloxFuture<T>&&) noexcept = default;
  VeloxFuture& operator=(VeloxFuture<T>&&) noexcept = default;

  VeloxFuture(EmptyTag) noexcept {}

  VeloxFuture(Base future) : future{std::move(future)} {}

  static VeloxFuture makeEmpty() noexcept {
    return {EmptyTag{}};
  }

  bool valid() const noexcept {
    return future.Valid();
  }

  bool isReady() const noexcept {
    return !valid() || future.Ready();
  }

  void wait() {
    if (!isReady()) {
      Wait(future);
    }
  }

  template <typename Duration>
  bool wait(Duration timeout) {
    if (!isReady()) {
      return WaitFor(timeout, future);
    }
    return true;
  }

  T get() {
    return std::move(future).Get().Ok();
  }

  template <typename Func>
  auto thenValue(folly::Executor::KeepAlive<> executor, Func&& callback) {
    using A = std::conditional_t<std::is_void_v<T>, yaclib::Unit, T>;
    using R = decltype(callback(std::declval<A>()));
    return VeloxFuture<R>{std::move(future).ThenInline(
        [executor = std::move(executor),
         callback = std::forward<Func>(callback)](A&& value) {
          auto [f, p] = yaclib::MakeContract<R>();
          executor->add([callback = std::move(callback),
                         p = std::move(p),
                         value = std::move(value)]() mutable {
            try {
              if constexpr (std::is_void_v<R>) {
                callback(std::move(value));
                std::move(p).Set();
              } else {
                std::move(p).Set(callback(std::move(value)));
              }
            } catch (...) {
              std::move(p).Set(std::current_exception());
            }
          });
          return std::move(f);
        })};
  }

  template <typename Tag, typename Func>
  void thenError(Tag, Func&& callback) {
    std::move(future).DetachInline(
        [callback = std::forward<Func>(callback)](yaclib::Result<> r) {
          if (r) {
            return;
          }
          try {
            std::ignore = std::move(r).Ok();
          } catch (const std::exception& e) {
            callback(e);
          }
        });
  }

  template <typename... Args>
  static VeloxFuture<T> make(Args&&... args) {
    return yaclib::MakeFuture<T>(std::forward<Args>(args)...);
  }

  static auto makeVector(size_t size) {
    std::vector<VeloxFuture<T>> futures;
    futures.reserve(size);
    for (size_t i = 0; i != size; ++i) {
      futures.emplace_back(VeloxFuture<T>::makeEmpty());
    }
    return futures;
  }

  Base future;
};

template <typename T = void>
inline std::pair<VeloxPromise<T>, VeloxFuture<T>> makeVeloxContract() {
  auto [f, p] = yaclib::MakeContract<T>();
  return {std::move(p), std::move(f)};
}

using ContinuePromise = VeloxPromise<void>;
using ContinueFuture = VeloxFuture<void>;

inline std::pair<ContinuePromise, ContinueFuture>
makeVeloxContinuePromiseContract(std::string_view context) {
  return makeVeloxContract();
}

} // namespace facebook::velox
