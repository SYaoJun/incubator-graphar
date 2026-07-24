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

#include <parquet/types.h>
#include <chrono>
#include <fstream>
#include <iostream>
#include <ostream>
#include <sstream>
#include <string>

#include "arrow/api.h"
#include "arrow/compute/api.h"
#include "arrow/dataset/api.h"
#include "graphar/label.h"
#include "graphar/util.h"
#include "graphar/writer_util.h"
#ifdef ARROW_ORC
#include "arrow/adapters/orc/adapter.h"
#endif
#include "arrow/csv/api.h"
#include "arrow/filesystem/api.h"
#include "arrow/io/api.h"
#include "arrow/stl.h"
#include "arrow/util/uri.h"
#include "parquet/arrow/reader.h"
#include "parquet/arrow/writer.h"

#include "./util.h"
#include "graphar/api/arrow_writer.h"

#include <catch2/catch_test_macros.hpp>

namespace graphar {

TEST_CASE_METHOD(GlobalFixture, "TestVertexPropertyWriter") {
  std::string path = test_data_dir + "/ldbc_sample/person_0_0.csv";
  arrow::io::IOContext io_context = arrow::io::default_io_context();

  auto fs = arrow::fs::FileSystemFromUriOrPath(path).ValueOrDie();
  std::shared_ptr<arrow::io::InputStream> input =
      fs->OpenInputStream(path).ValueOrDie();

  auto read_options = arrow::csv::ReadOptions::Defaults();
  auto parse_options = arrow::csv::ParseOptions::Defaults();
  parse_options.delimiter = '|';
  auto convert_options = arrow::csv::ConvertOptions::Defaults();

  // Instantiate TableReader from input stream and options
  auto maybe_reader = arrow::csv::TableReader::Make(
      io_context, input, read_options, parse_options, convert_options);
  REQUIRE(maybe_reader.ok());
  std::shared_ptr<arrow::csv::TableReader> reader = *maybe_reader;

  // Read table from CSV file
  auto maybe_table = reader->Read();
  REQUIRE(maybe_table.ok());
  std::shared_ptr<arrow::Table> table = *maybe_table;
  std::cout << table->num_rows() << ' ' << table->num_columns() << std::endl;

  // Construct the writer
  std::string vertex_meta_file_parquet =
      test_data_dir + "/ldbc_sample/parquet/" + "person.vertex.yml";
  auto vertex_meta_parquet = Yaml::LoadFile(vertex_meta_file_parquet).value();
  auto vertex_info_parquet = VertexInfo::Load(vertex_meta_parquet).value();
  auto maybe_writer = VertexPropertyWriter::Make(vertex_info_parquet, "/tmp/");
  REQUIRE(!maybe_writer.has_error());
  auto writer = maybe_writer.value();

  // Get & set validate level
  REQUIRE(writer->GetValidateLevel() == ValidateLevel::no_validate);
  writer->SetValidateLevel(ValidateLevel::strong_validate);
  REQUIRE(writer->GetValidateLevel() == ValidateLevel::strong_validate);

  // Valid cases
  // Write the table
  REQUIRE(writer->WriteTable(table, 0).ok());
  // Write the number of vertices
  REQUIRE(writer->WriteVerticesNum(table->num_rows()).ok());

  // Check vertex count
  input = fs->OpenInputStream("/tmp/vertex/person/vertex_count").ValueOrDie();
  auto num = input->Read(sizeof(IdType)).ValueOrDie();
  const IdType* ptr = reinterpret_cast<const IdType*>(num->data());
  REQUIRE((*ptr) == table->num_rows());

  // Invalid cases
  // Invalid vertices number
  REQUIRE(writer->WriteVerticesNum(-1).IsInvalid());
  // Out of range
  REQUIRE(writer->WriteChunk(table, 0).IsInvalid());
  // Invalid chunk id
  auto chunk = table->Slice(0, vertex_info_parquet->GetChunkSize());
  REQUIRE(writer->WriteChunk(chunk, -1).IsIndexError());
  // Invalid property group
  Property p1("invalid_property", int32(), false);
  auto pg1 = CreatePropertyGroup({p1}, FileType::CSV);
  REQUIRE(writer->WriteTable(table, pg1, 0).IsKeyError());
  // Property not found in table
  std::shared_ptr<arrow::Table> tmp_table =
      table->RenameColumns({"original_id", "firstName", "lastName", "id"})
          .ValueOrDie();
  auto pg2 = vertex_info_parquet->GetPropertyGroup("firstName");
  REQUIRE(writer->WriteTable(tmp_table, pg2, 0).IsInvalid());
  // Invalid data type
  auto pg3 = vertex_info_parquet->GetPropertyGroup("id");
  REQUIRE(writer->WriteTable(tmp_table, pg3, 0).IsTypeError());

#ifdef ARROW_ORC
  SECTION("TestOrcParquetReader") {
    arrow::Status st;
    arrow::MemoryPool* pool = arrow::default_memory_pool();
    std::string path1 = test_data_dir + "/ldbc_sample/orc" +
                        "/vertex/person/firstName_lastName_gender/chunk1";
    std::string path2 = test_data_dir + "/ldbc_sample/parquet" +
                        "/vertex/person/firstName_lastName_gender/chunk1";
    arrow::io::IOContext io_context = arrow::io::default_io_context();

    // Open ORC file reader
    auto fs1 = arrow::fs::FileSystemFromUriOrPath(path1).ValueOrDie();
    std::shared_ptr<arrow::io::RandomAccessFile> input1 =
        fs1->OpenInputFile(path1).ValueOrDie();
    std::unique_ptr<arrow::adapters::orc::ORCFileReader> reader =
        arrow::adapters::orc::ORCFileReader::Open(input1, pool).ValueOrDie();

    // Read entire file as a single Arrow table
    auto maybe_table = reader->Read();
    std::shared_ptr<arrow::Table> table1 = maybe_table.ValueOrDie();

    // Open Parquet file reader
    std::unique_ptr<parquet::arrow::FileReader> arrow_reader;
    st = graphar::util::OpenParquetArrowReader(path2, pool, &arrow_reader);

    // Read entire file as a single Arrow table
    auto maybe_table2 = arrow_reader->ReadTable();
    REQUIRE(maybe_table2.ok());
    auto table2 = maybe_table2.ValueOrDie();

    REQUIRE(table1->GetColumnByName("firstName")->ToString() ==
            table2->GetColumnByName("firstName")->ToString());
    REQUIRE(table1->GetColumnByName("lastName")->ToString() ==
            table2->GetColumnByName("lastName")->ToString());
    REQUIRE(table1->GetColumnByName("gender")->ToString() ==
            table2->GetColumnByName("gender")->ToString());
  }
#endif
  SECTION("TestVertexPropertyWriterWithOption") {
    // csv file
    // Construct the writer
    std::string vertex_meta_file_csv =
        test_data_dir + "/ldbc_sample/csv/" + "person.vertex.yml";
    auto vertex_meta_csv = Yaml::LoadFile(vertex_meta_file_csv).value();
    auto vertex_info_csv = VertexInfo::Load(vertex_meta_csv).value();
    auto csv_options = WriterOptions::CSVOptionBuilder();
    auto wopt = csv_options.build();
    csv_options.include_header(true);
    csv_options.delimiter('|');
    auto maybe_writer =
        VertexPropertyWriter::Make(vertex_info_csv, "/tmp/option/", wopt);
    REQUIRE(!maybe_writer.has_error());
    auto writer = maybe_writer.value();
    REQUIRE(writer->WriteTable(table, 0).ok());
    // read csv file
    auto parse_options = arrow::csv::ParseOptions::Defaults();
    parse_options.delimiter = '|';
    std::shared_ptr<arrow::io::InputStream> chunk0_input =
        fs->OpenInputStream(
              "/tmp/option/vertex/person/firstName_lastName_gender/chunk0")
            .ValueOrDie();
    auto read_options = arrow::csv::ReadOptions::Defaults();
    auto csv_reader =
        arrow::csv::TableReader::Make(arrow::io::default_io_context(),
                                      chunk0_input, read_options, parse_options,
                                      arrow::csv::ConvertOptions::Defaults())
            .ValueOrDie();
    auto maybe_table = csv_reader->Read();
    REQUIRE(maybe_table.ok());
    std::shared_ptr<arrow::Table> csv_table = *maybe_table;
    REQUIRE(csv_table->num_rows() == vertex_info_csv->GetChunkSize());
    REQUIRE(csv_table->num_columns() ==
            static_cast<int>(vertex_info_csv->GetPropertyGroup("firstName")
                                 ->GetProperties()
                                 .size()) +
                1);
    // type parquet
    auto options_parquet_Builder = WriterOptions::ParquetOptionBuilder(wopt);
    options_parquet_Builder.compression(arrow::Compression::type::UNCOMPRESSED);
    options_parquet_Builder.enable_statistics(false);
    parquet::SortingColumn sc;
    sc.column_idx = 1;
    std::vector<::parquet::SortingColumn> columns = {sc};
    options_parquet_Builder.sorting_columns(columns)
        .enable_store_decimal_as_integer(true)
        .max_row_group_length(10);
    wopt = options_parquet_Builder.build();
    maybe_writer =
        VertexPropertyWriter::Make(vertex_info_parquet, "/tmp/option/", wopt);
    REQUIRE(!maybe_writer.has_error());
    writer = maybe_writer.value();
    REQUIRE(writer->WriteTable(table, 0).ok());
    // read parquet file
    std::string parquet_file =
        "/tmp/option/vertex/person/firstName_lastName_gender/chunk0";
    std::unique_ptr<parquet::arrow::FileReader> parquet_reader;
    auto st = graphar::util::OpenParquetArrowReader(
        parquet_file, arrow::default_memory_pool(), &parquet_reader);
    REQUIRE(st.ok());
    auto maybe_parquet_table = parquet_reader->ReadTable();
    REQUIRE(maybe_parquet_table.ok());
    auto parquet_table = maybe_parquet_table.ValueOrDie();
    auto parquet_metadata = parquet_reader->parquet_reader()->metadata();
    auto row_group_meta = parquet_metadata->RowGroup(0);
    auto col_meta = row_group_meta->ColumnChunk(0);
    REQUIRE(row_group_meta->sorting_columns().size() == 1);
    REQUIRE(row_group_meta->sorting_columns()[0].column_idx == 1);
    REQUIRE(col_meta->compression() == parquet::Compression::UNCOMPRESSED);
    REQUIRE(!col_meta->statistics());
    REQUIRE(parquet_table->num_rows() == vertex_info_parquet->GetChunkSize());
    REQUIRE(parquet_metadata->num_row_groups() ==
            parquet_table->num_rows() / 10);
    REQUIRE(parquet_table->num_columns() ==
            static_cast<int>(vertex_info_parquet->GetPropertyGroup("firstName")
                                 ->GetProperties()
                                 .size() +
                             1));
#ifdef ARROW_ORC
    std::string vertex_meta_file_orc =
        test_data_dir + "/ldbc_sample/orc/" + "person.vertex.yml";
    auto vertex_meta_orc = Yaml::LoadFile(vertex_meta_file_orc).value();
    auto vertex_info_orc = VertexInfo::Load(vertex_meta_orc).value();
    auto optionsOrcBuilder = WriterOptions::ORCOptionBuilder(wopt);
    optionsOrcBuilder.compression(arrow::Compression::type::ZSTD);
    wopt = optionsOrcBuilder.build();
    maybe_writer =
        VertexPropertyWriter::Make(vertex_info_orc, "/tmp/option/", wopt);
    REQUIRE(!maybe_writer.has_error());
    writer = maybe_writer.value();
    REQUIRE(writer->WriteTable(table, 0).ok());
    auto fs1 = arrow::fs::FileSystemFromUriOrPath(
                   "/tmp/option/vertex/person/firstName_lastName_gender/chunk0")
                   .ValueOrDie();
    std::shared_ptr<arrow::io::RandomAccessFile> input1 =
        fs1->OpenInputFile(
               "/tmp/option/vertex/person/firstName_lastName_gender/chunk0")
            .ValueOrDie();
    arrow::MemoryPool* pool = arrow::default_memory_pool();
    std::unique_ptr<arrow::adapters::orc::ORCFileReader> reader =
        arrow::adapters::orc::ORCFileReader::Open(input1, pool).ValueOrDie();
    // Read entire file as a single Arrow table
    maybe_table = reader->Read();
    std::shared_ptr<arrow::Table> table1 = maybe_table.ValueOrDie();
    REQUIRE(reader->GetCompression() == parquet::Compression::ZSTD);
    REQUIRE(table1->num_rows() == vertex_info_parquet->GetChunkSize());
    REQUIRE(table1->num_columns() ==
            static_cast<int>(vertex_info_parquet->GetPropertyGroup("firstName")
                                 ->GetProperties()
                                 .size()) +
                1);
#endif
  }
}
TEST_CASE_METHOD(GlobalFixture, "TestEdgeChunkWriter") {
  arrow::Status st;
  arrow::MemoryPool* pool = arrow::default_memory_pool();
  std::string path = test_data_dir +
                     "/ldbc_sample/parquet/edge/person_knows_person/"
                     "unordered_by_source/adj_list/part0/chunk0";
  std::unique_ptr<parquet::arrow::FileReader> arrow_reader;
  st = graphar::util::OpenParquetArrowReader(path, pool, &arrow_reader);
  // Read entire file as a single Arrow table
  auto maybe_table = arrow_reader->ReadTable();
  REQUIRE(maybe_table.ok());

  std::shared_ptr<arrow::Table> table =
      maybe_table.ValueOrDie()
          ->RenameColumns(
              {GeneralParams::kSrcIndexCol, GeneralParams::kDstIndexCol})
          .ValueOrDie();
  std::cout << table->schema()->ToString() << std::endl;
  std::cout << table->num_rows() << ' ' << table->num_columns() << std::endl;
  // Construct the writer
  std::string edge_meta_file_csv =
      test_data_dir + "/ldbc_sample/csv/" + "person_knows_person.edge.yml";
  auto edge_meta_csv = Yaml::LoadFile(edge_meta_file_csv).value();
  auto edge_info_csv = EdgeInfo::Load(edge_meta_csv).value();
  auto adj_list_type = AdjListType::ordered_by_source;

  SECTION("TestEdgeChunkWriterWithoutOption") {
    auto maybe_writer =
        EdgeChunkWriter::Make(edge_info_csv, "/tmp/", adj_list_type);
    REQUIRE(!maybe_writer.has_error());
    auto writer = maybe_writer.value();

    // Get & set validate level
    REQUIRE(writer->GetValidateLevel() == ValidateLevel::no_validate);
    writer->SetValidateLevel(ValidateLevel::strong_validate);
    REQUIRE(writer->GetValidateLevel() == ValidateLevel::strong_validate);

    // Valid cases
    // Write adj list of vertex chunk 0 to files
    REQUIRE(writer->SortAndWriteAdjListTable(table, 0, 0).ok());
    // Write number of edges for vertex chunk 0
    REQUIRE(writer->WriteEdgesNum(0, table->num_rows()).ok());
    // Write number of vertices
    REQUIRE(writer->WriteVerticesNum(903).ok());

    auto fs = arrow::fs::FileSystemFromUriOrPath("/tmp/edge/").ValueOrDie();
    // Check the number of edges
    std::shared_ptr<arrow::io::InputStream> input2 =
        fs->OpenInputStream(
              "/tmp/edge/person_knows_person/ordered_by_source/edge_count0")
            .ValueOrDie();
    auto edge_num = input2->Read(sizeof(IdType)).ValueOrDie();
    const IdType* edge_num_ptr =
        reinterpret_cast<const IdType*>(edge_num->data());
    REQUIRE((*edge_num_ptr) == table->num_rows());

    // Check the number of vertices
    std::shared_ptr<arrow::io::InputStream> input3 =
        fs->OpenInputStream(
              "/tmp/edge/person_knows_person/ordered_by_source/vertex_count")
            .ValueOrDie();
    auto vertex_num = input3->Read(sizeof(IdType)).ValueOrDie();
    const auto* vertex_num_ptr =
        reinterpret_cast<const IdType*>(vertex_num->data());
    REQUIRE((*vertex_num_ptr) == 903);

    // Invalid cases
    // Invalid count or index
    REQUIRE(writer->WriteEdgesNum(-1, 0).IsIndexError());
    REQUIRE(writer->WriteEdgesNum(0, -1).IsIndexError());
    REQUIRE(writer->WriteVerticesNum(-1).IsIndexError());
    // Out of range
    REQUIRE(writer->WriteOffsetChunk(table, 0).IsInvalid());
    // Invalid chunk id
    REQUIRE(writer->WriteAdjListChunk(table, -1, 0).IsIndexError());
    REQUIRE(writer->WriteAdjListChunk(table, 0, -1).IsIndexError());
    // Invalid adj list type
    auto invalid_adj_list_type = AdjListType::unordered_by_dest;
    auto maybe_writer2 =
        EdgeChunkWriter::Make(edge_info_csv, "/tmp/", invalid_adj_list_type);
    REQUIRE(maybe_writer2.has_error());
    // Invalid property group
    Property p1("invalid_property", int32(), false);
    auto pg1 = CreatePropertyGroup({p1}, FileType::CSV);
    REQUIRE(writer->WritePropertyChunk(table, pg1, 0, 0).IsKeyError());
    // Property not found in table
    auto pg2 = edge_info_csv->GetPropertyGroup("creationDate");
    REQUIRE(writer->WritePropertyChunk(table, pg2, 0, 0).IsInvalid());
    // Required columns not found
    std::shared_ptr<arrow::Table> tmp_table =
        table->RenameColumns({"creationDate", "tmp_property"}).ValueOrDie();
    REQUIRE(writer->WriteAdjListChunk(tmp_table, 0, 0).IsInvalid());
    // Invalid data type
    REQUIRE(writer->WritePropertyChunk(tmp_table, pg2, 0, 0).IsTypeError());
  }
  SECTION("TestEdgeChunkWriterWithOption") {
    WriterOptions::CSVOptionBuilder csv_options_builder;
    csv_options_builder.include_header(true).delimiter('|');
    auto maybe_writer =
        EdgeChunkWriter::Make(edge_info_csv, "/tmp/option/", adj_list_type,
                              csv_options_builder.build());
    REQUIRE(!maybe_writer.has_error());
    auto writer = maybe_writer.value();

    // Valid: Write adj list with options
    REQUIRE(writer->SortAndWriteAdjListTable(table, 0, 0).ok());
    // Valid: Write edge count
    REQUIRE(writer->WriteEdgesNum(0, table->num_rows()).ok());
    // Valid: Write vertex count
    REQUIRE(writer->WriteVerticesNum(903).ok());

    // Read back CSV file and check delimiter/header
    auto parse_options = arrow::csv::ParseOptions::Defaults();
    parse_options.delimiter = '|';
    auto read_options = arrow::csv::ReadOptions::Defaults();
    auto fs = arrow::fs::FileSystemFromUriOrPath("/tmp/option/").ValueOrDie();
    std::shared_ptr<arrow::io::InputStream> chunk0_input =
        fs->OpenInputStream(
              "/tmp/option/edge/person_knows_person/ordered_by_source/adj_list/"
              "part0/chunk0")
            .ValueOrDie();
    auto csv_reader =
        arrow::csv::TableReader::Make(arrow::io::default_io_context(),
                                      chunk0_input, read_options, parse_options,
                                      arrow::csv::ConvertOptions::Defaults())
            .ValueOrDie();
    auto maybe_table2 = csv_reader->Read();
    REQUIRE(maybe_table2.ok());
    std::shared_ptr<arrow::Table> csv_table = *maybe_table2;
    REQUIRE(csv_table->num_rows() ==
            std::min(edge_info_csv->GetChunkSize(), table->num_rows()));
    REQUIRE(csv_table->num_columns() == table->num_columns());

    // Parquet option
    std::string edge_meta_file_parquet = test_data_dir +
                                         "/ldbc_sample/parquet/" +
                                         "person_knows_person.edge.yml";
    auto edge_meta_parquet = Yaml::LoadFile(edge_meta_file_parquet).value();
    auto edge_info_parquet = EdgeInfo::Load(edge_meta_parquet).value();
    auto optionsBuilderParquet = WriterOptions::ParquetOptionBuilder();
    optionsBuilderParquet.compression(arrow::Compression::type::UNCOMPRESSED);
    optionsBuilderParquet.enable_statistics(false);
    optionsBuilderParquet.enable_store_decimal_as_integer(true);
    optionsBuilderParquet.max_row_group_length(10);
    auto maybe_parquet_writer =
        EdgeChunkWriter::Make(edge_info_parquet, "/tmp/option/", adj_list_type,
                              optionsBuilderParquet.build());
    REQUIRE(!maybe_parquet_writer.has_error());
    auto parquet_writer = maybe_parquet_writer.value();
    REQUIRE(parquet_writer->SortAndWriteAdjListTable(table, 0, 0).ok());
    std::string parquet_file =
        "/tmp/option/edge/person_knows_person/ordered_by_source/adj_list/part0/"
        "chunk0";
    std::unique_ptr<parquet::arrow::FileReader> parquet_reader;
    auto st = graphar::util::OpenParquetArrowReader(
        parquet_file, arrow::default_memory_pool(), &parquet_reader);
    REQUIRE(st.ok());
    auto maybe_parquet_table = parquet_reader->ReadTable();
    REQUIRE(maybe_parquet_table.ok());
    auto parquet_table = maybe_parquet_table.ValueOrDie();
    auto parquet_metadata = parquet_reader->parquet_reader()->metadata();
    auto row_group_meta = parquet_metadata->RowGroup(0);
    auto col_meta = row_group_meta->ColumnChunk(0);
    REQUIRE(col_meta->compression() == parquet::Compression::UNCOMPRESSED);
    REQUIRE(!col_meta->statistics());
    REQUIRE(parquet_table->num_rows() ==
            std::min(table->num_rows(), edge_info_parquet->GetChunkSize()));
    REQUIRE(parquet_metadata->num_row_groups() ==
            parquet_table->num_rows() / 10 + 1);
    REQUIRE(parquet_table->num_columns() ==
            static_cast<int>(table->num_columns()));

#ifdef ARROW_ORC
    // ORC option
    std::string edge_meta_file_orc =
        test_data_dir + "/ldbc_sample/orc/" + "person_knows_person.edge.yml";
    auto edge_meta_orc = Yaml::LoadFile(edge_meta_file_orc).value();
    auto edge_info_orc = EdgeInfo::Load(edge_meta_orc).value();
    auto optionsBuilderOrc = WriterOptions::ORCOptionBuilder();
    optionsBuilderOrc.compression(arrow::Compression::type::ZSTD);
    auto maybe_orc_writer =
        EdgeChunkWriter::Make(edge_info_orc, "/tmp/option/", adj_list_type,
                              optionsBuilderOrc.build());
    REQUIRE(!maybe_orc_writer.has_error());
    auto orc_writer = maybe_orc_writer.value();
    REQUIRE(orc_writer->SortAndWriteAdjListTable(table, 0, 0).ok());
    auto orc_fs = arrow::fs::FileSystemFromUriOrPath(
                      "/tmp/option/edge/person_knows_person/ordered_by_source/"
                      "adj_list/part0/chunk0")
                      .ValueOrDie();
    std::shared_ptr<arrow::io::RandomAccessFile> orc_input =
        orc_fs
            ->OpenInputFile(
                "/tmp/option/edge/person_knows_person/ordered_by_source/"
                "adj_list/part0/chunk0")
            .ValueOrDie();
    arrow::MemoryPool* pool = arrow::default_memory_pool();
    std::unique_ptr<arrow::adapters::orc::ORCFileReader> orc_reader =
        arrow::adapters::orc::ORCFileReader::Open(orc_input, pool).ValueOrDie();
    auto maybe_orc_table = orc_reader->Read();
    REQUIRE(maybe_orc_table.ok());
    std::shared_ptr<arrow::Table> orc_table = *maybe_orc_table;
    REQUIRE(orc_reader->GetCompression() == parquet::Compression::ZSTD);
    REQUIRE(orc_table->num_rows() == table->num_rows());
    REQUIRE(orc_table->num_columns() == table->num_columns());
#endif
  }
}
TEST_CASE_METHOD(GlobalFixture,
                 "TestParquetBloomFilterComparison") {
  std::string path = test_data_dir + "/ldbc_sample/person_0_0.csv";
  arrow::io::IOContext io_context = arrow::io::default_io_context();
  auto fs = arrow::fs::FileSystemFromUriOrPath(path).ValueOrDie();
  std::shared_ptr<arrow::io::InputStream> input =
      fs->OpenInputStream(path).ValueOrDie();

  auto read_options = arrow::csv::ReadOptions::Defaults();
  auto parse_options = arrow::csv::ParseOptions::Defaults();
  parse_options.delimiter = '|';
  auto convert_options = arrow::csv::ConvertOptions::Defaults();

  auto maybe_reader = arrow::csv::TableReader::Make(
      io_context, input, read_options, parse_options, convert_options);
  REQUIRE(maybe_reader.ok());
  std::shared_ptr<arrow::csv::TableReader> reader = *maybe_reader;
  auto maybe_table = reader->Read();
  REQUIRE(maybe_table.ok());
  std::shared_ptr<arrow::Table> table = *maybe_table;

  std::string vertex_meta_file =
      test_data_dir + "/ldbc_sample/parquet/" + "person.vertex.yml";
  auto vertex_meta = Yaml::LoadFile(vertex_meta_file).value();
  auto vertex_info = VertexInfo::Load(vertex_meta).value();

  const std::string base_dir = "/tmp/bloom_test/";
  const std::string no_bloom_dir = base_dir + "no_bloom/";
  const std::string bloom_dir = base_dir + "bloom/";
  const std::string no_bloom_path =
      no_bloom_dir + "vertex/person/firstName_lastName_gender/chunk0";
  const std::string bloom_path =
      bloom_dir + "vertex/person/firstName_lastName_gender/chunk0";

  // ===============================================================
  // 1. Write WITHOUT bloom filter
  // ===============================================================
  {
    auto no_bloom_opts = WriterOptions::ParquetOptionBuilder()
                             .compression(arrow::Compression::ZSTD)
                             .build();
    auto maybe_writer = VertexPropertyWriter::Make(
        vertex_info, no_bloom_dir, no_bloom_opts);
    REQUIRE(!maybe_writer.has_error());
    auto writer = maybe_writer.value();
    REQUIRE(writer->WriteTable(table, 0).ok());
  }

  // ===============================================================
  // 2. Write WITH bloom filter (Arrow 25 auto-folding, GH-50008)
  //    ndv is left as std::nullopt so Arrow auto-sizes each column's
  //    bloom filter based on actual cardinality.
  // ===============================================================
  {
    ::parquet::BloomFilterOptions bf_opts;  // ndv=nullopt, fold=true (default)
    bf_opts.fpp = 0.01;                     // 1% false positive probability
    auto bloom_opts =
        WriterOptions::ParquetOptionBuilder()
            .compression(arrow::Compression::ZSTD)
            .enable_bloom_filter(true, bf_opts)
            .build();
    auto maybe_writer = VertexPropertyWriter::Make(
        vertex_info, bloom_dir, bloom_opts);
    REQUIRE(!maybe_writer.has_error());
    auto writer = maybe_writer.value();
    REQUIRE(writer->WriteTable(table, 0).ok());
  }

  // ===============================================================
  // 3. Read back metadata and compare
  // ===============================================================
  int64_t no_bloom_size = 0, bloom_size = 0;
  bool no_bloom_has_bloom = false;
  int bloom_col_count = 0;
  int64_t total_bloom_overhead = 0;

  // Verify WITHOUT bloom filter
  {
    auto file_info = fs->GetFileInfo(no_bloom_path).ValueOrDie();
    REQUIRE(file_info.type() == arrow::fs::FileType::File);
    no_bloom_size = file_info.size();

    std::unique_ptr<parquet::arrow::FileReader> parquet_reader;
    auto st = graphar::util::OpenParquetArrowReader(
        no_bloom_path, arrow::default_memory_pool(), &parquet_reader);
    REQUIRE(st.ok());
    auto parquet_metadata = parquet_reader->parquet_reader()->metadata();
    auto row_group_meta = parquet_metadata->RowGroup(0);

    for (int c = 0; c < row_group_meta->num_columns(); ++c) {
      auto col_meta = row_group_meta->ColumnChunk(c);
      auto bf_offset = col_meta->bloom_filter_offset();
      if (bf_offset.has_value() && bf_offset.value() > 0) {
        no_bloom_has_bloom = true;
        break;
      }
    }
  }

  // Verify WITH bloom filter
  {
    auto file_info = fs->GetFileInfo(bloom_path).ValueOrDie();
    REQUIRE(file_info.type() == arrow::fs::FileType::File);
    bloom_size = file_info.size();

    std::unique_ptr<parquet::arrow::FileReader> parquet_reader;
    auto st = graphar::util::OpenParquetArrowReader(
        bloom_path, arrow::default_memory_pool(), &parquet_reader);
    REQUIRE(st.ok());
    auto parquet_metadata = parquet_reader->parquet_reader()->metadata();
    auto row_group_meta = parquet_metadata->RowGroup(0);

    for (int c = 0; c < row_group_meta->num_columns(); ++c) {
      auto col_meta = row_group_meta->ColumnChunk(c);
      auto bf_offset = col_meta->bloom_filter_offset();
      if (bf_offset.has_value() && bf_offset.value() > 0) {
        ++bloom_col_count;
        auto bf_length = col_meta->bloom_filter_length();
        if (bf_length.has_value()) {
          total_bloom_overhead += bf_length.value();
        }
      }
    }
  }

  double size_increase_pct =
      static_cast<double>(bloom_size - no_bloom_size) / no_bloom_size * 100.0;

  // Output comparison results
  std::cout << "\n==============================================="
            << std::endl;
  std::cout << "  BLOOM FILTER COMPARISON RESULT" << std::endl;
  std::cout << "==============================================="
            << std::endl;
  std::cout << "  Row count:              " << table->num_rows()
            << std::endl;
  std::cout << "  Column count:           " << table->num_columns()
            << std::endl;
  std::cout << "  File size (no bloom):   " << no_bloom_size
            << " bytes (" << no_bloom_size / 1024.0 << " KB)"
            << std::endl;
  std::cout << "  File size (with bloom): " << bloom_size
            << " bytes (" << bloom_size / 1024.0 << " KB)"
            << std::endl;
  std::cout << "  Size increase:          " << size_increase_pct
            << "%" << std::endl;
  std::cout << "  Bloom overhead:         " << total_bloom_overhead
            << " bytes (" << total_bloom_overhead / 1024.0 << " KB)"
            << std::endl;
  std::cout << "  Columns w/ bloom:       " << bloom_col_count
            << std::endl;
  std::cout << "  No-bloom has bloom:     "
            << (no_bloom_has_bloom ? "YES" : "NO") << std::endl;
  std::cout << "==============================================="
            << std::endl;
  std::cout << "\n  Benefit: Bloom filter enables row-group-level"
            << " predicate pushdown.\n"
            << "  When reading with 'WHERE id = X', the reader "
            << "can skip entire row\n"
            << "  groups without scanning data pages, reducing "
            << "I/O significantly.\n"
            << "  Arrow 25.0.0+ auto-folds ndv to actual column "
            << "cardinality (GH-50008),\n"
            << "  so overhead is proportional to distinct values "
            << "rather than row count."
            << std::endl;
  std::cout << "==============================================="
            << std::endl;

  // ===============================================================
  // 4. Query performance comparison using Dataset Scanner
  //    Generate a larger synthetic table with multiple row groups
  //    so bloom filter can demonstrate row-group skipping.
  // ===============================================================
  {
    // Arrow compute functions must be initialized before using Scanner
    REQUIRE(arrow::compute::Initialize().ok());

    const int64_t num_rows = 50000;
    const int64_t row_group_size = 5000;  // ~10 row groups

    // Build synthetic table: sequential id (0..N-1) + random-ish val
    arrow::Int64Builder id_builder;
    arrow::DoubleBuilder val_builder;
    for (int64_t i = 0; i < num_rows; ++i) {
      REQUIRE(id_builder.Append(i).ok());
      REQUIRE(val_builder.Append(static_cast<double>(i % 1000)).ok());
    }
    auto id_array = id_builder.Finish().ValueOrDie();
    auto val_array = val_builder.Finish().ValueOrDie();
    auto syn_schema = arrow::schema(
        {arrow::field("id", arrow::int64()),
         arrow::field("val", arrow::float64())});
    auto big_table = arrow::Table::Make(syn_schema, {id_array, val_array});

    const std::string query_no_bloom_path = base_dir + "query_no_bloom.parquet";
    const std::string query_bloom_path = base_dir + "query_bloom.parquet";

    // Write WITHOUT bloom filter
    {
      auto opts = WriterOptions::ParquetOptionBuilder()
                      .max_row_group_length(row_group_size)
                      .compression(arrow::Compression::ZSTD)
                      .build();
      auto out = fs->OpenOutputStream(query_no_bloom_path).ValueOrDie();
      auto st = parquet::arrow::WriteTable(
          *big_table, arrow::default_memory_pool(), out, row_group_size,
          opts->getParquetWriterProperties(),
          opts->getArrowWriterProperties());
      REQUIRE(st.ok());
    }

    // Write WITH bloom filter on all columns (auto-folding)
    {
      auto opts = WriterOptions::ParquetOptionBuilder()
                      .max_row_group_length(row_group_size)
                      .compression(arrow::Compression::ZSTD)
                      .enable_bloom_filter()
                      .build();
      auto out = fs->OpenOutputStream(query_bloom_path).ValueOrDie();
      auto st = parquet::arrow::WriteTable(
          *big_table, arrow::default_memory_pool(), out, row_group_size,
          opts->getParquetWriterProperties(big_table->schema()),
          opts->getArrowWriterProperties());
      REQUIRE(st.ok());
    }

    // Verify row group counts are identical
    {
      auto meta_no_bloom = parquet::ReadMetaData(
          fs->OpenInputFile(query_no_bloom_path).ValueOrDie());
      auto meta_bloom = parquet::ReadMetaData(
          fs->OpenInputFile(query_bloom_path).ValueOrDie());
      REQUIRE(meta_no_bloom->num_row_groups() ==
              meta_bloom->num_row_groups());
    }

    // Point query: pick an id in the middle (row group ~9 of 10)
    const int64_t target_id = 42000;

    auto format = std::make_shared<arrow::dataset::ParquetFileFormat>();
    auto filter_expr = arrow::compute::equal(
        arrow::compute::field_ref("id"),
        arrow::compute::literal(arrow::Int64Scalar(target_id)));

    auto timed_scan =
        [&](const std::string& file_path)
        -> std::pair<int64_t, int64_t> {
      auto ds_factory =
          arrow::dataset::FileSystemDatasetFactory::Make(
              fs, {file_path}, format,
              arrow::dataset::FileSystemFactoryOptions())
              .ValueOrDie();
      auto dataset = ds_factory->Finish().ValueOrDie();

      // Warm-up (not timed)
      {
        auto warm_builder = dataset->NewScan().ValueOrDie();
        REQUIRE(warm_builder->Filter(filter_expr).ok());
        auto warm_scanner = warm_builder->Finish().ValueOrDie();
        warm_scanner->ToTable().ValueOrDie();
      }

      // Timed run
      auto start = std::chrono::high_resolution_clock::now();
      auto scan_builder = dataset->NewScan().ValueOrDie();
      REQUIRE(scan_builder->Filter(filter_expr).ok());
      auto scanner = scan_builder->Finish().ValueOrDie();
      auto result = scanner->ToTable().ValueOrDie();
      auto end = std::chrono::high_resolution_clock::now();

      auto elapsed_us =
          std::chrono::duration_cast<std::chrono::microseconds>(end - start)
              .count();
      return {result->num_rows(), elapsed_us};
    };

    auto [rows_no_bloom, time_no_bloom] =
        timed_scan(query_no_bloom_path);
    auto [rows_bloom, time_bloom] =
        timed_scan(query_bloom_path);

    double speedup =
        (time_no_bloom > 0 && time_bloom > 0)
            ? static_cast<double>(time_no_bloom) / time_bloom
            : 0.0;

    // Output query performance comparison
    std::cout << "\n==============================================="
              << std::endl;
    std::cout << "  QUERY PERFORMANCE COMPARISON" << std::endl;
    std::cout << "==============================================="
              << std::endl;
    std::cout << "  Total rows:             " << num_rows << std::endl;
    std::cout << "  Row groups:             "
              << num_rows / row_group_size << std::endl;
    std::cout << "  Filter:                 id == " << target_id
              << std::endl;
    std::cout << "  Rows returned:          " << rows_no_bloom
              << std::endl;
    std::cout << "  Time (no bloom):        " << time_no_bloom
              << " us" << std::endl;
    std::cout << "  Time (with bloom):      " << time_bloom << " us"
              << std::endl;
    if (speedup > 1.0) {
      std::cout << "  Speedup:                " << speedup << "x"
                << std::endl;
    } else {
      std::cout << "  Speedup:                " << speedup << "x"
                << " (dataset too small for bloom benefit)"
                << std::endl;
    }
    std::cout << "==============================================="
              << std::endl;

    REQUIRE(rows_no_bloom == rows_bloom);  // same correctness
    REQUIRE(rows_no_bloom > 0);
  }

  // ===============================================================
  // 5. Query performance with 100 row groups
  //    Larger row group count demonstrates row-group-level skipping
  //    where bloom filter truly shines.
  // ===============================================================
  {
    const int64_t num_rows = 500000;
    const int64_t row_group_size = 5000;  // 100 row groups
    const int64_t target_id = 420000;     // in the last few row groups

    // Build synthetic table: sequential id (0..N-1)
    arrow::Int64Builder id_builder;
    arrow::DoubleBuilder val_builder;
    for (int64_t i = 0; i < num_rows; ++i) {
      REQUIRE(id_builder.Append(i).ok());
      REQUIRE(val_builder.Append(static_cast<double>(i % 1000)).ok());
    }
    auto id_array = id_builder.Finish().ValueOrDie();
    auto val_array = val_builder.Finish().ValueOrDie();
    auto syn_schema = arrow::schema(
        {arrow::field("id", arrow::int64()),
         arrow::field("val", arrow::float64())});
    auto big_table = arrow::Table::Make(syn_schema, {id_array, val_array});

    const std::string large_no_bloom_path =
        base_dir + "large_no_bloom.parquet";
    const std::string large_bloom_path =
        base_dir + "large_bloom.parquet";

    // Write WITHOUT bloom filter
    {
      auto opts = WriterOptions::ParquetOptionBuilder()
                      .max_row_group_length(row_group_size)
                      .compression(arrow::Compression::ZSTD)
                      .build();
      auto out = fs->OpenOutputStream(large_no_bloom_path).ValueOrDie();
      auto st = parquet::arrow::WriteTable(
          *big_table, arrow::default_memory_pool(), out, row_group_size,
          opts->getParquetWriterProperties(),
          opts->getArrowWriterProperties());
      REQUIRE(st.ok());
    }

    // Write WITH bloom filter on all columns (auto-folding)
    {
      auto opts = WriterOptions::ParquetOptionBuilder()
                      .max_row_group_length(row_group_size)
                      .compression(arrow::Compression::ZSTD)
                      .enable_bloom_filter()
                      .build();
      auto out = fs->OpenOutputStream(large_bloom_path).ValueOrDie();
      auto st = parquet::arrow::WriteTable(
          *big_table, arrow::default_memory_pool(), out, row_group_size,
          opts->getParquetWriterProperties(big_table->schema()),
          opts->getArrowWriterProperties());
      REQUIRE(st.ok());
    }

    // Verify row group counts
    {
      auto meta_no_bloom = parquet::ReadMetaData(
          fs->OpenInputFile(large_no_bloom_path).ValueOrDie());
      auto meta_bloom = parquet::ReadMetaData(
          fs->OpenInputFile(large_bloom_path).ValueOrDie());
      REQUIRE(meta_no_bloom->num_row_groups() == 100);
      REQUIRE(meta_bloom->num_row_groups() == 100);
    }

    auto format = std::make_shared<arrow::dataset::ParquetFileFormat>();
    auto filter_expr = arrow::compute::equal(
        arrow::compute::field_ref("id"),
        arrow::compute::literal(arrow::Int64Scalar(target_id)));

    auto timed_scan =
        [&](const std::string& file_path)
        -> std::pair<int64_t, int64_t> {
      auto ds_factory =
          arrow::dataset::FileSystemDatasetFactory::Make(
              fs, {file_path}, format,
              arrow::dataset::FileSystemFactoryOptions())
              .ValueOrDie();
      auto dataset = ds_factory->Finish().ValueOrDie();

      // Warm-up (not timed)
      {
        auto warm_builder = dataset->NewScan().ValueOrDie();
        REQUIRE(warm_builder->Filter(filter_expr).ok());
        auto warm_scanner = warm_builder->Finish().ValueOrDie();
        warm_scanner->ToTable().ValueOrDie();
      }

      // Timed run
      auto start = std::chrono::high_resolution_clock::now();
      auto scan_builder = dataset->NewScan().ValueOrDie();
      REQUIRE(scan_builder->Filter(filter_expr).ok());
      auto scanner = scan_builder->Finish().ValueOrDie();
      auto result = scanner->ToTable().ValueOrDie();
      auto end = std::chrono::high_resolution_clock::now();

      auto elapsed_us =
          std::chrono::duration_cast<std::chrono::microseconds>(end - start)
              .count();
      return {result->num_rows(), elapsed_us};
    };

    auto [rows_no_bloom, time_no_bloom] =
        timed_scan(large_no_bloom_path);
    auto [rows_bloom, time_bloom] =
        timed_scan(large_bloom_path);

    // Also get file sizes for storage overhead report
    int64_t large_no_bloom_size =
        fs->GetFileInfo(large_no_bloom_path).ValueOrDie().size();
    int64_t large_bloom_size =
        fs->GetFileInfo(large_bloom_path).ValueOrDie().size();

    double speedup =
        (time_no_bloom > 0 && time_bloom > 0)
            ? static_cast<double>(time_no_bloom) / time_bloom
            : 0.0;

    std::cout << "\n==============================================="
              << std::endl;
    std::cout << "  QUERY PERFORMANCE COMPARISON (100 Row Groups)"
              << std::endl;
    std::cout << "==============================================="
              << std::endl;
    std::cout << "  Total rows:             " << num_rows << std::endl;
    std::cout << "  Row groups:             100" << std::endl;
    std::cout << "  Filter:                 id == " << target_id
              << std::endl;
    std::cout << "  Rows returned:          " << rows_no_bloom
              << std::endl;
    std::cout << "  File size (no bloom):   " << large_no_bloom_size
              << " bytes (" << large_no_bloom_size / 1024.0 << " KB)"
              << std::endl;
    std::cout << "  File size (with bloom): " << large_bloom_size
              << " bytes (" << large_bloom_size / 1024.0 << " KB)"
              << std::endl;
    std::cout << "  Size increase:          "
              << (static_cast<double>(large_bloom_size -
                                      large_no_bloom_size) /
                  large_no_bloom_size * 100.0)
              << "%" << std::endl;
    std::cout << "  Time (no bloom):        " << time_no_bloom
              << " us" << std::endl;
    std::cout << "  Time (with bloom):      " << time_bloom << " us"
              << std::endl;
    if (speedup > 1.0) {
      std::cout << "  Speedup:                " << speedup << "x"
                << std::endl;
    } else {
      std::cout << "  Speedup:                " << speedup << "x"
                << " (bloom filter not beneficial for this query)"
                << std::endl;
    }
    std::cout << "==============================================="
              << std::endl;

    REQUIRE(rows_no_bloom == rows_bloom);  // same correctness
    REQUIRE(rows_no_bloom > 0);
    // For existing values across many row groups, bloom filter
    // metadata overhead can outweigh data-page skip benefits.
    // The real value shows in the non-existent-value scenario below.
  }

  // ===============================================================
  // 6. Killer scenario: non-existent value lookup with heavy rows
  //    Bloom filter truly shines when each row group is large enough
  //    that skipping its data pages matters. We use wide rows (10
  //    columns), big row groups (100K rows each), and SNAPPY
  //    compression to keep data pages non-trivial in size.
  //    Query for a value that does NOT exist → bloom filter confirms
  //    "definitely absent" in every row group → ALL data pages skipped.
  // ===============================================================
  {
    const int64_t num_rows = 500000;
    const int64_t row_group_size = 100000;   // 5 big row groups
    const int64_t missing_id = 999999;       // not in [0, 499999]

    // Build a wide table (10 columns) to make each row group heavy
    arrow::Int64Builder id_builder;
    arrow::DoubleBuilder c1_builder, c2_builder, c3_builder, c4_builder;
    arrow::StringBuilder c5_builder, c6_builder;
    arrow::Int64Builder c7_builder, c8_builder, c9_builder;
    for (int64_t i = 0; i < num_rows; ++i) {
      REQUIRE(id_builder.Append(i).ok());
      REQUIRE(c1_builder.Append(static_cast<double>(i % 1000)).ok());
      REQUIRE(c2_builder.Append(static_cast<double>((i * 7) % 10000)).ok());
      REQUIRE(c3_builder.Append(static_cast<double>((i * 13) % 500)).ok());
      REQUIRE(c4_builder.Append(static_cast<double>(i * 0.5)).ok());
      REQUIRE(c5_builder.Append("str_" + std::to_string(i % 500)).ok());
      REQUIRE(c6_builder.Append("val_" + std::to_string(i % 200)).ok());
      REQUIRE(c7_builder.Append(i * 3).ok());
      REQUIRE(c8_builder.Append(i % 10000).ok());
      REQUIRE(c9_builder.Append(i / 100).ok());
    }
    auto syn_schema = arrow::schema({
        arrow::field("id", arrow::int64()),
        arrow::field("c1", arrow::float64()),
        arrow::field("c2", arrow::float64()),
        arrow::field("c3", arrow::float64()),
        arrow::field("c4", arrow::float64()),
        arrow::field("c5", arrow::utf8()),
        arrow::field("c6", arrow::utf8()),
        arrow::field("c7", arrow::int64()),
        arrow::field("c8", arrow::int64()),
        arrow::field("c9", arrow::int64()),
    });
    auto big_table = arrow::Table::Make(
        syn_schema,
        {id_builder.Finish().ValueOrDie(), c1_builder.Finish().ValueOrDie(),
         c2_builder.Finish().ValueOrDie(), c3_builder.Finish().ValueOrDie(),
         c4_builder.Finish().ValueOrDie(), c5_builder.Finish().ValueOrDie(),
         c6_builder.Finish().ValueOrDie(), c7_builder.Finish().ValueOrDie(),
         c8_builder.Finish().ValueOrDie(), c9_builder.Finish().ValueOrDie()});

    const std::string kill_no_bloom = base_dir + "kill_no_bloom.parquet";
    const std::string kill_bloom = base_dir + "kill_bloom.parquet";

    // Write WITHOUT bloom filter (UNCOMPRESSED for large data pages)
    {
      auto opts =
          WriterOptions::ParquetOptionBuilder()
              .max_row_group_length(row_group_size)
              .compression(arrow::Compression::UNCOMPRESSED)
              .build();
      auto out = fs->OpenOutputStream(kill_no_bloom).ValueOrDie();
      auto st = parquet::arrow::WriteTable(
          *big_table, arrow::default_memory_pool(), out, row_group_size,
          opts->getParquetWriterProperties(),
          opts->getArrowWriterProperties());
      REQUIRE(st.ok());
    }

    // Write WITH bloom filter (UNCOMPRESSED for large data pages)
    {
      ::parquet::BloomFilterOptions bf_opts;
      auto opts =
          WriterOptions::ParquetOptionBuilder()
              .max_row_group_length(row_group_size)
              .compression(arrow::Compression::UNCOMPRESSED)
              .enable_bloom_filter(true, bf_opts)
              .build();
      auto out = fs->OpenOutputStream(kill_bloom).ValueOrDie();
      auto st = parquet::arrow::WriteTable(
          *big_table, arrow::default_memory_pool(), out, row_group_size,
          opts->getParquetWriterProperties(big_table->schema()),
          opts->getArrowWriterProperties());
      REQUIRE(st.ok());
    }

    // --- Approach A: Dataset Scanner timing (higher-level) ---
    auto format = std::make_shared<arrow::dataset::ParquetFileFormat>();
    auto filter_expr = arrow::compute::equal(
        arrow::compute::field_ref("id"),
        arrow::compute::literal(arrow::Int64Scalar(missing_id)));

    auto timed_dataset_scan =
        [&](const std::string& path) -> std::pair<int64_t, int64_t> {
      auto ds = arrow::dataset::FileSystemDatasetFactory::Make(
                    fs, {path}, format,
                    arrow::dataset::FileSystemFactoryOptions())
                    .ValueOrDie()
                    ->Finish()
                    .ValueOrDie();
      // Warm-up
      {
        auto b = ds->NewScan().ValueOrDie();
        REQUIRE(b->Filter(filter_expr).ok());
        b->Finish().ValueOrDie()->ToTable().ValueOrDie();
      }
      // Timed
      auto start = std::chrono::high_resolution_clock::now();
      auto b = ds->NewScan().ValueOrDie();
      REQUIRE(b->Filter(filter_expr).ok());
      auto rows = b->Finish().ValueOrDie()->ToTable().ValueOrDie()->num_rows();
      auto end = std::chrono::high_resolution_clock::now();
      return {rows, std::chrono::duration_cast<std::chrono::microseconds>(
                        end - start).count()};
    };

    // --- Approach B: Read each row group individually via Result API ---
    // (Arrow 24.0.0+ deprecated the Status version of ReadRowGroup)
    auto scan_row_groups =
        [&](const std::string& path)
        -> std::tuple<int, int64_t, int64_t> {
      auto reader = parquet::arrow::FileReader::Make(
          arrow::default_memory_pool(),
          parquet::ParquetFileReader::OpenFile(path));
      REQUIRE(reader.ok());
      auto& r = *reader.ValueOrDie();
      int num_groups = r.num_row_groups();
      int64_t total_rows_read = 0;

      // Warm-up: read group 0
      r.ReadRowGroup(0).ValueOrDie();

      // Timed: read every row group
      auto start = std::chrono::high_resolution_clock::now();
      for (int rg = 0; rg < num_groups; ++rg) {
        auto tbl = r.ReadRowGroup(rg).ValueOrDie();
        total_rows_read += tbl->num_rows();
      }
      auto end = std::chrono::high_resolution_clock::now();
      return {num_groups, total_rows_read,
              std::chrono::duration_cast<std::chrono::microseconds>(
                  end - start).count()};
    };

    // Meta: check bloom filter availability
    auto check_bloom_meta = [&](const std::string& path) {
      auto meta =
          parquet::ReadMetaData(fs->OpenInputFile(path).ValueOrDie());
      for (int rg = 0; rg < meta->num_row_groups(); ++rg) {
        auto col_meta = meta->RowGroup(rg)->ColumnChunk(0);
        auto bloom_offset = col_meta->bloom_filter_offset();
        // Check if bloom filter is set (non-zero offset or not -1)
        if (bloom_offset.has_value() && bloom_offset.value() > 0) {
          return true;
        }
      }
      return false;
    };

    // File sizes
    int64_t kill_no_bloom_sz =
        fs->GetFileInfo(kill_no_bloom).ValueOrDie().size();
    int64_t kill_bloom_sz =
        fs->GetFileInfo(kill_bloom).ValueOrDie().size();

    // Dataset-level scan
    auto [ds_rows_no, ds_us_no] = timed_dataset_scan(kill_no_bloom);
    auto [ds_rows_bf, ds_us_bf] = timed_dataset_scan(kill_bloom);
    double ds_speedup = (ds_us_no > 0) ? (double)ds_us_no / ds_us_bf : 0;

    // RowGroup-level scan (read every row group individually)
    auto [rg_groups, rg_rows_no, rg_us_no] =
        scan_row_groups(kill_no_bloom);
    auto [rg_groups2, rg_rows_bf, rg_us_bf] =
        scan_row_groups(kill_bloom);
    (void)rg_groups2;

    bool has_bloom = check_bloom_meta(kill_bloom);

    std::cout << "\n==============================================="
              << std::endl;
    std::cout << "  KILLER SCENARIO: Non-Existent Value Lookup"
              << std::endl;
    std::cout << "==============================================="
              << std::endl;
    std::cout << "  Total rows:                " << num_rows
              << std::endl;
    std::cout << "  Row groups:                "
              << num_rows / row_group_size << std::endl;
    std::cout << "  Columns:                   10 (wide rows)"
              << std::endl;
    std::cout << "  Filter:                    id == " << missing_id
              << std::endl;
    std::cout << "  Bloom filter on id column: "
              << (has_bloom ? "YES" : "NO") << std::endl;
    std::cout << "-----------------------------------------------"
              << std::endl;
    std::cout << "  Dataset Scanner:" << std::endl;
    std::cout << "    Rows returned:           " << ds_rows_no
              << std::endl;
    std::cout << "    Time (no bloom):         " << ds_us_no
              << " us" << std::endl;
    std::cout << "    Time (with bloom):       " << ds_us_bf
              << " us" << std::endl;
    std::cout << "    Speedup:                 " << ds_speedup
              << "x" << std::endl;
    std::cout << "-----------------------------------------------"
              << std::endl;
    std::cout << "  RowGroup-level Scan (all groups):" << std::endl;
    std::cout << "    Row groups:               " << rg_groups
              << std::endl;
    std::cout << "    Rows read (no bloom):     " << rg_rows_no
              << std::endl;
    std::cout << "    Rows read (with bloom):   " << rg_rows_bf
              << std::endl;
    std::cout << "    Time (no bloom):          " << rg_us_no
              << " us" << std::endl;
    std::cout << "    Time (with bloom):        " << rg_us_bf
              << " us" << std::endl;
    if (rg_us_no > 0) {
      std::cout << "    Speedup:                  "
                << (double)rg_us_no / rg_us_bf << "x" << std::endl;
    }
    std::cout << "-----------------------------------------------"
              << std::endl;
    std::cout << "  File size (no bloom):      " << kill_no_bloom_sz
              << " bytes (" << kill_no_bloom_sz / 1024.0 << " KB)"
              << std::endl;
    std::cout << "  File size (with bloom):    " << kill_bloom_sz
              << " bytes (" << kill_bloom_sz / 1024.0 << " KB)"
              << std::endl;
    std::cout << "  Size increase:             "
              << (double)(kill_bloom_sz - kill_no_bloom_sz) /
                     kill_no_bloom_sz * 100.0
              << "%" << std::endl;
    std::cout << "==============================================="
              << std::endl;

    REQUIRE(ds_rows_no == 0);       // id not found
    REQUIRE(ds_rows_bf == 0);       // consistent
    REQUIRE(has_bloom);             // bloom filter is present
  }

  // Assertions
  REQUIRE(no_bloom_size > 0);
  REQUIRE(bloom_size > 0);
  REQUIRE(!no_bloom_has_bloom);
  REQUIRE(bloom_col_count > 0);
  REQUIRE(bloom_size >= no_bloom_size);
}
}  // namespace graphar
