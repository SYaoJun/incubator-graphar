set -e




export CPATH="/opt/homebrew/include:$CPATH"

rm -rf build_macos
cmake -S . -B build_macos -DCMAKE_BUILD_TYPE=Debug -DBUILD_TESTS=ON -DBUILD_EXAMPLES=ON -DBUILD_BENCHMARKS=ON

cmake --build build_macos -j$(sysctl -n hw.ncpu)