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
Arrow build-from-source module extension.

Builds Apache Arrow C++ via cmake + ninja in a repository rule
during the loading phase. Does NOT require rules_foreign_cc cmake() rule,
avoiding Bazel 9 toolchain incompatibility.
"""

# Arrow version to build.
ARROW_VERSION = "24.0.0"

_ARROW_LIBS = [
    "arrow",
    "arrow_acero",
    "arrow_compute",
    "arrow_dataset",
    "parquet",
]

def _get_lib_ext(repo_ctx):
    """Return the shared library extension for the current OS."""
    os_name = repo_ctx.os.name.lower()
    if "mac" in os_name:
        return ".dylib"
    return ".so"

def _arrow_build_repo_impl(repo_ctx):
    """Repository rule: download and build Arrow from source."""
    version = repo_ctx.attr.version
    lib_ext = _get_lib_ext(repo_ctx)

    # Download and extract Arrow source
    repo_ctx.report_progress("Downloading Arrow %s source..." % version)
    repo_ctx.download_and_extract(
        url = "https://archive.apache.org/dist/arrow/arrow-{ver}/apache-arrow-{ver}.tar.gz".format(
            ver = version,
        ),
        output = "arrow_src",
        stripPrefix = "apache-arrow-{}".format(version),
    )

    # Build Arrow with cmake + ninja
    repo_ctx.report_progress("Configuring Arrow with cmake...")
    build_dir = "build"

    cmake_args = [
        "cmake",
        "-GNinja",
        "-S", "cpp",
        "-B", build_dir,
        "-DCMAKE_BUILD_TYPE=Release",
        "-DARROW_BUILD_SHARED=ON",
        "-DARROW_BUILD_STATIC=OFF",
        "-DARROW_DEPENDENCY_SOURCE=BUNDLED",
        "-DARROW_COMPUTE=ON",
        "-DARROW_CSV=ON",
        "-DARROW_DATASET=ON",
        "-DARROW_FILESYSTEM=ON",
        "-DARROW_JSON=ON",
        "-DARROW_PARQUET=ON",
        "-DARROW_ACERO=ON",
        "-DARROW_ORC=ON",
        "-DARROW_WITH_BROTLI=ON",
        "-DARROW_WITH_BZ2=ON",
        "-DARROW_WITH_LZ4=ON",
        "-DARROW_WITH_SNAPPY=ON",
        "-DARROW_WITH_ZLIB=ON",
        "-DARROW_WITH_ZSTD=ON",
        "-DARROW_BUILD_TESTS=OFF",
        "-DARROW_BUILD_BENCHMARKS=OFF",
        "-DARROW_BUILD_EXAMPLES=OFF",
        "-DARROW_BUILD_INTEGRATION=OFF",
        "-DARROW_FLIGHT=OFF",
        "-DARROW_GANDIVA=OFF",
        "-DARROW_JEMALLOC=OFF",
        "-DARROW_MIMALLOC=OFF",
        "-DARROW_S3=OFF",
        "-DARROW_SUBSTRAIT=OFF",
        "-DARROW_USE_CCACHE=OFF",
        "-DCMAKE_CXX_STANDARD=20",
    ]

    result = repo_ctx.execute(
        cmake_args,
        working_directory = "arrow_src",
        quiet = False,
        timeout = 900,
    )
    if result.return_code != 0:
        fail("cmake configure failed:\n%s\n%s" % (result.stdout, result.stderr))

    # Build (with parallel jobs)
    repo_ctx.report_progress("Building Arrow with ninja...")
    result = repo_ctx.execute(
        ["ninja", "-C", "build", "-j", str(repo_ctx.attr.jobs)],
        working_directory = "arrow_src",
        quiet = False,
        timeout = 7200,
    )
    if result.return_code != 0:
        fail("ninja build failed:\n%s\n%s" % (result.stdout, result.stderr))

    # Symlink libraries from build dir to repo root for easy access
    # Arrow cmake puts shared libs in build/release/ (for Release builds)
    for lib in _ARROW_LIBS:
        src = "arrow_src/build/release/lib{}{}".format(lib, lib_ext)
        dst = "lib{}{}".format(lib, lib_ext)
        repo_ctx.symlink(src, dst)

    # Symlink source headers
    repo_ctx.symlink("arrow_src/cpp/src/arrow", "include_src/arrow")
    repo_ctx.symlink("arrow_src/cpp/src/parquet", "include_src/parquet")

    # Symlink generated headers (version.h, config.h, etc.)
    repo_ctx.symlink("arrow_src/build/src/arrow", "include_gen/arrow")
    repo_ctx.symlink("arrow_src/build/src/parquet", "include_gen/parquet")

    # Write BUILD file
    build_content = """# Apache Arrow C++ - Built from source
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
    hdrs = glob([
        "include_src/arrow/**/*.h",
        "include_gen/arrow/**/*.h",
    ]),
    includes = ["include_src", "include_gen"],
    visibility = ["//visibility:public"],
    deps = [":arrow_shared"],
)

cc_library(
    name = "arrow_acero",
    hdrs = glob([
        "include_src/arrow/**/*.h",
        "include_gen/arrow/**/*.h",
    ]),
    includes = ["include_src", "include_gen"],
    visibility = ["//visibility:public"],
    deps = [":arrow", ":arrow_acero_shared"],
)

cc_library(
    name = "arrow_compute",
    hdrs = glob([
        "include_src/arrow/**/*.h",
        "include_gen/arrow/**/*.h",
    ]),
    includes = ["include_src", "include_gen"],
    visibility = ["//visibility:public"],
    deps = [":arrow", ":arrow_compute_shared"],
)

cc_library(
    name = "arrow_dataset",
    hdrs = glob([
        "include_src/arrow/**/*.h",
        "include_gen/arrow/**/*.h",
    ]),
    includes = ["include_src", "include_gen"],
    visibility = ["//visibility:public"],
    deps = [":arrow", ":arrow_dataset_shared"],
)

cc_library(
    name = "parquet",
    hdrs = glob([
        "include_src/parquet/**/*.h",
        "include_gen/parquet/**/*.h",
    ]),
    includes = ["include_src", "include_gen"],
    visibility = ["//visibility:public"],
    deps = [":arrow", ":parquet_shared"],
)
"""

    repo_ctx.file("BUILD.bazel", build_content)

_arrow_build_repo = repository_rule(
    implementation = _arrow_build_repo_impl,
    attrs = {
        "version": attr.string(mandatory = True),
        "jobs": attr.int(default = 4),
    },
)

def _arrow_build_impl(module_ctx):
    """Module extension: creates the Arrow-from-source repository."""
    _arrow_build_repo(
        name = "arrow",
        version = ARROW_VERSION,
        jobs = 12,  # Use more jobs for faster build
    )

arrow_build = module_extension(
    implementation = _arrow_build_impl,
)
