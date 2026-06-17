# GraphAr C++ - Bazel Build Guide

This directory contains Bazel build configuration for the GraphAr C++ library.

## Quick Start

### Prerequisites

- **Bazel 7.x** (or Bazelisk for automatic version management)
- **C++17** compatible compiler (GCC 9+, Clang 10+)
- **CMake** and **Ninja** (for rules_foreign_cc)

### Option 1: Use System-Installed Arrow (Recommended)

Install Apache Arrow C++ development packages:

```bash
# Ubuntu/Debian
sudo apt-get install -y \
    libarrow-dev \
    libarrow-acero-dev \
    libarrow-dataset-dev \
    libparquet-dev

# Or install from source:
# https://arrow.apache.org/install/
```

Then build:

```bash
cd cpp/
bazel build //src/graphar          # Build the library
bazel test //test:all_tests         # Run all tests (set GAR_TEST_DATA first)
```

### Option 2: Build Arrow from Source via Bazel

Edit `MODULE.bazel` and switch the Arrow dependency:

```python
# Comment out system Arrow:
# arrow_system = use_extension("//bazel:arrow_system.bzl", "arrow_system")
# use_repo(arrow_system, "arrow")

# Uncomment from-source Arrow:
arrow_build = use_extension("//bazel:arrow_extension.bzl", "arrow_build")
use_repo(arrow_build, "arrow")
```

Then build:

```bash
cd cpp/
bazel build //src/graphar
```

**Note:** Building Arrow from source takes significant time (30-60 minutes) and disk space.

## Build Targets

### Core Library

```bash
bazel build //src/graphar              # Main GraphAr library
bazel build //src/graphar:public_headers  # Header files only
```

### Tests

```bash
# Set test data path first:
export GAR_TEST_DATA=$(pwd)/../testing

# Run all tests:
bazel test //test:all_tests --test_env=GAR_TEST_DATA=$(pwd)/../testing

# Run a single test:
bazel test //test:test_info --test_env=GAR_TEST_DATA=$(pwd)/../testing

# Run with sanitizer:
bazel test --config=debug //test:all_tests --test_env=GAR_TEST_DATA=$(pwd)/../testing
```

### Examples (without Boost)

```bash
bazel build //examples:construct_info_example
bazel build //examples:low_level_reader_example
bazel build //examples:high_level_writer_example
```

### Benchmarks

```bash
bazel build //benchmarks:all_benchmarks
bazel run //benchmarks:arrow_chunk_reader_benchmark
```

## Build Configurations

```bash
# Debug build with AddressSanitizer
bazel build --config=debug //src/graphar

# Coverage build
bazel build --config=coverage //src/graphar

# Optimized release (default)
bazel build --compilation_mode=opt //src/graphar
```

## Boost-dependent Examples

Graph algorithm examples (BFS, PageRank, Connected Components) require Boost Graph Library.
Since Boost is not available on BCR, you need to:

1. Install Boost system-wide:
   ```bash
   sudo apt-get install libboost-graph-dev
   ```

2. Create a Boost BUILD file or use rules_foreign_cc to build it.

See `examples/BUILD.bazel` for commented-out Boost-dependent targets.

## Project Structure

```
cpp/
├── MODULE.bazel          # Bzlmod module definition (dependencies)
├── BUILD.bazel           # Root BUILD file (aliases)
├── .bazelrc              # Bazel configuration
├── .bazelversion         # Recommended Bazel version
├── bazel/                # Custom Bazel extensions
│   ├── BUILD.bazel
│   ├── arrow_extension.bzl   # Build Arrow from source
│   └── arrow_system.bzl      # Use system Arrow
├── src/
│   ├── BUILD.bazel
│   └── graphar/
│       └── BUILD.bazel   # Core library target
├── thirdparty/
│   └── BUILD.bazel       # Embedded third-party libraries
├── test/
│   └── BUILD.bazel       # Test targets
├── examples/
│   └── BUILD.bazel       # Example binaries
└── benchmarks/
    └── BUILD.bazel       # Benchmark binaries
```
