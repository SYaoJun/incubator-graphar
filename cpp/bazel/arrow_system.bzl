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
Arrow repository rule for using a system-installed Apache Arrow C++.

Creates a repository that wraps the system-installed Arrow shared libraries
via symlinks, supporting macOS (Homebrew, .dylib) and Linux (.so).
"""

_ARROW_LIBS = [
    "arrow",
    "arrow_acero",
    "arrow_dataset",
    "parquet",
]

def _find_arrow_include(repo_ctx):
    """Find the Arrow include directory."""
    candidates = [
        "/opt/homebrew/include",
        "/usr/local/include",
        "/usr/include",
    ]
    for path in candidates:
        if repo_ctx.path(path + "/arrow/api.h").exists:
            return path
    fail("Arrow headers not found. Install: brew install apache-arrow")

def _find_arrow_lib_dir(repo_ctx):
    """Find the Arrow library directory."""
    candidates = [
        "/opt/homebrew/lib",
        "/usr/local/lib",
        "/usr/lib/x86_64-linux-gnu",
        "/usr/lib64",
        "/usr/lib/aarch64-linux-gnu",
    ]
    for path in candidates:
        for ext in [".dylib", ".so"]:
            if repo_ctx.path(path + "/libarrow" + ext).exists:
                return path, ext
    fail("Arrow libraries not found. Install: brew install apache-arrow")

def _arrow_repo_impl(repo_ctx):
    """Repository rule: symlink Arrow headers/libs and generate BUILD."""
    include_dir = _find_arrow_include(repo_ctx)
    lib_dir, lib_ext = _find_arrow_lib_dir(repo_ctx)

    # Symlink all headers as a tree under include/
    repo_ctx.symlink(include_dir + "/arrow", "include/arrow")
    repo_ctx.symlink(include_dir + "/parquet", "include/parquet")

    # Symlink shared libraries
    for lib in _ARROW_LIBS:
        src = "{0}/lib{1}{2}".format(lib_dir, lib, lib_ext)
        dst = "lib{0}{1}".format(lib, lib_ext)
        repo_ctx.symlink(src, dst)

    build_content = """# System-installed Apache Arrow C++
load("@rules_cc//cc:defs.bzl", "cc_import", "cc_library")

"""

    for lib in _ARROW_LIBS:
        build_content += """
cc_import(
    name = "{lib}_shared",
    shared_library = ":lib{lib}{ext}",
    visibility = ["//visibility:private"],
)
""".format(lib = lib, ext = lib_ext)

    build_content += """
cc_library(
    name = "arrow",
    hdrs = glob(["include/arrow/**/*.h"]),
    strip_include_prefix = "include",
    visibility = ["//visibility:public"],
    deps = [
        ":arrow_shared",
    ],
)

cc_library(
    name = "arrow_acero",
    hdrs = glob(["include/arrow/**/*.h"]),
    strip_include_prefix = "include",
    visibility = ["//visibility:public"],
    deps = [
        ":arrow",
        ":arrow_acero_shared",
    ],
)

cc_library(
    name = "arrow_dataset",
    hdrs = glob(["include/arrow/**/*.h"]),
    strip_include_prefix = "include",
    visibility = ["//visibility:public"],
    deps = [
        ":arrow",
        ":arrow_dataset_shared",
    ],
)

cc_library(
    name = "parquet",
    hdrs = glob(["include/parquet/**/*.h"]),
    strip_include_prefix = "include",
    visibility = ["//visibility:public"],
    deps = [
        ":arrow",
        ":parquet_shared",
    ],
)
"""

    repo_ctx.file("BUILD.bazel", build_content)

_arrow_repo = repository_rule(
    implementation = _arrow_repo_impl,
    local = True,
)

def _arrow_system_impl(module_ctx):
    """Module extension that creates the Arrow system repository."""
    _arrow_repo(name = "arrow")

arrow_system = module_extension(
    implementation = _arrow_system_impl,
)
