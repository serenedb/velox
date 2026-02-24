# Copyright (c) Facebook, Inc. and its affiliates.
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

if(${VELOX_ENABLE_HIVE_CONNECTOR})
  set(VELOX_ENABLE_HIVE_CONNECTOR_UTILS ON)
endif()

velox_add_library(velox_hive_config OBJECT HiveConfig.cpp)
velox_link_libraries(velox_hive_config velox_core velox_exception)

velox_add_library(
  velox_hive_connector_utils
  OBJECT
  HiveConnectorUtil.cpp
  TableHandle.cpp)

velox_link_libraries(
  velox_hive_connector_utils
  PUBLIC velox_hive_config velox_connector
  PRIVATE velox_common_io velox_dwio_catalog_fbhive)

if(${VELOX_ENABLE_HIVE_CONNECTOR})
  add_subdirectory(iceberg)

  velox_add_library(
    velox_hive_connector
    OBJECT
    BufferedInputBuilder.cpp
    FileHandle.cpp
    HiveConnector.cpp
    HiveConnectorSplit.cpp
    HiveDataSink.cpp
    HiveDataSource.cpp
    HivePartitionName.cpp
    PartitionIdGenerator.cpp
    SplitReader.cpp)

  velox_link_libraries(
    velox_hive_connector
    PUBLIC velox_hive_iceberg_splitreader velox_hive_connector_utils
    PRIVATE velox_common_io velox_connector velox_dwio_catalog_fbhive
            velox_hive_partition_function)

  velox_add_library(velox_hive_partition_function HivePartitionFunction.cpp)
  velox_link_libraries(velox_hive_partition_function velox_core velox_exec)
endif()

if(${VELOX_BUILD_TESTING})
  add_subdirectory(tests)
endif()

if(${VELOX_ENABLE_BENCHMARKS})
  add_subdirectory(benchmarks)
endif()
