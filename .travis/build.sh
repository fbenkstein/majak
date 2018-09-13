#!/bin/bash
set -ex

cmake --version

# build only ninja with make first
mkdir build-make
pushd build-make
cmake \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_CXX_COMPILER=${CXX} \
    -DCMAKE_C_COMPILER=${CC} \
    -DCMAKE_VERBOSE_MAKEFILE=ON \
    -DNINJA_BUILD_TESTS=OFF \
    ..
cmake --build . -- ninja
export NINJA=$PWD/ninja
popd

# build everything with ninja second
mkdir build-ninja
pushd build-ninja
cmake \
    -GNinja \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_CXX_COMPILER=${CXX} \
    -DCMAKE_C_COMPILER=${CC} \
    -DCMAKE_MAKE_PROGRAM=${NINJA} ..
cmake --build . -- -v
ctest --output-on-failure
popd
