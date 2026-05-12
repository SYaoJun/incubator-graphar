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

#include <arrow/compute/api.h>
#include <cstddef>
#include <iostream>
#include <memory>
#include <ostream>
#include <string>
#include "arrow/api.h"
#include "graphar/api/high_level_reader.h"
#include "graphar/arrow/chunk_reader.h"
#include "graphar/arrow/chunk_writer.h"
#include "graphar/graph_info.h"
#include "parquet/arrow/writer.h"

#include "./util.h"

#include <catch2/catch_test_macros.hpp>

std::shared_ptr<arrow::Table> read_csv_to_table(const std::string& filename) {
  arrow::csv::ReadOptions read_options{};
  arrow::csv::ParseOptions parse_options{};
  arrow::csv::ConvertOptions convert_options{};

  parse_options.delimiter = '|';

  auto input =
      arrow::io::ReadableFile::Open(filename, arrow::default_memory_pool())
          .ValueOrDie();

  auto reader = arrow::csv::TableReader::Make(arrow::io::default_io_context(),
                                              input, read_options,
                                              parse_options, convert_options)
                    .ValueOrDie();

  std::shared_ptr<arrow::Table> table;
  table = reader->Read().ValueOrDie();

  return table;
}

namespace graphar {
TEST_CASE_METHOD(GlobalFixture, "test_multi_label_builder") {
  std::cout << "Test multi label builder" << std::endl;

  // construct graph information from file
  std::string path = test_data_dir + "/ldbc/parquet/" + "ldbc.graph.yml";
  auto graph_info = graphar::GraphInfo::Load(path).value();
  auto vertex_info = graph_info->GetVertexInfo("organisation");

  auto labels = vertex_info->GetLabels();

  std::unordered_map<std::string, size_t> code;

  std::vector<std::vector<bool>> label_column_data;

  // read labels csv file as arrow table
  auto table = read_csv_to_table(test_data_dir + "/ldbc/organisation_0_0.csv");
  std::string table_message = table->ToString();

  auto schema = table->schema();
  std::cout << schema->ToString() << std::endl;

  // write arrow table as parquet chunk
  auto maybe_writer =
      VertexPropertyWriter::Make(vertex_info, "/tmp/ldbc/parquet/");
  REQUIRE(!maybe_writer.has_error());
  auto writer = maybe_writer.value();
  REQUIRE(writer->WriteTable(table, 0).ok());
  REQUIRE(writer->WriteVerticesNum(table->num_rows()).ok());

  // read label chunk as arrow table
  auto maybe_reader = VertexPropertyArrowChunkReader::Make(
      vertex_info, labels, "/tmp/ldbc/parquet/");
  REQUIRE(maybe_reader.status().ok());
  auto reader = maybe_reader.value();
  REQUIRE(reader->seek(0).ok());
  REQUIRE(reader->GetLabelChunk().status().ok());
  REQUIRE(reader->next_chunk().ok());
}

TEST_CASE_METHOD(GlobalFixture, "test_vertices_with_multiple_labels") {
  std::cout << "Test vertices with multiple labels" << std::endl;

  std::string path = test_data_dir + "/ldbc/parquet/" + "ldbc.graph.yml";
  auto graph_info = graphar::GraphInfo::Load(path).value();
  std::string type = "organisation";

  // Test verticesWithMultipleLabels
  std::vector<std::string> filter_labels = {"university", "company"};
  auto maybe_vertices_collection =
      VerticesCollection::verticesWithMultipleLabels(filter_labels, graph_info,
                                                     type);
  REQUIRE(!maybe_vertices_collection.has_error());
  auto vertices_collection = maybe_vertices_collection.value();

  // Verify the filtered vertices contain both labels
  auto vertex_info = graph_info->GetVertexInfo(type);
  auto all_labels = vertex_info->GetLabels();
  REQUIRE(!all_labels.empty());

  // Iterate through filtered vertices and verify they have both labels
  size_t count = 0;
  for (auto it = vertices_collection->begin(); it != vertices_collection->end();
       ++it) {
    auto labels_result = it.label();
    REQUIRE(labels_result.status().ok());
    auto vertex_labels = labels_result.value();
    // Check that the vertex has both "university" and "company" labels
    bool has_university = false;
    bool has_company = false;
    for (const auto& label : vertex_labels) {
      if (label == "university")
        has_university = true;
      if (label == "company")
        has_company = true;
    }
    REQUIRE(has_university);
    REQUIRE(has_company);
    count++;
  }
  std::cout << "Filtered " << count
            << " vertices with labels 'university' and 'company'" << std::endl;
  REQUIRE(count > 0);
}

TEST_CASE_METHOD(GlobalFixture, "test_vertices_with_multiple_labels_by_acero") {
  std::cout << "Test vertices with multiple labels by Acero" << std::endl;

  std::string path = test_data_dir + "/ldbc/parquet/" + "ldbc.graph.yml";
  auto graph_info = graphar::GraphInfo::Load(path).value();
  std::string type = "organisation";

  // Test verticesWithMultipleLabelsbyAcero
  std::vector<std::string> filter_labels = {"university", "company"};
  auto maybe_vertices_collection =
      VerticesCollection::verticesWithMultipleLabelsbyAcero(filter_labels,
                                                            graph_info, type);
  REQUIRE(!maybe_vertices_collection.has_error());
  auto vertices_collection = maybe_vertices_collection.value();

  // Verify the filtered vertices contain both labels
  auto vertex_info = graph_info->GetVertexInfo(type);
  auto all_labels = vertex_info->GetLabels();
  REQUIRE(!all_labels.empty());

  // Iterate through filtered vertices and verify they have both labels
  size_t count = 0;
  for (auto it = vertices_collection->begin(); it != vertices_collection->end();
       ++it) {
    auto labels_result = it.label();
    REQUIRE(labels_result.status().ok());
    auto vertex_labels = labels_result.value();
    // Check that the vertex has both "university" and "company" labels
    bool has_university = false;
    bool has_company = false;
    for (const auto& label : vertex_labels) {
      if (label == "university")
        has_university = true;
      if (label == "company")
        has_company = true;
    }
    REQUIRE(has_university);
    REQUIRE(has_company);
    count++;
  }
  std::cout << "Filtered (by Acero) " << count
            << " vertices with labels 'university' and 'company'" << std::endl;
  REQUIRE(count > 0);
}
}  // namespace graphar
