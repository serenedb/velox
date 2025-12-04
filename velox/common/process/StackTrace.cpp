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

#include "velox/common/process/StackTrace.h"

#include "velox/common/process/ProcessBase.h"

#include <absl/debugging/stacktrace.h>
#include <absl/debugging/symbolize.h>
#include <folly/Conv.h>

namespace facebook::velox::process {

StackTrace::StackTrace(int32_t skipFrames) {
  create(skipFrames);
}

StackTrace::StackTrace(const StackTrace& other) {
  btPtrs_ = other.btPtrs_;
  if (folly::old::test_once(other.btVectorFlag_)) {
    btVector_ = other.btVector_;
    folly::old::call_once(btVectorFlag_, [] {}); // Set the flag.
  }
  if (folly::old::test_once(other.btFlag_)) {
    bt_ = other.bt_;
    folly::old::call_once(btFlag_, [] {}); // Set the flag.
  }
}

StackTrace& StackTrace::operator=(const StackTrace& other) {
  if (this != &other) {
    this->~StackTrace();
    new (this) StackTrace(other);
  }
  return *this;
}

void StackTrace::create(int32_t skipFrames) {
  // ::create(), ::StackTrace()
  static constexpr int32_t kDefaultSkipFrameAdjust = 2;
  static constexpr int32_t kMaxFrames = 75;

  btPtrs_.clear();
  void* btPtrs[kMaxFrames];
  auto framecount = absl::GetStackTrace(
      btPtrs, kMaxFrames, skipFrames + kDefaultSkipFrameAdjust);
  if (framecount <= 0) {
    return;
  }

  btPtrs_.assign(btPtrs, btPtrs + framecount);
}

///////////////////////////////////////////////////////////////////////////////
// reporting functions

const std::vector<std::string>& StackTrace::toStrVector() const {
  folly::old::call_once(btVectorFlag_, [&] {
    size_t frame = 0;
    std::string_view framename;
    char demangled[1024];
    for (auto ptr : btPtrs_) {
      if (absl::Symbolize(ptr, demangled, sizeof(demangled))) {
        framename = demangled;
      } else {
        framename = "*no symbol name available for this frame";
      }
      btVector_.push_back(fmt::format("# {:<2d} {}", frame++, framename));
    }
  });
  return btVector_;
}

const std::string& StackTrace::toString() const {
  folly::old::call_once(btFlag_, [&] {
    const auto& vec = toStrVector();
    size_t needed = 0;
    for (const auto& frame : vec) {
      needed += frame.size() + 1;
    }
    bt_.reserve(needed);
    for (const auto& frameTitle : vec) {
      bt_ += frameTitle;
      bt_ += '\n';
    }
  });
  return bt_;
}

} // namespace facebook::velox::process
