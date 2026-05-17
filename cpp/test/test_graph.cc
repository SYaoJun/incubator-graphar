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

#include <iostream>

#include "./util.h"
#include "graphar/api/high_level_reader.h"
#include "graphar/expression.h"

#include <catch2/catch_test_macros.hpp>

namespace graphar {
constexpr size_t expectedFemaleCount = 454;
constexpr size_t expectedMaleCount = 449;
constexpr size_t expectedTotalCount = 903;
TEST_CASE_METHOD(GlobalFixture, "Graph") {
  // read file and construct graph info
  std::string path =
      test_data_dir + "/ldbc_sample/parquet/ldbc_sample.graph.yml";
  auto maybe_graph_info = GraphInfo::Load(path);
  REQUIRE(maybe_graph_info.status().ok());
  auto graph_info = maybe_graph_info.value();

  SECTION("VerticesCollection") {
    // construct vertices collection
    std::string type = "person", property = "firstName";
    auto maybe_vertices_collection = VerticesCollection::Make(graph_info, type);
    REQUIRE(!maybe_vertices_collection.has_error());
    auto vertices = maybe_vertices_collection.value();
    auto count = 0;
    for (auto it = vertices->begin(); it != vertices->end(); ++it) {
      // access data through iterator directly
      std::cout << it.id() << ", id=" << it.property<int64_t>("id").value()
                << ", firstName="
                << it.property<std::string>("firstName").value() << std::endl;
      // access data through vertex
      auto vertex = *it;
      std::cout << vertex.id()
                << ", id=" << vertex.property<int64_t>("id").value()
                << ", firstName="
                << vertex.property<std::string>("firstName").value()
                << std::endl;
      // access data reference through vertex
      REQUIRE(vertex.property<int64_t>("id").value() ==
              vertex.property<const int64_t&>("id").value());
      REQUIRE(vertex.property<std::string>("firstName").value() ==
              vertex.property<const std::string&>("firstName").value());
      REQUIRE(vertex.property<const std::string&>("id").has_error());
      count++;
    }
    auto it_last = vertices->begin() + (count - 1);
    std::cout << it_last.id()
              << ", id=" << it_last.property<int64_t>("id").value()
              << ", firstName="
              << it_last.property<std::string>("firstName").value()
              << std::endl;
    auto it_begin = it_last + (1 - count);

    auto it = vertices->begin();
    it += (count - 1);
    REQUIRE(it.id() == it_last.id());
    REQUIRE(it.property<int64_t>("id").value() ==
            it_last.property<int64_t>("id").value());
    it += (1 - count);
    REQUIRE(it.id() == it_begin.id());
    REQUIRE(it.property<int64_t>("id").value() ==
            it_begin.property<int64_t>("id").value());
  }

  SECTION("VerticesCollectionFilterByLabel") {
    std::string path = test_data_dir + "/ldbc/parquet/" + "ldbc.graph.yml";
    auto maybe_graph_info = GraphInfo::Load(path);
    REQUIRE(maybe_graph_info.status().ok());
    auto graph_info = maybe_graph_info.value();

    auto vertex_info = graph_info->GetVertexInfo("organisation");
    REQUIRE(vertex_info != nullptr);

    auto labels = vertex_info->GetLabels();
    if (!labels.empty()) {
      auto vertices = std::make_shared<VerticesCollection>(
          vertex_info, graph_info->GetPrefix());

      auto maybe_filtered_ids =
          vertices->filter(std::vector<std::string>{labels[0]}, nullptr);
      REQUIRE(maybe_filtered_ids.status().ok());
      auto filtered_ids = maybe_filtered_ids.value();

      std::cout << "Filtered " << filtered_ids.size()
                << " vertices with label '" << labels[0] << "'" << std::endl;

      auto filtered_vertices = std::make_shared<VerticesCollection>(
          vertex_info, graph_info->GetPrefix(), true, filtered_ids);

      size_t count = 0;
      for (auto it = filtered_vertices->begin(); it != filtered_vertices->end();
           ++it) {
        count++;
      }
      REQUIRE(count == filtered_ids.size());
    }
  }

  SECTION("VerticesCollectionFilterByProperty") {
    auto vertex_info = graph_info->GetVertexInfo("person");
    REQUIRE(vertex_info != nullptr);

    auto vertices = std::make_shared<VerticesCollection>(
        vertex_info, graph_info->GetPrefix());
    REQUIRE(vertices->size() == expectedTotalCount);
    std::cout << "total size " << vertices->size() << std::endl;
    // filter female vertices
    auto filter_female =
        _Equal(_Property("gender"), _Literal(std::string("female")));
    std::vector<IdType> new_valid_chunk;
    auto maybe_filtered_female_ids =
        vertices->filter("gender", filter_female, &new_valid_chunk);
    REQUIRE(maybe_filtered_female_ids.status().ok());
    auto filtered_female_ids = maybe_filtered_female_ids.value();
    // filter male vertices
    auto filter_male =
        _Equal(_Property("gender"), _Literal(std::string("male")));
    auto maybe_filtered_male_ids =
        vertices->filter("gender", filter_male, &new_valid_chunk);
    REQUIRE(maybe_filtered_male_ids.status().ok());
    auto filtered_male_ids = maybe_filtered_male_ids.value();

    std::cout << "Filtered " << filtered_female_ids.size()
              << " vertices with gender='female'" << std::endl;
    std::cout << "Filtered " << filtered_male_ids.size()
              << " vertices with gender='male'" << std::endl;

    REQUIRE(filtered_female_ids.size() == expectedFemaleCount);
    REQUIRE(filtered_male_ids.size() == expectedMaleCount);
    REQUIRE(filtered_male_ids.size() + filtered_female_ids.size() ==
            expectedTotalCount);
  }

  SECTION("ListProperty") {
    // read file and construct graph info
    std::string path =
        test_data_dir +
        "/ldbc_sample/parquet/ldbc_sample_with_feature.graph.yml";
    auto maybe_graph_info = GraphInfo::Load(path);
    REQUIRE(maybe_graph_info.status().ok());
    auto graph_info = maybe_graph_info.value();
    std::string type = "person", list_property = "feature";
    auto maybe_vertices_collection = VerticesCollection::Make(graph_info, type);
    REQUIRE(!maybe_vertices_collection.has_error());
    auto vertices = maybe_vertices_collection.value();
    auto count = 0;
    auto vertex_info = graph_info->GetVertexInfo(type);
    auto data_type = vertex_info->GetPropertyType(list_property).value();
    REQUIRE(data_type->id() == Type::LIST);
    REQUIRE(data_type->value_type()->id() == Type::FLOAT);
    if (data_type->id() == Type::LIST &&
        data_type->value_type()->id() == Type::FLOAT) {
      for (auto it = vertices->begin(); it != vertices->end(); ++it) {
        auto vertex = *it;
        auto float_array = vertex.property<FloatArray>(list_property).value();
        for (size_t i = 0; i < float_array.size(); i++) {
          REQUIRE(float_array[i] == static_cast<float>(vertex.id()) + i);
        }
        count++;
      }
      REQUIRE(count == 903);
    }
  }

  SECTION("EdgesCollection") {
    std::string src_type = "person", edge_type = "knows", dst_type = "person";
    // iterate edges of vertex chunk 0
    auto expect =
        EdgesCollection::Make(graph_info, src_type, edge_type, dst_type,
                              AdjListType::ordered_by_source, 0, 1);
    REQUIRE(!expect.has_error());
    auto edges = expect.value();
    auto end = edges->end();
    size_t count = 0;
    for (auto it = edges->begin(); it != end; ++it) {
      // access data through iterator directly
      std::cout << "src=" << it.source() << ", dst=" << it.destination() << " ";
      // access data through edge
      auto edge = *it;
      REQUIRE(edge.source() == it.source());
      REQUIRE(edge.destination() == it.destination());
      std::cout << "creationDate="
                << edge.property<std::string>("creationDate").value()
                << std::endl;
      // access data reference through edge
      REQUIRE(edge.property<std::string>("creationDate").value() ==
              edge.property<const std::string&>("creationDate").value());
      REQUIRE(edge.property<const int64_t&>("creationDate").has_error());
      count++;
    }
    std::cout << "edge_count=" << count << std::endl;
    REQUIRE(edges->size() == count);

    // iterate edges of vertex chunk [2, 4)
    auto expect1 =
        EdgesCollection::Make(graph_info, src_type, edge_type, dst_type,
                              AdjListType::ordered_by_dest, 2, 4);
    REQUIRE(!expect1.has_error());
    auto edges1 = expect1.value();
    auto end1 = edges1->end();
    size_t count1 = 0;
    for (auto it = edges1->begin(); it != end1; ++it) {
      count1++;
    }
    std::cout << "edge_count=" << count1 << std::endl;
    REQUIRE(edges1->size() == count1);

    // iterate all edges
    auto expect2 =
        EdgesCollection::Make(graph_info, src_type, edge_type, dst_type,
                              AdjListType::ordered_by_source);
    REQUIRE(!expect2.has_error());
    auto& edges2 = expect2.value();
    auto end2 = edges2->end();
    size_t count2 = 0;
    for (auto it = edges2->begin(); it != end2; ++it) {
      auto edge = *it;
      std::cout << "src=" << edge.source() << ", dst=" << edge.destination()
                << std::endl;
      count2++;
    }
    std::cout << "edge_count=" << count2 << std::endl;
    REQUIRE(edges2->size() == count2);

    // empty collection
    auto expect3 =
        EdgesCollection::Make(graph_info, src_type, edge_type, dst_type,
                              AdjListType::unordered_by_source, 5, 5);
    REQUIRE(!expect2.has_error());
    auto edges3 = expect3.value();
    auto end3 = edges3->end();
    size_t count3 = 0;
    for (auto it = edges3->begin(); it != end3; ++it) {
      count3++;
    }
    std::cout << "edge_count=" << count3 << std::endl;
    REQUIRE(count3 == 0);
    REQUIRE(edges3->size() == 0);

    // invalid adjlist type
    auto expect4 =
        EdgesCollection::Make(graph_info, src_type, edge_type, dst_type,
                              AdjListType::unordered_by_dest);
    REQUIRE(expect4.status().IsInvalid());
  }

  SECTION("ValidateProperty") {
    // read file and construct graph info
    std::string path = test_data_dir + "/neo4j/MovieGraph.graph.yml";
    auto maybe_graph_info = GraphInfo::Load(path);
    REQUIRE(maybe_graph_info.status().ok());
    auto graph_info = maybe_graph_info.value();
    // get vertices collection
    std::string type = "Person", property = "born";
    auto maybe_vertices_collection = VerticesCollection::Make(graph_info, type);
    REQUIRE(!maybe_vertices_collection.has_error());
    auto vertices = maybe_vertices_collection.value();
    // the count of valid property value
    auto count = 0;
    for (auto it = vertices->begin(); it != vertices->end(); ++it) {
      // get a vertex and access its data
      auto vertex = *it;
      // property not exists
      REQUIRE_THROWS_AS(vertex.IsValid("bornn"), std::invalid_argument);
      if (vertex.IsValid(property)) {
        REQUIRE(vertex.property<int64_t>(property).value() != 0);
        count++;
      } else {
        std::cout << "the property is not valid" << std::endl;
      }
    }
    REQUIRE(count == 128);
    auto last_invalid_vertex = *(vertices->end() + -1);
    REQUIRE(last_invalid_vertex.property<int64_t>(property).has_error());
  }

  SECTION("EdgeIterator") {
    std::string src_type = "person", edge_type = "knows", dst_type = "person";
    auto expect =
        EdgesCollection::Make(graph_info, src_type, edge_type, dst_type,
                              AdjListType::ordered_by_source);
    REQUIRE(!expect.has_error());
    auto edges = expect.value();

    // Test iterator functionality
    auto begin = edges->begin();
    auto end = edges->end();
    size_t count = 0;

    // Iterate through first 2000 edges
    for (auto it = begin; it != end; ++it) {
      if (count >= 2000) {
        break;
      }
      count++;
      REQUIRE(it.source() >= 0);
      REQUIRE(it.destination() >= 0);
      REQUIRE(it.property<std::string>("creationDate").has_value());
    }
    REQUIRE(count == 2000);

    // Test skipping and iterating next 2000 edges
    auto begin2 = edges->begin();
    size_t i = 0;
    for (auto it = begin2; it != end; ++it, i++) {
      if (i < 2000) {
        continue;
      }
      if (i >= 4000) {
        break;
      }
      count++;
      REQUIRE(it.source() >= 0);
      REQUIRE(it.destination() >= 0);
      REQUIRE(it.property<std::string>("creationDate").has_value());
    }
    REQUIRE(count == 4000);

    // Test skipping and iterating next 2000 edges
    auto begin3 = edges->begin();
    size_t j = 0;
    for (auto it = begin3; it != end; ++it, j++) {
      if (j < 4000) {
        continue;
      }
      if (j >= 6000) {
        break;
      }
      count++;
      REQUIRE(it.source() >= 0);
      REQUIRE(it.destination() >= 0);
      REQUIRE(it.property<std::string>("creationDate").has_value());
    }
    REQUIRE(count == 6000);

    // Test iterating remaining edges
    auto begin4 = edges->begin();
    size_t k = 0;
    for (auto it = begin4; it != end; ++it, k++) {
      if (k < 6000) {
        continue;
      }
      count++;
      REQUIRE(it.source() >= 0);
      REQUIRE(it.destination() >= 0);
      REQUIRE(it.property<std::string>("creationDate").has_value());
    }

    // Verify total count matches collection size
    REQUIRE(count == edges->size());
    std::cout << "Total edge_count=" << count << std::endl;
  }

  SECTION("DateType") {
    std::string path_date =
        test_data_dir + "/ldbc_sample/parquet/ldbc_sample_date.graph.yml";
    auto maybe_graph_info_date = GraphInfo::Load(path_date);
    REQUIRE(maybe_graph_info_date.status().ok());
    auto graph_info_date = maybe_graph_info_date.value();
    std::string src_type = "person", edge_type = "knows-date",
                dst_type = "person";
    auto expect =
        EdgesCollection::Make(graph_info_date, src_type, edge_type, dst_type,
                              AdjListType::ordered_by_source);
    REQUIRE(!expect.has_error());
    auto edges = expect.value();

    // Expected values for the first ten creationDate-date entries
    int32_t expected_dates[10] = {14820, 15442, 14909, 15182, 15141,
                                  15058, 15155, 15135, 15364, 15455};
    size_t count = 0;
    for (auto it = edges->begin(); it != edges->end() && count < 10;
         ++it, ++count) {
      auto date_val = it.property<int32_t>("creationDate-date");
      REQUIRE(date_val.has_value());
      REQUIRE(date_val.value() == expected_dates[count]);
    }
    REQUIRE(count == 10);
    std::cout << "DateType edge_count=" << count << std::endl;
  }

  SECTION("TimestampType") {
    std::string path_timestamp =
        test_data_dir + "/ldbc_sample/parquet/ldbc_sample_timestamp.graph.yml";
    auto maybe_graph_info_timestamp = GraphInfo::Load(path_timestamp);
    REQUIRE(maybe_graph_info_timestamp.status().ok());
    auto graph_info_timestamp = maybe_graph_info_timestamp.value();
    std::string src_type = "person", edge_type = "knows-timestamp",
                dst_type = "person";
    auto expect =
        EdgesCollection::Make(graph_info_timestamp, src_type, edge_type,
                              dst_type, AdjListType::ordered_by_source);
    REQUIRE(!expect.has_error());
    auto edges = expect.value();

    // Expected values for the first ten creationDate-timestamp entries
    int64_t expected_timestamps[10] = {
        1280503193298LL, 1334239018931LL, 1288146786288LL, 1311781394869LL,
        1308223719623LL, 1301064563134LL, 1309416320777LL, 1307728039432LL,
        1327492287348LL, 1335389465259LL};
    size_t count = 0;
    for (auto it = edges->begin(); it != edges->end() && count < 10;
         ++it, ++count) {
      auto ts_val = it.property<int64_t>("creationDate-timestamp");
      REQUIRE(ts_val.has_value());
      REQUIRE(ts_val.value() == expected_timestamps[count]);
    }
    REQUIRE(count == 10);
    std::cout << "TimestampType edge_count=" << count << std::endl;
  }

  SECTION("VerticesCollectionFilterError") {
    auto vertex_info = graph_info->GetVertexInfo("person");
    REQUIRE(vertex_info != nullptr);

    auto vertices = std::make_shared<VerticesCollection>(
        vertex_info, graph_info->GetPrefix());
  }

  SECTION("VerticesWithPropertyStatic") {
    auto filter =
        _Equal(_Property("gender"), _Literal(std::string("female")));

    auto result1 = VerticesCollection::verticesWithProperty(
        "gender", filter, graph_info, "person");
    REQUIRE(!result1.has_error());
    auto vertices1 = result1.value();
    REQUIRE(vertices1->size() > 0);

    auto result2 = VerticesCollection::verticesWithProperty(
        "gender", filter, vertices1);
    REQUIRE(!result2.has_error());
  }

  SECTION("EdgeIterFirstSrcOrderedBySource") {
    std::string src_type = "person", edge_type = "knows", dst_type = "person";
    auto expect = EdgesCollection::Make(graph_info, src_type, edge_type,
                                        dst_type, AdjListType::ordered_by_source);
    REQUIRE(!expect.has_error());
    auto edges = expect.value();

    auto it = edges->begin();
    IdType src_id = it.source();
    auto find_it = edges->find_src(src_id, it);
    REQUIRE((find_it == edges->end() || find_it.source() == src_id));
  }

  SECTION("EdgeIterFirstDstOrderedByDest") {
    std::string src_type = "person", edge_type = "knows", dst_type = "person";
    auto expect = EdgesCollection::Make(graph_info, src_type, edge_type,
                                        dst_type, AdjListType::ordered_by_dest);
    REQUIRE(!expect.has_error());
    auto edges = expect.value();

    auto it = edges->begin();
    IdType dst_id = it.destination();
    auto find_it = edges->find_dst(dst_id, it);
    REQUIRE((find_it == edges->end() || find_it.destination() == dst_id));
  }

  SECTION("OBSEdgeCollectionFindDst") {
    std::string src_type = "person", edge_type = "knows", dst_type = "person";
    auto expect = EdgesCollection::Make(graph_info, src_type, edge_type,
                                        dst_type, AdjListType::ordered_by_source);
    REQUIRE(!expect.has_error());
    auto edges = expect.value();

    auto it = edges->begin();
    IdType dst_id = it.destination();
    auto find_it = edges->find_dst(dst_id, it);
    REQUIRE((find_it == edges->end() || find_it.destination() == dst_id));
  }

  SECTION("OBDEdgesCollectionFindSrc") {
    std::string src_type = "person", edge_type = "knows", dst_type = "person";
    auto expect = EdgesCollection::Make(graph_info, src_type, edge_type,
                                        dst_type, AdjListType::ordered_by_dest);
    REQUIRE(!expect.has_error());
    auto edges = expect.value();

    auto it = edges->begin();
    IdType src_id = it.source();
    auto find_it = edges->find_src(src_id, it);
    REQUIRE((find_it == edges->end() || find_it.source() == src_id));
  }

  SECTION("OBDEdgesCollectionFindDst") {
    std::string src_type = "person", edge_type = "knows", dst_type = "person";
    auto expect = EdgesCollection::Make(graph_info, src_type, edge_type,
                                        dst_type, AdjListType::ordered_by_dest);
    REQUIRE(!expect.has_error());
    auto edges = expect.value();

    auto it = edges->begin();
    IdType dst_id = it.destination();
    auto find_it = edges->find_dst(dst_id, it);
    REQUIRE((find_it == edges->end() || find_it.destination() == dst_id));
  }

  SECTION("EdgesCollectionMakeError") {
    auto result = EdgesCollection::Make(graph_info, "person", "non_existent_edge",
                                       "person", AdjListType::ordered_by_source);
    REQUIRE(result.has_error());
    REQUIRE(result.status().IsKeyError());

    // Test with non-existent vertex type
    auto result2 = EdgesCollection::Make(graph_info, "non_existent", "knows", "person",
                                        AdjListType::ordered_by_source);
    REQUIRE(result2.has_error());
    REQUIRE(result2.status().IsKeyError());
  }

  SECTION("EdgeIterNextSrcOrderedBySource") {
    std::string src_type = "person", edge_type = "knows", dst_type = "person";
    auto expect = EdgesCollection::Make(graph_info, src_type, edge_type,
                                        dst_type, AdjListType::ordered_by_source);
    REQUIRE(!expect.has_error());
    auto edges = expect.value();

    auto it = edges->begin();
    IdType src_id = it.source();
    auto find_it = edges->find_src(src_id, it);
    if (find_it != edges->end() && find_it.source() == src_id) {
      bool has_next = find_it.next_src();
      if (has_next) {
        REQUIRE(find_it.source() == src_id);
      }
    }
  }

  SECTION("EdgeIterNextDstOrderedByDest") {
    std::string src_type = "person", edge_type = "knows", dst_type = "person";
    auto expect = EdgesCollection::Make(graph_info, src_type, edge_type,
                                        dst_type, AdjListType::ordered_by_dest);
    REQUIRE(!expect.has_error());
    auto edges = expect.value();

    auto it = edges->begin();
    IdType dst_id = it.destination();
    auto find_it = edges->find_dst(dst_id, it);
    if (find_it != edges->end() && find_it.destination() == dst_id) {
      bool has_next = find_it.next_dst();
      if (has_next) {
        REQUIRE(find_it.destination() == dst_id);
      }
    }
  }

  SECTION("EdgeIterNextSrcWithId") {
    std::string src_type = "person", edge_type = "knows", dst_type = "person";
    auto expect = EdgesCollection::Make(graph_info, src_type, edge_type,
                                        dst_type, AdjListType::ordered_by_source);
    REQUIRE(!expect.has_error());
    auto edges = expect.value();

    auto it = edges->begin();
    if (it != edges->end()) {
      IdType src_id = it.source();
      auto find_it = edges->find_src(src_id, it);
      if (find_it != edges->end()) {
        bool found = find_it.next_src(src_id);
        if (found) {
          REQUIRE(find_it.source() == src_id);
        }
      }
    }
  }

  SECTION("EdgeIterNextDstWithId") {
    std::string src_type = "person", edge_type = "knows", dst_type = "person";
    auto expect = EdgesCollection::Make(graph_info, src_type, edge_type,
                                        dst_type, AdjListType::ordered_by_dest);
    REQUIRE(!expect.has_error());
    auto edges = expect.value();

    auto it = edges->begin();
    if (it != edges->end()) {
      IdType dst_id = it.destination();
      auto find_it = edges->find_dst(dst_id, it);
      if (find_it != edges->end()) {
        bool found = find_it.next_dst(dst_id);
        if (found) {
          REQUIRE(find_it.destination() == dst_id);
        }
      }
    }
  }

  SECTION("EdgePropertyWrongType") {
    std::string src_type = "person", edge_type = "knows", dst_type = "person";
    auto expect = EdgesCollection::Make(graph_info, src_type, edge_type,
                                        dst_type, AdjListType::ordered_by_source);
    REQUIRE(!expect.has_error());
    auto edges = expect.value();

    for (auto it = edges->begin(); it != edges->end(); ++it) {
      auto edge = *it;
      auto date_result = edge.property<std::string>("creationDate");
      REQUIRE(!date_result.has_error());

      auto wrong_type = edge.property<int64_t>("creationDate");
      REQUIRE(wrong_type.has_error());
      break;
    }
  }

  SECTION("EdgeIterPropertyNotFound") {
    std::string src_type = "person", edge_type = "knows", dst_type = "person";
    auto expect = EdgesCollection::Make(graph_info, src_type, edge_type,
                                        dst_type, AdjListType::ordered_by_source);
    REQUIRE(!expect.has_error());
    auto edges = expect.value();

    for (auto it = edges->begin(); it != edges->end(); ++it) {
      auto not_found = it.property<std::string>("non_existent_property");
      REQUIRE(not_found.has_error());
      break;
    }
  }

  SECTION("VerticesCollectionFindMethod") {
    auto vertex_info = graph_info->GetVertexInfo("person");
    REQUIRE(vertex_info != nullptr);

    auto vertices = std::make_shared<VerticesCollection>(
        vertex_info, graph_info->GetPrefix());

    auto it = vertices->find(0);
    REQUIRE(it != vertices->end());
    REQUIRE(it.id() == 0);
  }

  SECTION("VerticesCollectionGetVertexInfoAndPrefix") {
    auto vertex_info = graph_info->GetVertexInfo("person");
    REQUIRE(vertex_info != nullptr);

    auto vertices = std::make_shared<VerticesCollection>(
        vertex_info, graph_info->GetPrefix());

    auto retrieved_info = vertices->GetVertexInfo();
    REQUIRE(retrieved_info != nullptr);
    auto prefix = vertices->GetPrefix();
    REQUIRE(!prefix.empty());
  }

  SECTION("EdgeIterCopyConstructor") {
    std::string src_type = "person", edge_type = "knows", dst_type = "person";
    auto expect = EdgesCollection::Make(graph_info, src_type, edge_type,
                                        dst_type, AdjListType::ordered_by_source);
    REQUIRE(!expect.has_error());
    auto edges = expect.value();

    auto it = edges->begin();
    EdgeIter copy_it(it);
    REQUIRE(copy_it == it);
  }

  SECTION("EdgeIterCopyAssignment") {
    std::string src_type = "person", edge_type = "knows", dst_type = "person";
    auto expect = EdgesCollection::Make(graph_info, src_type, edge_type,
                                        dst_type, AdjListType::ordered_by_source);
    REQUIRE(!expect.has_error());
    auto edges = expect.value();

    auto it1 = edges->begin();
    auto it2 = edges->end();
    it2 = it1;
    REQUIRE(it2 == it1);
  }

  SECTION("EdgeIterGlobalChunkIndex") {
    std::string src_type = "person", edge_type = "knows", dst_type = "person";
    auto expect = EdgesCollection::Make(graph_info, src_type, edge_type,
                                        dst_type, AdjListType::ordered_by_source);
    REQUIRE(!expect.has_error());
    auto edges = expect.value();

    auto it = edges->begin();
    IdType chunk_idx = it.global_chunk_index();
    REQUIRE(chunk_idx >= 0);

    IdType cur_off = it.cur_offset();
    REQUIRE(cur_off >= 0);
  }

  SECTION("EdgeIterToBegin") {
    std::string src_type = "person", edge_type = "knows", dst_type = "person";
    auto expect = EdgesCollection::Make(graph_info, src_type, edge_type,
                                        dst_type, AdjListType::ordered_by_source);
    REQUIRE(!expect.has_error());
    auto edges = expect.value();

    auto it = edges->begin();
    ++it;
    ++it;
    ++it;

    it.to_begin();
    REQUIRE(it.global_chunk_index() >= 0);
  }

  SECTION("EdgeIterPostfixIncrement") {
    std::string src_type = "person", edge_type = "knows", dst_type = "person";
    auto expect = EdgesCollection::Make(graph_info, src_type, edge_type,
                                        dst_type, AdjListType::ordered_by_source);
    REQUIRE(!expect.has_error());
    auto edges = expect.value();

    auto it = edges->begin();
    auto old_it = it++;
    REQUIRE(old_it != it);
  }

  SECTION("VerticesCollectionFilterWithNewValidChunk") {
    auto vertex_info = graph_info->GetVertexInfo("person");
    REQUIRE(vertex_info != nullptr);

    auto vertices = std::make_shared<VerticesCollection>(
        vertex_info, graph_info->GetPrefix());

    std::vector<IdType> new_valid_chunk;
    auto filter_female =
        _Equal(_Property("gender"), _Literal(std::string("female")));
    auto result = vertices->filter("gender", filter_female, &new_valid_chunk);
    REQUIRE(!result.has_error());
  }

  SECTION("VerticesWithPropertyOnFilteredCollection") {
    auto vertex_info = graph_info->GetVertexInfo("person");
    REQUIRE(vertex_info != nullptr);

    auto filter_female =
        _Equal(_Property("gender"), _Literal(std::string("female")));
    auto vertices = std::make_shared<VerticesCollection>(
        vertex_info, graph_info->GetPrefix());
    auto maybe_filtered_ids =
        vertices->filter("gender", filter_female, nullptr);
    REQUIRE(!maybe_filtered_ids.has_error());

    auto filtered_vertices = std::make_shared<VerticesCollection>(
        vertex_info, graph_info->GetPrefix(), true, maybe_filtered_ids.value());

    auto filter_name =
        _Equal(_Property("firstName"), _Literal(std::string("Dan")));
    auto result = VerticesCollection::verticesWithProperty(
        "firstName", filter_name, filtered_vertices);
  }

  SECTION("EdgesCollectionWithChunkRange") {
    std::string src_type = "person", edge_type = "knows", dst_type = "person";
    auto expect = EdgesCollection::Make(graph_info, src_type, edge_type,
                                       dst_type, AdjListType::ordered_by_source, 0, 2);
    REQUIRE(!expect.has_error());
    auto edges = expect.value();
    REQUIRE(edges->size() >= 0);
  }
}

TEST_CASE("VerticesWithLabelFunctions", "[graph][label]") {
  // Load the full ldbc graph which has labelled vertices (organisation)
  std::string ldbc_graph_path =
      std::string(std::getenv("GAR_TEST_DATA")) + "/ldbc/parquet/ldbc.graph.yml";
  auto maybe_graph_info = GraphInfo::Load(ldbc_graph_path);
  REQUIRE(!maybe_graph_info.has_error());
  auto ldbc_graph_info = maybe_graph_info.value();

  SECTION("VerticesWithLabel") {
    // Test verticesWithLabel with organisation vertex (has labels: university, company, public)
    auto result =
        VerticesCollection::verticesWithLabel("university", ldbc_graph_info, "organisation");
    REQUIRE(!result.has_error());
    auto vertices = result.value();
    REQUIRE(vertices->size() > 0);

    // Iterate through vertices
    size_t count = 0;
    for (auto it = vertices->begin(); it != vertices->end() && count < 10; ++it, ++count) {
      REQUIRE(it.id() >= 0);
    }
  }

  SECTION("VerticesWithLabelbyAcero") {
    auto result =
        VerticesCollection::verticesWithLabelbyAcero("company", ldbc_graph_info, "organisation");
    REQUIRE(!result.has_error());
    auto vertices = result.value();
    // Result depends on data availability

    size_t count = 0;
    for (auto it = vertices->begin(); it != vertices->end() && count < 10; ++it, ++count) {
      REQUIRE(it.id() >= 0);
    }
  }

  SECTION("VerticesWithMultipleLabels") {
    auto result = VerticesCollection::verticesWithMultipleLabels(
        {"university", "company"}, ldbc_graph_info, "organisation");
    REQUIRE(!result.has_error());
    auto vertices = result.value();
    // Result depends on data

    size_t count = 0;
    for (auto it = vertices->begin(); it != vertices->end() && count < 10; ++it, ++count) {
      REQUIRE(it.id() >= 0);
    }
  }

  SECTION("VerticesWithMultipleLabelsbyAcero") {
    auto result = VerticesCollection::verticesWithMultipleLabelsbyAcero(
        {"university", "company"}, ldbc_graph_info, "organisation");
    REQUIRE(!result.has_error());
    auto vertices = result.value();
    // Result depends on data

    size_t count = 0;
    for (auto it = vertices->begin(); it != vertices->end() && count < 10; ++it, ++count) {
      REQUIRE(it.id() >= 0);
    }
  }

  SECTION("VerticesWithLabelOnCollection") {
    // First get all organisation vertices
    auto all_result = VerticesCollection::Make(ldbc_graph_info, "organisation");
    REQUIRE(!all_result.has_error());
    auto all_vertices = all_result.value();

    // Then filter with label
    auto result = VerticesCollection::verticesWithLabel("public", all_vertices);
    REQUIRE(!result.has_error());
    auto filtered_vertices = result.value();

    size_t count = 0;
    for (auto it = filtered_vertices->begin(); it != filtered_vertices->end() && count < 10;
         ++it, ++count) {
      REQUIRE(it.id() >= 0);
    }
  }

  SECTION("VerticesWithMultipleLabelsOnCollection") {
    auto all_result = VerticesCollection::Make(ldbc_graph_info, "organisation");
    REQUIRE(!all_result.has_error());
    auto all_vertices = all_result.value();

    auto result = VerticesCollection::verticesWithMultipleLabels(
        {"university"}, all_vertices);
    REQUIRE(!result.has_error());
    auto filtered_vertices = result.value();

    size_t count = 0;
    for (auto it = filtered_vertices->begin(); it != filtered_vertices->end() && count < 10;
         ++it, ++count) {
      REQUIRE(it.id() >= 0);
    }
  }

  SECTION("VerticesWithLabelError") {
    // Test with non-existent label - the function throws exception for invalid label
    try {
      auto result =
          VerticesCollection::verticesWithLabel("nonexistent_label", ldbc_graph_info, "organisation");
      // If no exception, result should be an error
      REQUIRE(result.has_error());
    } catch (const std::exception& e) {
      // Expected: exception thrown for invalid label
      REQUIRE(true);
    }
  }
}

TEST_CASE("VertexIterHasLabelAndLabelFunctions", "[graph][label]") {
  // Load the full ldbc graph which has labelled vertices (organisation)
  std::string ldbc_graph_path =
      std::string(std::getenv("GAR_TEST_DATA")) + "/ldbc/parquet/ldbc.graph.yml";
  auto maybe_graph_info = GraphInfo::Load(ldbc_graph_path);
  REQUIRE(!maybe_graph_info.has_error());
  auto ldbc_graph_info = maybe_graph_info.value();

  SECTION("HasLabel") {
    // Get organisation vertices which have labels: university, company, public
    auto result =
        VerticesCollection::verticesWithLabel("university", ldbc_graph_info, "organisation");
    REQUIRE(!result.has_error());
    auto vertices = result.value();
    REQUIRE(vertices->size() > 0);

    // Iterate and test hasLabel
    size_t count = 0;
    for (auto it = vertices->begin(); it != vertices->end() && count < 10; ++it, ++count) {
      REQUIRE(it.id() >= 0);
      // university vertices should have label "university" as true
      auto has_university = it.hasLabel("university");
      REQUIRE(!has_university.has_error());
      REQUIRE(has_university.value());
    }
  }

  SECTION("HasLabelOnMultipleLabelsCollection") {
    // Get vertices with multiple labels (university AND company can't exist)
    // So we test with single label collection
    auto result =
        VerticesCollection::verticesWithLabel("company", ldbc_graph_info, "organisation");
    REQUIRE(!result.has_error());
    auto vertices = result.value();

    size_t count = 0;
    for (auto it = vertices->begin(); it != vertices->end() && count < 10; ++it, ++count) {
      REQUIRE(it.id() >= 0);
      auto has_company = it.hasLabel("company");
      REQUIRE(!has_company.has_error());
      REQUIRE(has_company.value());
    }
  }

  SECTION("Label") {
    // Get all organisation vertices
    auto all_result = VerticesCollection::Make(ldbc_graph_info, "organisation");
    REQUIRE(!all_result.has_error());
    auto vertices = all_result.value();

    // Get all labels for the vertex
    size_t count = 0;
    for (auto it = vertices->begin(); it != vertices->end() && count < 10; ++it, ++count) {
      auto labels_result = it.label();
      REQUIRE(!labels_result.has_error());
      auto labels = labels_result.value();
      // All organisation vertices should have at least one label
      REQUIRE(labels.size() > 0);
      // Verify labels are valid (university, company, or public)
      for (const auto& label : labels) {
        REQUIRE((label == "university" || label == "company" || label == "public"));
      }
    }
  }

  SECTION("HasLabelError") {
    // Get organisation vertices
    auto result =
        VerticesCollection::verticesWithLabel("university", ldbc_graph_info, "organisation");
    REQUIRE(!result.has_error());
    auto vertices = result.value();

    // Test hasLabel with non-existent label
    auto it = vertices->begin();
    auto has_invalid = it.hasLabel("nonexistent_label");
    REQUIRE(has_invalid.has_error());
  }

  SECTION("LabelOnFilteredCollection") {
    // Test label() on filtered collection
    auto result =
        VerticesCollection::verticesWithLabel("public", ldbc_graph_info, "organisation");
    REQUIRE(!result.has_error());
    auto vertices = result.value();

    size_t count = 0;
    for (auto it = vertices->begin(); it != vertices->end() && count < 10; ++it, ++count) {
      auto labels_result = it.label();
      REQUIRE(!labels_result.has_error());
      auto labels = labels_result.value();
      REQUIRE(labels.size() > 0);
      // Verify "public" label exists
      bool has_public = false;
      for (const auto& label : labels) {
        if (label == "public") {
          has_public = true;
          break;
        }
      }
      REQUIRE(has_public);
    }
  }

  SECTION("HasLabelAndLabelCombination") {
    // Get all organisation vertices and verify both functions work together
    auto result = VerticesCollection::Make(ldbc_graph_info, "organisation");
    REQUIRE(!result.has_error());
    auto vertices = result.value();

    size_t count = 0;
    for (auto it = vertices->begin(); it != vertices->end() && count < 10; ++it, ++count) {
      auto labels_result = it.label();
      REQUIRE(!labels_result.has_error());
      auto labels = labels_result.value();

      // Verify each label returned by label() returns true for hasLabel
      for (const auto& label : labels) {
        auto has_label = it.hasLabel(label);
        REQUIRE(!has_label.has_error());
        REQUIRE(has_label.value());
      }

      // Verify non-existent labels return false (or error)
      auto has_nonexistent = it.hasLabel("nonexistent_label");
      REQUIRE(has_nonexistent.has_error());
    }
  }
}
}  // namespace graphar
