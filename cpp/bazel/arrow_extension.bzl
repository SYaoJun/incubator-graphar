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
Arrow module extension — unified entry point.

Two strategies, selected via the GAR_ARROW_SOURCE environment variable:

    GAR_ARROW_SOURCE=1  bazel build //src/graphar   → build Arrow from source
    bazel build //src/graphar                        → use system Arrow (default)

The from-source build downloads and compiles Apache Arrow via cmake + ninja
inside a repository rule during the loading phase.
"""

# Arrow version used when building from source.
ARROW_VERSION = "24.0.0"

# Shared libraries produced by the from-source build.
_ARROW_BUILD_LIBS = [
    "arrow",
    "arrow_acero",
    "arrow_compute",
    "arrow_dataset",
    "parquet",
]

# Shared libraries expected from a system installation.
_ARROW_SYSTEM_LIBS = [
    "arrow",
    "arrow_acero",
    "arrow_compute",
    "arrow_dataset",
    "parquet",
]

# --------------------------------------------------------------------------- #
#  Helpers
# --------------------------------------------------------------------------- #

def _get_lib_ext(repo_ctx):
    os_name = repo_ctx.os.name.lower()
    return ".dylib" if "mac" in os_name else ".so"

def _is_source_build(repo_ctx):
    val = repo_ctx.os.environ.get("GAR_ARROW_SOURCE", "")
    return val.lower() in ("1", "on", "true", "yes")

# --------------------------------------------------------------------------- #
#  Option A: build from source
# --------------------------------------------------------------------------- #

def _build_from_source(repo_ctx):
    version = repo_ctx.attr.version
    lib_ext = _get_lib_ext(repo_ctx)

    repo_ctx.report_progress("Downloading Arrow %s source..." % version)
    repo_ctx.download_and_extract(
        url = "https://archive.apache.org/dist/arrow/arrow-{ver}/apache-arrow-{ver}.tar.gz".format(ver = version),
        output = "arrow_src",
        stripPrefix = "apache-arrow-{}".format(version),
    )

    repo_ctx.report_progress("Configuring Arrow with cmake...")
    build_dir = "build"
    cmake_args = [
        "cmake", "-GNinja",
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

    result = repo_ctx.execute(cmake_args, working_directory = "arrow_src", quiet = False, timeout = 900)
    if result.return_code != 0:
        fail("cmake configure failed:\n%s\n%s" % (result.stdout, result.stderr))

    repo_ctx.report_progress("Building Arrow with ninja...")
    result = repo_ctx.execute(
        ["ninja", "-C", "build", "-j", str(repo_ctx.attr.jobs)],
        working_directory = "arrow_src", quiet = False, timeout = 7200,
    )
    if result.return_code != 0:
        fail("ninja build failed:\n%s\n%s" % (result.stdout, result.stderr))

    # Symlink built libraries (cmake puts them in build/release/ for Release)
    for lib in _ARROW_BUILD_LIBS:
        repo_ctx.symlink("arrow_src/build/release/lib{}{}".format(lib, lib_ext), "lib{}{}".format(lib, lib_ext))

    # Source headers
    repo_ctx.symlink("arrow_src/cpp/src/arrow", "include_src/arrow")
    repo_ctx.symlink("arrow_src/cpp/src/parquet", "include_src/parquet")
    # Generated headers (version.h, config.h, etc.)
    repo_ctx.symlink("arrow_src/build/src/arrow", "include_gen/arrow")
    repo_ctx.symlink("arrow_src/build/src/parquet", "include_gen/parquet")

    # BUILD file
    build_content = '# Apache Arrow C++ — built from source\n'
    build_content += 'load("@rules_cc//cc:defs.bzl", "cc_import", "cc_library")\n\n'

    for lib in _ARROW_BUILD_LIBS:
        build_content += 'cc_import(\n'
        build_content += '    name = "%s_shared",\n' % lib
        build_content += '    shared_library = ":lib%s%s",\n' % (lib, lib_ext)
        build_content += '    visibility = ["//visibility:private"],\n'
        build_content += ')\n\n'

    for lib in _ARROW_BUILD_LIBS:
        deps_str = '":arrow_shared"' if lib == "arrow" else '":arrow", ":%s_shared"' % lib
        hdrs_path = "parquet" if lib == "parquet" else "arrow"
        build_content += 'cc_library(\n'
        build_content += '    name = "%s",\n' % lib
        build_content += '    hdrs = glob([\n'
        build_content += '        "include_src/%s/**/*.h",\n' % hdrs_path
        build_content += '        "include_gen/%s/**/*.h",\n' % hdrs_path
        build_content += '    ]),\n'
        build_content += '    includes = ["include_src", "include_gen"],\n'
        build_content += '    visibility = ["//visibility:public"],\n'
        build_content += '    deps = [%s],\n' % deps_str
        build_content += ')\n\n'

    repo_ctx.file("BUILD.bazel", build_content)

# --------------------------------------------------------------------------- #
#  Option B: system-installed Arrow
# --------------------------------------------------------------------------- #

def _find_arrow_include(repo_ctx):
    for path in ["/opt/homebrew/include", "/usr/local/include", "/usr/include"]:
        if repo_ctx.path(path + "/arrow/api.h").exists:
            return path
    fail("Arrow headers not found. Install: brew install apache-arrow")

def _find_arrow_lib_dir(repo_ctx):
    for path in ["/opt/homebrew/lib", "/usr/local/lib",
                 "/usr/lib/x86_64-linux-gnu", "/usr/lib64", "/usr/lib/aarch64-linux-gnu"]:
        for ext in [".dylib", ".so"]:
            if repo_ctx.path(path + "/libarrow" + ext).exists:
                return path, ext
    fail("Arrow libraries not found. Install: brew install apache-arrow")

def _use_system(repo_ctx):
    include_dir = _find_arrow_include(repo_ctx)
    lib_dir, lib_ext = _find_arrow_lib_dir(repo_ctx)

    repo_ctx.symlink(include_dir + "/arrow", "include/arrow")
    repo_ctx.symlink(include_dir + "/parquet", "include/parquet")

    for lib in _ARROW_SYSTEM_LIBS:
        repo_ctx.symlink("{0}/lib{1}{2}".format(lib_dir, lib, lib_ext), "lib{0}{1}".format(lib, lib_ext))

    build_content = '# System-installed Apache Arrow C++\n'
    build_content += 'load("@rules_cc//cc:defs.bzl", "cc_import", "cc_library")\n\n'

    for lib in _ARROW_SYSTEM_LIBS:
        build_content += 'cc_import(\n'
        build_content += '    name = "%s_shared",\n' % lib
        build_content += '    shared_library = ":lib%s%s",\n' % (lib, lib_ext)
        build_content += '    visibility = ["//visibility:private"],\n'
        build_content += ')\n\n'

    for name in _ARROW_SYSTEM_LIBS:
        deps_str = '":arrow_shared"' if name == "arrow" else '":arrow", ":%s_shared"' % name
        hdrs_glob = 'glob(["include/parquet/**/*.h"])' if name == "parquet" else 'glob(["include/arrow/**/*.h"])'
        build_content += 'cc_library(\n'
        build_content += '    name = "%s",\n' % name
        build_content += '    hdrs = %s,\n' % hdrs_glob
        build_content += '    strip_include_prefix = "include",\n'
        build_content += '    visibility = ["//visibility:public"],\n'
        build_content += '    deps = [%s],\n' % deps_str
        build_content += ')\n\n'

    repo_ctx.file("BUILD.bazel", build_content)

# --------------------------------------------------------------------------- #
#  Unified repository rule
# --------------------------------------------------------------------------- #

def _arrow_repo_impl(repo_ctx):
    if _is_source_build(repo_ctx):
        _build_from_source(repo_ctx)
    else:
        _use_system(repo_ctx)

_arrow_repo = repository_rule(
    implementation = _arrow_repo_impl,
    attrs = {
        "version": attr.string(default = ARROW_VERSION),
        "jobs": attr.int(default = 12),
    },
)

# --------------------------------------------------------------------------- #
#  Module extension
# --------------------------------------------------------------------------- #

def _arrow_impl(module_ctx):
    _arrow_repo(name = "arrow")

arrow = module_extension(implementation = _arrow_impl)
