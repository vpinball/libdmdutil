#!/bin/bash

set -e

LIBZEDMD_SHA=b2190a3efaa52d705c9a5c62f00b418376a2604d
LIBSERUM_SHA=b69d2b436bc93570a2e7e78d0946cd3c43f7aed5

NUM_PROCS=$(sysctl -n hw.ncpu)

echo "Building libraries..."
echo "  LIBZEDMD_SHA: ${LIBZEDMD_SHA}"
echo "  LIBSERUM_SHA: ${LIBSERUM_SHA}"
echo ""

if [ -z "${BUILD_TYPE}" ]; then
   BUILD_TYPE="Release"
fi

echo "Build type: ${BUILD_TYPE}"
echo "Procs: ${NUM_PROCS}"
echo ""

rm -rf external
mkdir external
cd external

#
# build libzedmd and copy to external
#

curl -sL https://github.com/PPUC/libzedmd/archive/${LIBZEDMD_SHA}.zip -o libzedmd.zip
unzip libzedmd.zip
cd libzedmd-$LIBZEDMD_SHA
cp src/ZeDMD.h ../../third-party/include
platforms/macos/x64/external.sh
cmake -DPLATFORM=macos -DARCH=x64 -DBUILD_SHARED=ON -DBUILD_STATIC=OFF -DCMAKE_BUILD_TYPE=${BUILD_TYPE} -B build
cmake --build build -- -j${NUM_PROCS}
cp third-party/include/libserialport.h ../../third-party/include
cp third-party/runtime-libs/macos/x64/libserialport.dylib ../../third-party/runtime-libs/macos/x64
cp build/libzedmd.0.5.0.dylib ../../third-party/runtime-libs/macos/x64
cd ..

#
# build libserum and copy to external
#

curl -sL https://github.com/zesinger/libserum/archive/${LIBSERUM_SHA}.zip -o libserum.zip
unzip libserum.zip
cd libserum-$LIBSERUM_SHA
cp src/serum-decode.h ../../third-party/include
cmake -DPLATFORM=macos -DARCH=x64 -DBUILD_SHARED=ON -DBUILD_STATIC=OFF -DCMAKE_BUILD_TYPE=${BUILD_TYPE} -B build
cmake --build build -- -j${NUM_PROCS}
cp build/libserum.1.6.2.dylib ../../third-party/runtime-libs/macos/x64
cd ..