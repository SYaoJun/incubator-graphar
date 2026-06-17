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

"""
Arrow module extension for building Apache Arrow C++ from source using rules_foreign_cc.

This extension downloads the Arrow source release and builds it via CMake,
producing cc_library targets that other rules can depend on.

Usage in MODULE.bazel:
    arrow_build = use_extension("//bazel:arrow_extension.bzl", "arrow_build")
    use_repo(arrow_build, "arrow")

Then depend on:
    @arrow//:arrow
    @arrow//:arrow_acero
    @arrow//:arrow_dataset
    @arrow//:parquet

Arrow version can be configured below. Note that Arrow >= 24.0.0 requires C++20.
"""

load("@rules_foreign_cc//foreign_cc:defs.bzl", "cmake")

# Arrow version to build.
# - 18.1.0: C++17, stable
# - 22.0.0: C++17, newer features
# - 24.0.0: C++20 required
ARROW_VERSION = "18.1.0"

def _arrow_build_impl(module_ctx):
    """Module extension to build Apache Arrow C++ from source."""

    # Download Arrow source
    module_ctx.download_and_extract(
        url = "https://dlcdn.apache.org/arrow/arrow-{}/apache-arrow-{}.tar.gz".format(
            ARROW_VERSION,
            ARROW_VERSION,
        ),
        output = "arrow_src",
        stripPrefix = "apache-arrow-{}".format(ARROW_VERSION),
    )

    # Write a BUILD file for the Arrow source directory
    module_ctx.file(
        "arrow_src/BUILD.bazel",
        content = """\
load("@rules_foreign_cc//foreign_cc:defs.bzl", "cmake")

filegroup(
    name = "all_srcs",
    srcs = glob(
        ["**"],
        exclude = ["BUILD.bazel"],
    ),
)

# Common CMake cache entries for all Arrow builds
_ARROW_COMMON_CACHE = {{
    "ARROW_BUILD_SHARED": "ON",
    "ARROW_BUILD_STATIC": "OFF",
    "ARROW_DEPENDENCY_SOURCE": "BUNDLED",
    # Components needed by GraphAr
    "ARROW_COMPUTE": "ON",
    "ARROW_CSV": "ON",
    "ARROW_DATASET": "ON",
    "ARROW_FILESYSTEM": "ON",
    "ARROW_JSON": "ON",
    "ARROW_PARQUET": "ON",
    "ARROW_ACERO": "ON",
    "ARROW_ORC": "ON",
    # Compression libraries
    "ARROW_WITH_BROTLI": "ON",
    "ARROW_WITH_BZ2": "ON",
    "ARROW_WITH_LZ4": "ON",
    "ARROW_WITH_SNAPPY": "ON",
    "ARROW_WITH_ZLIB": "ON",
    "ARROW_WITH_ZSTD": "ON",
    # Disable unnecessary components
    "ARROW_BUILD_TESTS": "OFF",
    "ARROW_BUILD_BENCHMARKS": "OFF",
    "ARROW_BUILD_EXAMPLES": "OFF",
    "ARROW_BUILD_INTEGRATION": "OFF",
    "ARROW_FLIGHT": "OFF",
    "ARROW_GANDIVA": "OFF",
    "ARROW_JEMALLOC": "OFF",
    "ARROW_MIMALLOC": "OFF",
    "ARROW_S3": "OFF",
    "ARROW_SUBSTRAIT": "OFF",
    "ARROW_USE_CCACHE": "OFF",
    # C++ standard
    "CMAKE_CXX_STANDARD": "17",
}}

cmake(
    name = "arrow_build",
    cache_entries = dict(_ARROW_COMMON_CACHE),
    env = {{
        "CMAKE_BUILD_PARALLEL_LEVEL": "4",
    }},
    generate_args = ["-GNinja"],
    lib_source = ":all_srcs",
    out_lib_dir = "lib",
    out_shared_libs = [
        "libarrow.so",
        "libarrow_acero.so",
        "libarrow_dataset.so",
        "libparquet.so",
    ],
    visibility = ["//visibility:public"],
)

# Header-only target for Arrow include paths
cc_library(
    name = "arrow_headers",
    hdrs = glob(
        ["**/*.h"],
        allow_empty = True,
    ),
    includes = [
        "cpp/src",
    ],
    visibility = ["//visibility:public"],
)

# Individual library targets
cc_import(
    name = "libarrow",
    shared_library = ":arrow_build",
    visibility = ["//visibility:private"],
)

cc_library(
    name = "arrow",
    hdrs = glob(["cpp/src/arrow/**/*.h"], allow_empty = True),
    includes = ["cpp/src"],
    visibility = ["//visibility:public"],
    deps = [":libarrow"],
)

cc_library(
    name = "arrow_acero",
    hdrs = glob(["cpp/src/arrow/**/*.h"], allow_empty = True),
    includes = ["cpp/src"],
    visibility = ["//visibility:public"],
    deps = [":libarrow"],
)

cc_library(
    name = "arrow_dataset",
    hdrs = glob(["cpp/src/arrow/**/*.h"], allow_empty = True),
    includes = ["cpp/src"],
    visibility = ["//visibility:public"],
    deps = [":libarrow"],
)

cc_library(
    name = "parquet",
    hdrs = glob(["cpp/src/parquet/**/*.h"], allow_empty = True),
    includes = ["cpp/src"],
    visibility = ["//visibility:public"],
    deps = [":libarrow"],
)
""",
    )

    # Create root BUILD for the arrow repo
    module_ctx.file(
        "BUILD.bazel",
        content = "# Arrow external repository root\n",
    )

arrow_build = module_extension(
    implementation = _arrow_build_impl,
)
