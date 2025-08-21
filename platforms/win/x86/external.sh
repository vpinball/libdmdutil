#!/bin/bash

set -e

source ./platforms/config.sh

echo "Building libraries..."
echo "  LIBZEDMD_SHA: ${LIBZEDMD_SHA}"
echo "  LIBSERUM_SHA: ${LIBSERUM_SHA}"
echo "  LIBPUPDMD_SHA: ${LIBPUPDMD_SHA}"
echo ""

rm -rf external
mkdir external
cd external

#
# build libzedmd and copy to external
#

curl -sL https://github.com/PPUC/libzedmd/archive/${LIBZEDMD_SHA}.tar.gz -o libzedmd-${LIBZEDMD_SHA}.tar.gz
tar xzf libzedmd-${LIBZEDMD_SHA}.tar.gz
mv libzedmd-${LIBZEDMD_SHA} libzedmd
cd libzedmd
BUILD_TYPE=${BUILD_TYPE} platforms/win/x86/external.sh
cmake \
   -G "Visual Studio 17 2022" \
   -A Win32 \
   -DPLATFORM=win \
   -DARCH=x86 \
   -DBUILD_SHARED=ON \
   -DBUILD_STATIC=OFF \
   -B build
cmake --build build --config ${BUILD_TYPE}
cp src/ZeDMD.h ../../third-party/include/
cp third-party/include/libserialport.h ../../third-party/include/
cp third-party/include/cargs.h ../../third-party/include/
cp -r third-party/include/komihash ../../third-party/include/
cp -r third-party/include/sockpp ../../third-party/include/
cp third-party/include/FrameUtil.h ../../third-party/include/
cp third-party/build-libs/win/x86/cargs.lib ../../third-party/build-libs/win/x86/
cp third-party/runtime-libs/win/x86/cargs.dll ../../third-party/runtime-libs/win/x86/
cp third-party/build-libs/win/x86/libserialport.lib ../../third-party/build-libs/win/x86/
cp third-party/runtime-libs/win/x86/libserialport.dll ../../third-party/runtime-libs/win/x86/
cp third-party/build-libs/win/x86/sockpp.lib ../../third-party/build-libs/win/x86/
cp third-party/runtime-libs/win/x86/sockpp.dll ../../third-party/runtime-libs/win/x86/
cp build/${BUILD_TYPE}/zedmd.lib ../../third-party/build-libs/win/x86/
cp build/${BUILD_TYPE}/zedmd.dll ../../third-party/runtime-libs/win/x86/
cp -r test ../../
cd ..

#
# build libserum and copy to external
#

curl -sL https://github.com/PPUC/libserum_concentrate/archive/${LIBSERUM_SHA}.tar.gz -o libserum-${LIBSERUM_SHA}.tar.gz
tar xzf libserum-${LIBSERUM_SHA}.tar.gz
mv libserum_concentrate-${LIBSERUM_SHA} libserum
cd libserum
cmake \
   -G "Visual Studio 17 2022" \
   -A Win32 \
   -DPLATFORM=win \
   -DARCH=x86 \
   -DBUILD_SHARED=ON \
   -DBUILD_STATIC=OFF \
   -B build
cmake --build build --config ${BUILD_TYPE}
cp src/SceneGenerator.h ../../third-party/include/
cp src/serum.h ../../third-party/include/
cp src/serum-decode.h ../../third-party/include/
cp build/${BUILD_TYPE}/serum.lib ../../third-party/build-libs/win/x86/
cp build/${BUILD_TYPE}/serum.dll ../../third-party/runtime-libs/win/x86/
cd ..

#
# build libpupdmd and copy to external
#

curl -sL https://github.com/ppuc/libpupdmd/archive/${LIBPUPDMD_SHA}.tar.gz -o libpupdmd-${LIBPUPDMD_SHA}.tar.gz
tar xzf libpupdmd-${LIBPUPDMD_SHA}.tar.gz
mv libpupdmd-${LIBPUPDMD_SHA} libpupdmd
cd libpupdmd
cmake \
   -G "Visual Studio 17 2022" \
   -A Win32 \
   -DPLATFORM=win \
   -DARCH=x86 \
   -DBUILD_SHARED=ON \
   -DBUILD_STATIC=OFF \
   -B build
cmake --build build --config ${BUILD_TYPE}
cp src/pupdmd.h ../../third-party/include/
cp build/${BUILD_TYPE}/pupdmd.lib ../../third-party/build-libs/win/x86/
cp build/${BUILD_TYPE}/pupdmd.dll ../../third-party/runtime-libs/win/x86/
cd ..
