# Licensed to the Apache Software Foundation (ASF) under one
# or more contributor license agreements.  See the NOTICE file
# distributed with this work for additional information
# regarding copyright ownership.  The ASF licenses this file
# to you under the Apache License, Version 2.0 (the
# "License"); you may not use this file except in compliance
# with the License.  You may obtain a copy of the License at
#
#   http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing,
# software distributed under the License is distributed on an
# "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
# KIND, either express or implied.  See the License for the
# specific language governing permissions and limitations
# under the License.

find_package(benchmark REQUIRED)

macro(add_graphar_benchmark target)
    set(options)
    set(oneValueArgs)
    set(multiValueArgs SRCS)
    cmake_parse_arguments(add_benchmark "${options}" "${oneValueArgs}" "${multiValueArgs}" ${ARGN})
    add_executable(${target} ${add_benchmark_SRCS})
    target_compile_features(${target} PRIVATE cxx_std_17)
    target_include_directories(${target} PRIVATE ${PROJECT_SOURCE_DIR}/thirdparty)
    target_link_libraries(${target} PRIVATE benchmark::benchmark_main graphar ${CMAKE_DL_LIBS})
endmacro()

add_graphar_benchmark(arrow_chunk_reader_benchmark SRCS arrow_chunk_reader_benchmark.cc)
add_graphar_benchmark(label_filter_benchmark SRCS label_filter_benchmark.cc)
add_graphar_benchmark(graph_info_benchmark SRCS graph_info_benchmark.cc)
