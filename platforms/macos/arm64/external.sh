#!/bin/bash

set -e

CARGS_SHA=5949a20a926e902931de4a32adaad9f19c76f251
LIBZEDMD_SHA=42d95ed6f1fe2065ecbd247502d177d7e5eb7e4c
LIBSERUM_SHA=b69d2b436bc93570a2e7e78d0946cd3c43f7aed5
SOCKPP_SHA=e6c4688a576d95f42dd7628cefe68092f6c5cd0f
LIBPUPDMD_SHA=63cae52ccae975fb509f7f992a5503be43b08456

NUM_PROCS=$(sysctl -n hw.ncpu)

echo "Building libraries..."
echo "  CARGS_SHA: ${CARGS_SHA}"
echo "  LIBZEDMD_SHA: ${LIBZEDMD_SHA}"
echo "  LIBSERUM_SHA: ${LIBSERUM_SHA}"
echo "  SOCKPP_SHA: ${SOCKPP_SHA}"
echo "  LIBPUPDMD_SHA: ${LIBPUPDMD_SHA}"
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
# build cargs and copy to external
#

curl -sL https://github.com/likle/cargs/archive/${CARGS_SHA}.zip -o cargs.zip
unzip cargs.zip
cd cargs-${CARGS_SHA}
cp include/cargs.h ../../third-party/include/
mkdir build
cd build
cmake -DCMAKE_OSX_ARCHITECTURES=arm64 -DBUILD_SHARED_LIBS=ON ..
make
cp -a libcargs*.dylib ../../../third-party/runtime-libs/macos/arm64/
cd ../..

#
# build libzedmd and copy to external
#

curl -sL https://github.com/PPUC/libzedmd/archive/${LIBZEDMD_SHA}.zip -o libzedmd.zip
unzip libzedmd.zip
cd libzedmd-$LIBZEDMD_SHA
cp src/ZeDMD.h ../../third-party/include/
platforms/macos/arm64/external.sh
cmake -DPLATFORM=macos -DARCH=arm64 -DBUILD_SHARED=ON -DBUILD_STATIC=OFF -DCMAKE_BUILD_TYPE=${BUILD_TYPE} -B build
cmake --build build -- -j${NUM_PROCS}
cp third-party/include/libserialport.h ../../third-party/include/
cp -a third-party/runtime-libs/macos/arm64/*.dylib ../../third-party/runtime-libs/macos/arm64/
cp -a build/*.dylib ../../third-party/runtime-libs/macos/arm64/
cp -r test ../../
cd ..

#
# build libserum and copy to external
#

curl -sL https://github.com/zesinger/libserum/archive/${LIBSERUM_SHA}.zip -o libserum.zip
unzip libserum.zip
cd libserum-$LIBSERUM_SHA
cp src/serum-decode.h ../../third-party/include/
cmake -DPLATFORM=macos -DARCH=arm64 -DBUILD_SHARED=ON -DBUILD_STATIC=OFF -DCMAKE_BUILD_TYPE=${BUILD_TYPE} -B build
cmake --build build -- -j${NUM_PROCS}
cp -a build/*.dylib ../../third-party/runtime-libs/macos/arm64/
cd ..

#
# build sockpp and copy to external
#

curl -sL https://github.com/fpagliughi/sockpp/archive/${SOCKPP_SHA}.zip -o sockpp.zip
unzip sockpp.zip
cd sockpp-$SOCKPP_SHA
cp -r include/sockpp ../../third-party/include/
cmake -DSOCKPP_BUILD_SHARED=ON \
   -DSOCKPP_BUILD_STATIC=OFF \
   -DCMAKE_OSX_DEPLOYMENT_TARGET=13.0 \
   -DCMAKE_OSX_ARCHITECTURES=arm64 \
   -DCMAKE_BUILD_TYPE=${BUILD_TYPE} \
   -B build
cmake --build build -- -j${NUM_PROCS}
cp -a build/*.dylib ../../third-party/runtime-libs/macos/arm64/
cd ..

#
# build libpupdmd and copy to external
#

curl -sL https://github.com/ppuc/libpupdmd/archive/${LIBPUDMD_SHA}.zip -o libpupdmd.zip
unzip libpupdmd.zip
cd libpupdmd-$LIBPUDMD_SHA
cp src/pupdmd.h ../../third-party/include/
cmake -DPLATFORM=macos -DARCH=arm64 -DBUILD_SHARED=ON -DBUILD_STATIC=OFF -DCMAKE_BUILD_TYPE=${BUILD_TYPE} -B build
cmake --build build -- -j${NUM_PROCS}
cp -a build/*.dylib ../../third-party/runtime-libs/macos/arm64/
cd ..
