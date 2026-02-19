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

# Collect core source files
file(GLOB_RECURSE CORE_SRC_FILES "graphar/*.cc" ${CMAKE_CURRENT_SOURCE_DIR}/../thirdparty/mini-yaml/yaml/*.cpp)

# Build graphar library
if(GRAPHAR_BUILD_STATIC)
    add_library(graphar STATIC ${CORE_SRC_FILES})
else()
    add_library(graphar SHARED ${CORE_SRC_FILES})
endif()

# Install target
install(TARGETS graphar
        EXPORT graphar-targets
        ARCHIVE DESTINATION lib
        LIBRARY DESTINATION lib
        RUNTIME DESTINATION bin
        INCLUDES DESTINATION include
)

target_compile_features(graphar PRIVATE cxx_std_17)
target_include_directories(graphar PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../thirdparty)
target_link_libraries(graphar PRIVATE ${CMAKE_DL_LIBS})

# Arrow linking
if(BUILD_ARROW_FROM_SOURCE)
    target_include_directories(graphar SYSTEM BEFORE PRIVATE ${GAR_ARROW_INCLUDE_DIR})
    if(APPLE)
        target_link_libraries(graphar PRIVATE -Wl,-force_load
            graphar_bundled_dependencies
            "-framework CoreFoundation"
            "-framework Security")
    elseif(MSVC)
        target_link_libraries(graphar PRIVATE graphar_bundled_dependencies)
    else()
        target_link_libraries(graphar PRIVATE -Wl,--exclude-libs,ALL
            graphar_bundled_dependencies)
    endif()
    # OpenSSL and CURL
    if(OPENSSL_FOUND)
        target_link_libraries(graphar PUBLIC OpenSSL::SSL)
    endif()
    if(CURL_FOUND)
        target_link_libraries(graphar PUBLIC ${CURL_LIBRARIES})
    endif()
else()
    if(APPLE)
        if(USE_STATIC_ARROW)
            target_link_libraries(graphar PRIVATE -Wl,-force_load
                Arrow::arrow_static
                Parquet::parquet_static
                ArrowDataset::arrow_dataset_static
                ArrowAcero::arrow_acero_static)
        else()
            target_link_libraries(graphar PRIVATE -Wl,-force_load Arrow::arrow_shared
                Parquet::parquet_shared
                ArrowDataset::arrow_dataset_shared
                ArrowAcero::arrow_acero_shared)
        endif()
    elseif(MSVC)
        if(USE_STATIC_ARROW)
            target_link_libraries(graphar PRIVATE
                Arrow::arrow_static
                Parquet::parquet_static
                ArrowDataset::arrow_dataset_static
                ArrowAcero::arrow_acero_static)
        else()
            target_link_libraries(graphar PRIVATE
                Arrow::arrow_shared
                Parquet::parquet_shared
                ArrowDataset::arrow_dataset_shared
                ArrowAcero::arrow_acero_shared)
        endif()
    else()
        if(USE_STATIC_ARROW)
            target_link_libraries(graphar PRIVATE -Wl,--exclude-libs,ALL -Wl,--whole-archive
                Arrow::arrow_static
                Parquet::parquet_static
                ArrowDataset::arrow_dataset_static
                ArrowAcero::arrow_acero_static -Wl,--no-whole-archive)
        else()
            target_link_libraries(graphar PRIVATE -Wl,--exclude-libs,ALL -Wl,--whole-archive Arrow::arrow_shared
                Parquet::parquet_shared
                ArrowDataset::arrow_dataset_shared
                ArrowAcero::arrow_acero_shared -Wl,--no-whole-archive)
        endif()
    endif()
endif()
