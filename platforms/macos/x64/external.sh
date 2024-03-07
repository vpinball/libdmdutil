#!/bin/bash

set -e

LIBZEDMD_SHA=08e98a858eb6e1394b4844bec7dd27c7c0d9a845
LIBSERUM_SHA=b69d2b436bc93570a2e7e78d0946cd3c43f7aed5
LIBSOCKPP_SHA=e6c4688a576d95f42dd7628cefe68092f6c5cd0f

NUM_PROCS=$(sysctl -n hw.ncpu)

SCRIPT_DIR=$(realpath "$(dirname "$0")")

echo "Building libraries..."
echo "  LIBZEDMD_SHA: ${LIBZEDMD_SHA}"
echo "  LIBSERUM_SHA: ${LIBSERUM_SHA}"
echo "  LIBSOCKPP_SHA: ${LIBSOCKPP_SHA}"
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
cp src/ZeDMD.h ../../third-party/include/
platforms/macos/x64/external.sh
cmake -DPLATFORM=macos -DARCH=x64 -DBUILD_SHARED=ON -DBUILD_STATIC=OFF -DCMAKE_BUILD_TYPE=${BUILD_TYPE} -B build
cmake --build build -- -j${NUM_PROCS}
cp third-party/include/libserialport.h ../../third-party/include/
cp -P third-party/runtime-libs/macos/x64/*.dylib ../../third-party/runtime-libs/macos/x64/
cp -P build/*.dylib ../../third-party/runtime-libs/macos/x64/
cp -r test ../../
cd ..

#
# build libserum and copy to external
#

curl -sL https://github.com/zesinger/libserum/archive/${LIBSERUM_SHA}.zip -o libserum.zip
unzip libserum.zip
cd libserum-$LIBSERUM_SHA
cp src/serum-decode.h ../../third-party/include/
cmake -DPLATFORM=macos -DARCH=x64 -DBUILD_SHARED=ON -DBUILD_STATIC=OFF -DCMAKE_BUILD_TYPE=${BUILD_TYPE} -B build
cmake --build build -- -j${NUM_PROCS}
cp -P build/*.dylib ../../third-party/runtime-libs/macos/x64/
cd ..

#
# build libsockpp and copy to external
#

curl -sL https://github.com/fpagliughi/sockpp/archive/${LIBSOCKPP_SHA}.zip -o sockpp.zip
unzip sockpp.zip
cd sockpp-$LIBSOCKPP_SHA
cp -r include/sockpp ../../third-party/include/
cmake -DSOCKPP_BUILD_SHARED=ON \
   -DSOCKPP_BUILD_STATIC=OFF \
   -DCMAKE_OSX_DEPLOYMENT_TARGET=13.0 \
   -DCMAKE_OSX_ARCHITECTURES=x86_64 \
   -DCMAKE_BUILD_TYPE=${BUILD_TYPE} \
   -B build
cmake --build build -- -j${NUM_PROCS}
cp -P build/*.dylib ../../third-party/runtime-libs/macos/x64/
cd ..
