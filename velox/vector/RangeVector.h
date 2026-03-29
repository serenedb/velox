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

#include "velox/vector/ComplexVector.h"
#include "velox/vector/FlatVector.h"

namespace facebook::velox {

/// ArrayVector subclass that stores only per-row {start, step} metadata
/// instead of materializing all elements. Used by generate_series to avoid
/// allocating memory proportional to the range size.
///
/// Must only be consumed by the Unnest operator which detects RangeVector
/// via dynamic_cast and calls materializeElements() per-batch.
class RangeVector : public ArrayVector {
 public:
  struct RowMeta {
    int64_t start;
    int64_t step;
  };

  RangeVector(
      memory::MemoryPool* pool,
      TypePtr type,
      BufferPtr nulls,
      vector_size_t length,
      BufferPtr offsets,
      BufferPtr sizes,
      std::vector<RowMeta> metas)
      : ArrayVector(
            pool,
            std::move(type),
            std::move(nulls),
            length,
            std::move(offsets),
            std::move(sizes),
            BaseVector::create(BIGINT(), 0, pool)),
        metas_(std::move(metas)) {}

  std::span<const RowMeta> metas() const {
    return metas_;
  }

  /// Skip the elements size validation since elements is intentionally empty.
  void validate(const VectorValidateOptions& options) const override {
    this->BaseVector::validate(options);
  }

  // RangeVector must only be consumed by Unnest via materializeElements().
  // These overrides prevent accidental use in other contexts.
  VectorPtr slice(
      vector_size_t /*offset*/, vector_size_t /*length*/) const override {
    VELOX_FAIL("RangeVector does not support slice, use Unnest");
  }

  void copyRanges(const BaseVector* /*source*/,
      const folly::Range<const CopyRange*>& /*ranges*/) override {
    VELOX_FAIL("RangeVector does not support copyRanges, use Unnest");
  }

  void ensureWritable(const SelectivityVector& /*rows*/) override {
    VELOX_FAIL("RangeVector does not support ensureWritable");
  }

  void prepareForReuse() override {
    VELOX_FAIL("RangeVector does not support prepareForReuse");
  }

  /// Materialize elements for the given global element indices.
  /// Each index is a position in the flattened element space defined by
  /// the offsets/sizes arrays. Returns a FlatVector<int64_t> of size 'count'.
  VectorPtr materializeElements(
      const vector_size_t* indices,
      vector_size_t count,
      memory::MemoryPool* pool) const {
    auto result =
        BaseVector::create<FlatVector<int64_t>>(BIGINT(), count, pool);
    auto* raw = result->mutableRawValues();

    const auto* offsets = this->rawOffsets();

    for (vector_size_t i = 0; i < count; ++i) {
      const auto globalIdx = indices[i];
      const auto [meta, localOffset] = findRow(globalIdx, offsets);
      raw[i] = meta.start + localOffset * meta.step;
    }
    return result;
  }

 private:
  std::pair<RowMeta, int64_t> findRow(
      vector_size_t globalIdx,
      const vector_size_t* rawOffsets) const {
    vector_size_t lo = 0;
    vector_size_t hi = metas_.size();
    while (lo + 1 < hi) {
      auto mid = lo + (hi - lo) / 2;
      if (rawOffsets[mid] <= globalIdx) {
        lo = mid;
      } else {
        hi = mid;
      }
    }
    return {metas_[lo], static_cast<int64_t>(globalIdx) - rawOffsets[lo]};
  }

  std::vector<RowMeta> metas_;
};

} // namespace facebook::velox
