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
Arrow module extension for using a system-installed Apache Arrow C++.

This extension creates a cc_import repository that wraps the system-installed
Arrow shared libraries. The user must have Arrow C++ installed on their system.

System requirements:
    - libarrow-dev (or arrow-devel)
    - libparquet-dev (or parquet-devel)
    - Arrow Acero and Dataset modules

The extension searches for Arrow headers in standard locations:
    /usr/include/arrow
    /usr/local/include/arrow

And libraries in:
    /usr/lib/x86_64-linux-gnu
    /usr/lib64
    /usr/local/lib
"""

_ARROW_LIBS = [
    "arrow",
    "arrow_acero",
    "arrow_dataset",
    "parquet",
]

def _find_arrow_include(module_ctx):
    """Find the Arrow include directory."""
    candidates = [
        "/usr/include",
        "/usr/local/include",
    ]
    for path in candidates:
        if module_ctx.path(path + "/arrow/api.h").exists:
            return path
    return "/usr/include"  # fallback

def _find_arrow_lib_dir(module_ctx):
    """Find the Arrow library directory."""
    candidates = [
        "/usr/lib/x86_64-linux-gnu",
        "/usr/lib64",
        "/usr/local/lib",
        "/usr/lib/aarch64-linux-gnu",
    ]
    for path in candidates:
        if module_ctx.path(path + "/libarrow.so").exists:
            return path
    return "/usr/lib/x86_64-linux-gnu"  # fallback

def _arrow_system_impl(module_ctx):
    """Module extension to create a system Arrow repository."""

    include_dir = _find_arrow_include(module_ctx)
    lib_dir = _find_arrow_lib_dir(module_ctx)

    # Create BUILD file for the system Arrow repository
    build_content = """# System-installed Apache Arrow C++
load("@rules_cc//cc:defs.bzl", "cc_import", "cc_library")

"""

    # Create cc_import targets for each Arrow shared library
    for lib in _ARROW_LIBS:
        build_content += """
cc_import(
    name = "{lib}_shared",
    shared_library = "{lib_dir}/lib{lib}.so",
    visibility = ["//visibility:private"],
)
""".format(lib = lib, lib_dir = lib_dir)

    # Create a combined cc_library with all headers and libraries
    build_content += """
cc_library(
    name = "arrow",
    hdrs = glob(["{include_dir}/arrow/**/*.h"]),
    includes = ["{include_dir}"],
    visibility = ["//visibility:public"],
    deps = [
        ":arrow_shared",
    ],
)

cc_library(
    name = "arrow_acero",
    hdrs = glob(["{include_dir}/arrow/**/*.h"]),
    includes = ["{include_dir}"],
    visibility = ["//visibility:public"],
    deps = [
        ":arrow",
        ":arrow_acero_shared",
    ],
)

cc_library(
    name = "arrow_dataset",
    hdrs = glob(["{include_dir}/arrow/**/*.h"]),
    includes = ["{include_dir}"],
    visibility = ["//visibility:public"],
    deps = [
        ":arrow",
        ":arrow_dataset_shared",
    ],
)

cc_library(
    name = "parquet",
    hdrs = glob(["{include_dir}/parquet/**/*.h"]),
    includes = ["{include_dir}"],
    visibility = ["//visibility:public"],
    deps = [
        ":arrow",
        ":parquet_shared",
    ],
)
""".format(include_dir = include_dir, lib_dir = lib_dir)

    module_ctx.file("BUILD.bazel", build_content)

arrow_system = module_extension(
    implementation = _arrow_system_impl,
)
