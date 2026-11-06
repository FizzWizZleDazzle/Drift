#!/bin/sh
# Builds the native library this package ships, into dist/.
#
# The library is a C shim over the Drift engine with the engine and its
# dependencies linked in, so dist/ holds one self-contained file:
# libdrift_candela.so, .dylib or .dll. Extra arguments go to the CMake
# configure step (for example -DDRIFT_BACKEND_WEBGPU=ON).
set -eu
cd "$(dirname "$0")"
cmake -S ../.. -B build -DCMAKE_BUILD_TYPE=Release \
  -DDRIFT_BUILD_CANDELA=ON -DDRIFT_BUILD_EXAMPLES=OFF -DDRIFT_BUILD_TESTS=OFF \
  -DDRIFT_DEV=OFF "$@"
cmake --build build --config Release --target drift_candela
ls -l dist
