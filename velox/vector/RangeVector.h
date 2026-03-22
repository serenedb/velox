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

#include "velox/common/base/SimdUtil.h"
#include "velox/vector/BuilderTypeUtils.h"
#include "velox/vector/FlatVector.h"
#include "velox/vector/SimpleVector.h"

namespace facebook::velox {

/// Provides a vector type that stores an arithmetic sequence defined by
/// (start, step, length). The value at index i is: start + step * i.
///
/// This is O(1) memory regardless of vector length, making it ideal for
/// generate_series/sequence functions that produce monotonic integer sequences.
///
/// Example: RangeVector(start=0, step=1, length=100001)
///   represents {0, 1, 2, ..., 100000} using only 3 scalars.
template <typename T>
class RangeVector : public SimpleVector<T> {
 public:
#ifdef VELOX_ENABLE_LOAD_SIMD_VALUE_BUFFER
  static constexpr bool can_simd =
      (std::is_same_v<T, int64_t> || std::is_same_v<T, int32_t> ||
       std::is_same_v<T, int16_t> || std::is_same_v<T, int8_t> ||
       std::is_same_v<T, size_t>);
#endif

  RangeVector(
      velox::memory::MemoryPool* pool,
      TypePtr type,
      vector_size_t length,
      T start,
      T step)
      : SimpleVector<T>(
            pool,
            std::move(type),
            VectorEncoding::Simple::RANGE,
            BufferPtr(nullptr),
            length,
            SimpleVectorStats<T>{},
            std::nullopt,
            0,
            step > 0 ? std::optional<bool>(true)
            : step < 0
                ? std::optional<bool>(false)
                : std::optional<bool>(true),
            std::nullopt,
            std::nullopt),
        start_(start),
        step_(step) {}

  ~RangeVector() override = default;

  bool mayHaveNulls() const override {
    return false;
  }

  bool mayHaveNullsRecursive() const override {
    return false;
  }

  bool isNullAt(vector_size_t /*idx*/) const override {
    return false;
  }

  bool containsNullAt(vector_size_t /*idx*/) const override {
    return false;
  }

  typename SimpleVector<T>::TValueAt valueAtFast(vector_size_t idx) const {
    return static_cast<T>(
        static_cast<int128_t>(start_) +
        static_cast<int128_t>(step_) * static_cast<int128_t>(idx));
  }

  typename SimpleVector<T>::TValueAt valueAt(
      vector_size_t idx) const override {
    return valueAtFast(idx);
  }

  std::unique_ptr<SimpleVector<uint64_t>> hashAll() const override {
    if (BaseVector::length_ == 0) {
      return nullptr;
    }
    BufferPtr hashes = AlignedBuffer::allocate<uint64_t>(
        BaseVector::length_, BaseVector::pool_);
    auto* rawHashes = hashes->asMutable<uint64_t>();
    for (vector_size_t i = 0; i < BaseVector::length_; ++i) {
      rawHashes[i] = this->hashValueAt(i);
    }
    return std::make_unique<FlatVector<uint64_t>>(
        BaseVector::pool_,
        BIGINT(),
        BufferPtr(nullptr),
        BaseVector::length_,
        std::move(hashes),
        std::vector<BufferPtr>(),
        SimpleVectorStats<uint64_t>{},
        std::nullopt,
        0,
        false,
        sizeof(uint64_t) * BaseVector::length_);
  }

#ifdef VELOX_ENABLE_LOAD_SIMD_VALUE_BUFFER
  xsimd::batch<T> loadSIMDValueBufferAt(size_t index) const {
    if constexpr (std::is_same_v<T, bool>) {
      throw std::runtime_error(
          "Range encoding only supports SIMD operations on integers");
    } else {
      constexpr int kBatchSize = xsimd::batch<T>::size;
      alignas(xsimd::default_arch::alignment()) T tmp[kBatchSize];
      for (int i = 0; i < kBatchSize; ++i) {
        tmp[i] = valueAtFast(index + i);
      }
      return xsimd::load_aligned(tmp);
    }
  }
#endif

  bool isScalar() const override {
    return true;
  }

  VectorPtr slice(vector_size_t offset, vector_size_t length) const override {
    return std::make_shared<RangeVector<T>>(
        BaseVector::pool_,
        BaseVector::type_,
        length,
        valueAtFast(offset),
        step_);
  }

  bool isNullsWritable() const override {
    return false;
  }

  VectorPtr testingCopyPreserveEncodings(
      velox::memory::MemoryPool* pool) const override {
    auto selfPool = pool ? pool : BaseVector::pool_;
    return std::make_shared<RangeVector<T>>(
        selfPool, BaseVector::type_, BaseVector::length_, start_, step_);
  }

  void transferOrCopyTo(velox::memory::MemoryPool* /*pool*/) override {
    VELOX_NYI("{} unsupported", __FUNCTION__);
  }

  std::string toString(vector_size_t index) const override {
    std::stringstream out;
    out << "range[" << index << "] = " << valueAtFast(index);
    return out.str();
  }

  T start() const {
    return start_;
  }

  T step() const {
    return step_;
  }

 private:
  uint64_t retainedSizeImpl(
      uint64_t& /*totalStringBufferSize*/) const override {
    return sizeof(start_) + sizeof(step_);
  }

  T start_;
  T step_;
};

template <typename T>
using RangeVectorPtr = std::shared_ptr<RangeVector<T>>;

} // namespace facebook::velox
