#!/bin/bash
set -ex

# build with make first
mkdir build-make
pushd build-make
cmake \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_CXX_COMPILER=${CXX} \
    -DCMAKE_C_COMPILER=${CC} \
    -DCMAKE_VERBOSE_MAKEFILE=ON \
    ..
cmake --build . -- ninja
export NINJA=$PWD/ninja
popd

# build with ninja second
mkdir build-ninja
pushd build-ninja
cmake \
    -GNinja \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_CXX_COMPILER=${CXX} \
    -DCMAKE_C_COMPILER=${CC} \
    -DCMAKE_MAKE_PROGRAM=${NINJA} ..
cmake --build . -- -v
ctest
popd

sha1sum build-{make,ninja}/{ninja,libninja.a}
