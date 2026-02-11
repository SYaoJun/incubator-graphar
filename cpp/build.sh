#!/bin/bash


set -e

PKG_DIR="/Users/yaojun/db/pkg"


export GAR_ARROW_SOURCE_URL="file://${PKG_DIR}/apache-arrow-15.0.0.tar.gz"
export BOOST_SOURCE_URL="file://${PKG_DIR}/boost-1.88.0-cmake.tar.gz"

# 添加Arrow头文件路径到环境变量
export CPATH="/opt/homebrew/include:$CPATH"



rm -rf build_macos
cmake -S . -B build_macos -DCMAKE_BUILD_TYPE=Debug -DBUILD_TESTS=ON -DBUILD_EXAMPLES=ON -DBUILD_BENCHMARKS=ON


cmake --build build_macos -j$(sysctl -n hw.ncpu)

