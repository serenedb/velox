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

#include <array>
#include <limits>
#include <string>

#include "folly/CppAttributes.h"
#include "velox/dwio/common/BufferedInput.h"
#include "velox/dwio/common/Reader.h"
#include "velox/dwio/common/TypeWithId.h"
#include "velox/dwio/common/compression/Compression.h"

namespace facebook::velox::text {

using common::CompressionKind;
using common::ScanSpec;
using dwio::common::BufferedInput;
using dwio::common::ColumnSelector;
using dwio::common::ColumnStatistics;
using dwio::common::Mutation;
using dwio::common::RowReaderOptions;
using dwio::common::SerDeOptions;
using dwio::common::TypeWithId;
using memory::MemoryPool;

class MalformedRowException : public std::runtime_error {
 public:
  MalformedRowException(
      uint64_t rowNumber,
      std::string columnName,
      std::string value,
      std::string fileName)
      : std::runtime_error(
            fmt::format(
                "Malformed row {} in file {}, column {}: \"{}\"",
                rowNumber,
                fileName,
                columnName,
                value)),
        rowNumber_(rowNumber),
        columnName_(std::move(columnName)),
        value_(std::move(value)),
        fileName_(std::move(fileName)) {}

  uint64_t rowNumber() const {
    return rowNumber_;
  }

  std::string_view columnName() const {
    return columnName_;
  }

  std::string_view value() const {
    return value_;
  }

  std::string_view fileName() const {
    return fileName_;
  }

 private:
  uint64_t rowNumber_;
  std::string columnName_;
  std::string value_;
  std::string fileName_;
};

class RejectLimitExceededException : public std::runtime_error {
 public:
  RejectLimitExceededException(
      uint64_t rejectLimit,
      uint64_t rejectedRows,
      uint64_t rowNumber,
      std::string columnName,
      std::string value,
      std::string fileName)
      : std::runtime_error(
            fmt::format(
                "Exceeded reject limit {} with {} rejected rows in file {}",
                rejectLimit,
                rejectedRows,
                fileName)),
        rejectLimit_(rejectLimit),
        rejectedRows_(rejectedRows),
        rowNumber_(rowNumber),
        columnName_(std::move(columnName)),
        value_(std::move(value)),
        fileName_(std::move(fileName)) {}

  uint64_t rejectLimit() const {
    return rejectLimit_;
  }
  uint64_t rejectedRows() const {
    return rejectedRows_;
  }
  uint64_t rowNumber() const {
    return rowNumber_;
  }
  std::string_view columnName() const {
    return columnName_;
  }
  std::string_view value() const {
    return value_;
  }
  std::string_view fileName() const {
    return fileName_;
  }

 private:
  uint64_t rejectLimit_;
  uint64_t rejectedRows_;
  uint64_t rowNumber_;
  std::string columnName_;
  std::string value_;
  std::string fileName_;
};

struct RowError {
  uint64_t rowNumber;
  std::string_view columnName;
  const Type& columnType;
  std::string_view value;
  std::string_view fileName;
  uint64_t rejectLimit;
  uint64_t rejectedRows;
};

using ErrorHandler = std::function<void(const RowError&)>;

inline ErrorHandler defaultErrorHandler() {
  return [](const RowError& err) {
    if (err.rejectLimit > 0 && err.rejectedRows > err.rejectLimit) {
      throw RejectLimitExceededException(
          err.rejectLimit,
          err.rejectedRows,
          err.rowNumber,
          std::string{err.columnName},
          std::string{err.value},
          std::string{err.fileName});
    }
    throw MalformedRowException(
        err.rowNumber,
        std::string{err.columnName},
        std::string{err.value},
        std::string{err.fileName});
  };
}

class ReaderOptions : public dwio::common::ReaderOptions {
 public:
  explicit ReaderOptions(velox::memory::MemoryPool* pool)
      : dwio::common::ReaderOptions(pool) {}

  ReaderOptions& setRejectLimit(uint64_t limit) {
    rejectLimit_ = limit;
    return *this;
  }

  uint64_t rejectLimit() const {
    return rejectLimit_;
  }

  ReaderOptions& setErrorHandler(ErrorHandler handler) {
    errorHandler_ = std::move(handler);
    return *this;
  }

  const ErrorHandler& errorHandler() const {
    return errorHandler_;
  }

 private:
  uint64_t rejectLimit_{std::numeric_limits<uint64_t>::max()};
  ErrorHandler errorHandler_{defaultErrorHandler()};
};

// Shared state for a file between TextReader and TextRowReader
struct FileContents {
  FileContents(MemoryPool& pool, const std::shared_ptr<const RowType>& t);

  const size_t COLUMN_POSITION_INVALID = std::numeric_limits<size_t>::max();
  const std::shared_ptr<const RowType> schema;

  std::unique_ptr<BufferedInput> input;
  std::unique_ptr<dwio::common::SeekableInputStream> inputStream;
  std::unique_ptr<dwio::common::SeekableInputStream> decompressedInputStream;
  MemoryPool& pool;
  uint64_t fileLength;
  CompressionKind compression;
  dwio::common::compression::CompressionOptions compressionOptions;
  SerDeOptions serDeOptions;
  std::array<bool, 128> needsEscape;

  uint64_t rejectLimit{0};
  ErrorHandler errorHandler{defaultErrorHandler()};
};

using DelimType = uint8_t;
constexpr DelimType DelimTypeNone = 0;
constexpr DelimType DelimTypeEOR = 1;
constexpr DelimType DelimTypeEOE = 2;

class TextReader : public dwio::common::Reader {
 public:
  TextReader(
      const dwio::common::ReaderOptions& options,
      std::unique_ptr<BufferedInput> input);

  std::optional<uint64_t> numberOfRows() const override;

  std::unique_ptr<ColumnStatistics> columnStatistics(
      uint32_t index) const override;

  const RowTypePtr& rowType() const override;

  CompressionKind getCompression() const;

  const std::shared_ptr<const TypeWithId>& typeWithId() const override;

  std::unique_ptr<dwio::common::RowReader> createRowReader(
      const RowReaderOptions& options) const override;

  uint64_t getFileLength() const;

 private:
  ReaderOptions options_;
  mutable std::shared_ptr<const TypeWithId> typeWithId_;
  std::shared_ptr<FileContents> contents_;
  std::shared_ptr<const TypeWithId> schemaWithId_;
  std::shared_ptr<const RowType> internalSchema_;
};

class TextRowReader : public dwio::common::RowReader {
 public:
  TextRowReader(
      std::shared_ptr<FileContents> fileContents,
      const RowReaderOptions& options);

  uint64_t next(
      uint64_t size,
      VectorPtr& result,
      const Mutation* mutation = nullptr) override;

  int64_t nextRowNumber() override;

  int64_t nextReadSize(uint64_t size) override;

  void updateRuntimeStats(
      dwio::common::RuntimeStatistics& stats) const override;

  void resetFilterCaches() override;

  std::optional<size_t> estimatedRowSize() const override;

  const ColumnSelector& getColumnSelector() const;

  std::shared_ptr<const TypeWithId> getSelectedType() const;

  uint64_t getRowNumber() const;

  uint64_t seekToRow(uint64_t rowNumber);

  uint64_t rejectedRowCount() const {
    return rejectedRows_;
  }

 private:
  const RowReaderOptions& getDefaultOpts();

  const std::shared_ptr<const RowType>& getType() const;

  bool isSelectedField(const std::shared_ptr<const TypeWithId>& t);

  const char* getStreamNameData() const;

  uint64_t getLength();

  uint64_t getStreamLength() const;

  void setEOF();

  void incrementDepth();

  void decrementDepth(DelimType& delim);

  void setEOE(DelimType& delim);

  void resetEOE(DelimType& delim);

  bool isEOE(DelimType delim);

  void setEOR(DelimType& delim);

  bool isEOR(DelimType delim);

  bool isOuterEOR(DelimType delim);

  bool isEOEorEOR(DelimType delim);

  void setNone(DelimType& delim);

  bool isNone(DelimType delim);

  DelimType getDelimType(uint8_t v);

  template <bool skipLF = false>
  char getByteUnchecked(DelimType& delim);

  template <bool skipLF = false>
  char getByteUncheckedOptimized(DelimType& delim);

  uint8_t getByte(DelimType& delim);
  uint8_t getByteOptimized(DelimType& delim);

  bool getEOR(DelimType& delim, bool& isNull);

  bool skipLine();

  void resetLine();

  static std::string&
  getString(TextRowReader& th, bool& isNull, DelimType& delim);

  template <typename T>
  static T getInteger(TextRowReader& th, bool& isNull, DelimType& delim);

  static bool getBoolean(TextRowReader& th, bool& isNull, DelimType& delim);

  static float getFloat(TextRowReader& th, bool& isNull, DelimType& delim);

  static double getDouble(TextRowReader& th, bool& isNull, DelimType& delim);

  void readElement(
      const std::shared_ptr<const Type>& t,
      const std::shared_ptr<const Type>& reqT,
      BaseVector* FOLLY_NULLABLE data,
      vector_size_t insertionRow,
      DelimType& delim);

  template <class T, class reqT>
  void putValue(
      const std::function<T(TextRowReader& th, bool& isNull, DelimType& delim)>&
          f,
      BaseVector* FOLLY_NULLABLE data,
      vector_size_t insertionRow,
      DelimType& delim);

  template <class T>
  void setValueFromString(
      const std::string& str,
      BaseVector* FOLLY_NULLABLE data,
      vector_size_t insertionRow,
      std::function<std::optional<T>(const std::string&)> convert);

  const std::shared_ptr<FileContents> contents_;
  const std::shared_ptr<const TypeWithId> schemaWithId_;
  const std::shared_ptr<velox::common::ScanSpec>& scanSpec_;

  mutable std::shared_ptr<const TypeWithId> selectedSchema_;

  RowReaderOptions options_;
  ColumnSelector columnSelector_;
  uint64_t currentRow_;
  uint64_t pos_;
  bool atEOL_;
  bool atEOF_;
  bool atSOL_;
  bool atPhysicalEOF_;
  uint8_t depth_;
  std::string unreadData_;
  std::string_view preLoadedUnreadData_;
  int unreadIdx_;
  uint64_t limit_; // lowest offset not in the range
  uint64_t fileLength_;
  std::string ownedString_;
  std::shared_ptr<dwio::common::DataBuffer<char>> varBinBuf_;
  uint64_t rejectedRows_{0};
  bool rowHasError_{false};
  std::string errorValue_;
};

} // namespace facebook::velox::text
