/*
 * Licensed to the Apache Software Foundation (ASF) under one
 * or more contributor license agreements.  See the NOTICE file
 * distributed with this work for additional information
 * regarding copyright ownership.  The ASF licenses this file
 * to you under the Apache License, Version 2.0 (the
 * "License"); you may not use this file except in compliance
 * with the License.  You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing,
 * software distributed under the License is distributed on an
 * "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
 * KIND, either express or implied.  See the License for the
 * specific language governing permissions and limitations
 * under the License.
 */

#ifdef GRAPHAR_VORTEX

#include <cstdlib>
#include <iostream>
#include <string>

#include "arrow/api.h"
#include "arrow/filesystem/api.h"

#include <catch2/catch_test_macros.hpp>

#include "graphar/filesystem.h"
#include "graphar/fwd.h"
#include "graphar/types.h"
#include "graphar/writer_util.h"

namespace graphar {

TEST_CASE("VortexFileFormat") {
  // Create a simple Arrow Table for testing
  auto int_builder = std::make_shared<arrow::Int64Builder>();
  auto str_builder = std::make_shared<arrow::StringBuilder>();
  auto float_builder = std::make_shared<arrow::DoubleBuilder>();

  REQUIRE(int_builder->AppendValues({1, 2, 3, 4, 5}).ok());
  REQUIRE(str_builder->AppendValues({"Alice", "Bob", "Carol", "Dave", "Eve"})
              .ok());
  REQUIRE(float_builder->AppendValues({1.1, 2.2, 3.3, 4.4, 5.5}).ok());

  std::shared_ptr<arrow::Array> int_array, str_array, float_array;
  REQUIRE(int_builder->Finish(&int_array).ok());
  REQUIRE(str_builder->Finish(&str_array).ok());
  REQUIRE(float_builder->Finish(&float_array).ok());

  auto schema = arrow::schema({arrow::field("id", arrow::int64()),
                               arrow::field("name", arrow::utf8()),
                               arrow::field("score", arrow::float64())});
  auto table = arrow::Table::Make(schema, {int_array, str_array, float_array});
  REQUIRE(table->num_rows() == 5);
  REQUIRE(table->num_columns() == 3);

  // Create a local FileSystem
  auto arrow_fs = std::make_shared<arrow::fs::LocalFileSystem>();
  auto fs = std::make_shared<FileSystem>(arrow_fs);

  const std::string vortex_path = "/tmp/graphar_test_vortex/test_table.vortex";
  // Ensure directory exists
  REQUIRE(arrow_fs->CreateDir("/tmp/graphar_test_vortex").ok());

  SECTION("WriteAndReadRoundTrip") {
    // Write table to vortex file
    auto write_options = std::make_shared<WriterOptions>();
    auto status = fs->WriteTableToFile(table, FileType::VORTEX, vortex_path,
                                       write_options);
    REQUIRE(status.ok());

    // Read it back
    std::vector<int> all_columns;
    auto result = fs->ReadFileToTable(vortex_path, FileType::VORTEX, all_columns);
    REQUIRE(!result.has_error());
    auto read_table = result.value();

    // Verify schema and data
    REQUIRE(read_table->num_rows() == 5);
    REQUIRE(read_table->num_columns() == 3);
    REQUIRE(read_table->schema()->field(0)->name() == "id");
    REQUIRE(read_table->schema()->field(1)->name() == "name");
    REQUIRE(read_table->schema()->field(2)->name() == "score");

    // Verify column data types (vortex may convert utf8 -> utf8_view)
    REQUIRE(read_table->schema()->field(0)->type()->id() == arrow::Type::INT64);
    REQUIRE((read_table->schema()->field(1)->type()->id() ==
                 arrow::Type::STRING ||
             read_table->schema()->field(1)->type()->id() ==
                 arrow::Type::STRING_VIEW));
    REQUIRE(read_table->schema()->field(2)->type()->id() ==
            arrow::Type::DOUBLE);

    std::cout << "[VortexRoundTrip] Read back " << read_table->num_rows()
              << " rows, " << read_table->num_columns() << " columns"
              << std::endl;
  }

  SECTION("ReadWithColumnSelection") {
    // Write first
    auto write_options = std::make_shared<WriterOptions>();
    auto status = fs->WriteTableToFile(table, FileType::VORTEX, vortex_path,
                                       write_options);
    REQUIRE(status.ok());

    // Read with column indices: only "id" (0) and "score" (2)
    std::vector<int> column_indices = {0, 2};
    auto result =
        fs->ReadFileToTable(vortex_path, FileType::VORTEX, column_indices);
    REQUIRE(!result.has_error());
    auto read_table = result.value();

    REQUIRE(read_table->num_rows() == 5);
    REQUIRE(read_table->num_columns() == 2);
    REQUIRE(read_table->schema()->field(0)->name() == "id");
    REQUIRE(read_table->schema()->field(1)->name() == "score");

    std::cout << "[VortexColumnSelect] Read back " << read_table->num_rows()
              << " rows, " << read_table->num_columns() << " columns"
              << std::endl;
  }

  SECTION("FileTypeConversion") {
    // Verify FileType string conversion works for vortex
    REQUIRE(std::string(FileTypeToString(FileType::VORTEX)) == "vortex");
    REQUIRE(StringToFileType("vortex") == FileType::VORTEX);
  }

  // Cleanup
  REQUIRE(arrow_fs->DeleteDir("/tmp/graphar_test_vortex").ok());
}

}  // namespace graphar

#endif  // GRAPHAR_VORTEX
