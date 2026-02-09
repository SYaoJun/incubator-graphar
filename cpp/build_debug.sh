#!/bin/bash
# GraphAr Debug Build Script with Local Dependencies
# Usage: ./build_debug.sh

set -e

# Local package directory
PKG_DIR="/Users/yaojun/db/pkg"

# Arrow 15.0.0
ARROW_VERSION="15.0.0"
ARROW_TARBALL="${PKG_DIR}/apache-arrow-${ARROW_VERSION}.tar.gz"
ARROW_URL="https://dlcdn.apache.org/arrow/arrow-${ARROW_VERSION}/apache-arrow-${ARROW_VERSION}.tar.gz"

# Download Arrow 15.0.0 if not present
if [ ! -f "$ARROW_TARBALL" ]; then
    echo "Downloading Apache Arrow ${ARROW_VERSION}..."
    curl -L -o "$ARROW_TARBALL" "$ARROW_URL"
    echo "Downloaded to ${ARROW_TARBALL}"
else
    echo "Using existing ${ARROW_TARBALL}"
fi

# Export environment variables for local dependencies
export GAR_ARROW_SOURCE_URL="file://${ARROW_TARBALL}"
export ARROW_VERSION_TO_BUILD="${ARROW_VERSION}"
export BOOST_SOURCE_URL="file://${PKG_DIR}/boost-1.88.0-cmake.tar.gz"

echo "=== Configuration ==="
echo "Arrow source: ${GAR_ARROW_SOURCE_URL}"
echo "Boost source: ${BOOST_SOURCE_URL}"
echo ""

# Clean and configure
rm -rf build-debug
cmake -S . -B build-debug -DCMAKE_BUILD_TYPE=Debug -DBUILD_TESTS=ON -DBUILD_EXAMPLES=ON -DBUILD_BENCHMARKS=ON -DArrow_ROOT=/opt/homebrew/opt/apache-arrow

# Build
echo ""
echo "=== Building ==="
cmake --build build-debug -j$(sysctl -n hw.ncpu)

echo ""
echo "=== Build complete ==="
echo "Binaries are in: build-debug/"
