# GraphAr C++

This directory contains the code and build system for the GraphAr C++ library.

## Requirements

- **Bazel 9.1** — the build system. Install via [Bazelisk](https://github.com/bazelbuild/bazelisk) for automatic version management.
- **C++20** compiler — required by Apache Arrow >= 24.0.0.
- **CMake 3.16+** and **Ninja** — used internally to build Apache Arrow from source.

On macOS, install prerequisites with [Homebrew](https://brew.sh):

```bash
brew install bazelisk cmake ninja
```

On Ubuntu/Debian:

```bash
sudo apt-get install -y build-essential cmake ninja-build
# Install Bazelisk:
# https://github.com/bazelbuild/bazelisk/releases
```

> [!NOTE]
> Apache Arrow C++ does **not** need to be pre-installed. The build system
> downloads and compiles Arrow 24.0.0 from source automatically on the first
> `bazel build`.

## Quick Start

```bash
git clone https://github.com/apache/incubator-graphar.git
cd incubator-graphar/cpp

# Build the library (first build downloads + compiles Arrow, ~20-30 min)
bazel build //src/graphar
```

## Build the Library

```bash
# Debug build with AddressSanitizer
bazel build --config=debug //src/graphar

# Optimized release build (default)
bazel build //src/graphar

# Build a specific target
bazel build //src/graphar           # Core library
bazel build //examples:...          # All examples
bazel build //benchmarks:...        # All benchmarks
```

## Run Tests

```bash
# Initialize test data submodule
git submodule update --init --recursive

# Run all tests
bazel test //test:all_tests --test_env=GAR_TEST_DATA=$(pwd)/../testing

# Run a single test
bazel test //test:test_info --test_env=GAR_TEST_DATA=$(pwd)/../testing

# Debug build with sanitizer
bazel test --config=debug //test:all_tests --test_env=GAR_TEST_DATA=$(pwd)/../testing
```

## Run Examples

```bash
bazel build //examples:all_examples

# Run an example
bazel run //examples:construct_info_example
# … or execute directly:
./bazel-bin/examples/construct_info_example
```

> Boost-dependent examples (BFS, PageRank, etc.) require a local Boost
> installation. See `examples/BUILD.bazel` for details.

## Run Benchmarks

```bash
bazel build //benchmarks:all_benchmarks
bazel run //benchmarks:arrow_chunk_reader_benchmark
```

## Build Configurations

| Name     | Command                       | Effect                                      |
|----------|-------------------------------|---------------------------------------------|
| release  | `bazel build …` (default)     | `-O3`, debug symbols                        |
| debug    | `bazel build --config=debug`  | `-O0`, AddressSanitizer, frame pointer      |
| coverage | `bazel build --config=coverage` | prof-guided coverage instrumentation     |

## Arrow Dependency

The build system supports two strategies, controlled by the `GAR_ARROW_SOURCE`
environment variable. No editing of `MODULE.bazel` is needed.

### Default — Use system-installed Arrow

```bash
# Requires Arrow C++ (>= 24.0.0) to be installed on your system
bazel build //src/graphar
```

On macOS: `brew install apache-arrow`  
On Ubuntu: `sudo apt-get install libarrow-dev libarrow-dataset-dev libarrow-acero-dev libparquet-dev`

### Build Arrow from source

```bash
# Downloads and compiles Arrow 24.0.0 automatically (~20-30 min first time)
GAR_ARROW_SOURCE=1 bazel build //src/graphar
```

This requires `cmake` and `ninja` (used internally by the repository rule).  
The Arrow version is controlled by `ARROW_VERSION` in `bazel/arrow_extension.bzl`.

## Generate API Documentation

Install [Doxygen](https://www.doxygen.nl/) (>= 1.8), then:

```bash
doxygen Doxyfile
```

Output goes to `docs_doxygen/`.

## Code Formatting & Linting

```bash
# clang-format
clang-format --style=file -i src/graphar/*.h src/graphar/*.cc \
    test/*.h test/*.cc examples/*.h examples/*.cc \
    benchmarks/*.h benchmarks/*.cc

# cpplint
python misc/cpplint.py --root=include \
    src/graphar/*.h src/graphar/*.cc \
    test/*.h test/*.cc examples/*.h examples/*.cc \
    benchmarks/*.h benchmarks/*.cc
```

## Project Structure

```
cpp/
├── MODULE.bazel          # Bzlmod dependencies
├── BUILD.bazel           # Root aliases
├── .bazelversion         # Required Bazel version (9.1.0)
├── .bazelrc              # Build settings & configurations
├── bazel/                # Custom Bazel extensions
│   └── arrow_extension.bzl   # Arrow dependency (source / system)
├── src/graphar/          # Core library source
├── test/                 # Unit tests (Catch2)
├── examples/             # Example programs
├── benchmarks/           # Google Benchmark targets
└── thirdparty/           # Vendored third-party libraries
```

## How to use

Please refer to the [GraphAr C++ API Reference](https://graphar.apache.org/docs/category/c-library).
