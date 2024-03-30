#!/bin/bash

set -e

CARGS_SHA=5949a20a926e902931de4a32adaad9f19c76f251
LIBZEDMD_SHA=42d95ed6f1fe2065ecbd247502d177d7e5eb7e4c
LIBSERUM_SHA=b69d2b436bc93570a2e7e78d0946cd3c43f7aed5
SOCKPP_SHA=e6c4688a576d95f42dd7628cefe68092f6c5cd0f
LIBPUPDMD_SHA=5ed99b7c02a3873ba2ef0bd582b6e9fcf8061222

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
cmake \
   -G "Visual Studio 17 2022" \
   -DBUILD_SHARED_LIBS=ON \
   -A Win32 \
   -B build
cmake --build build --config ${BUILD_TYPE}
cp include/cargs.h ../../third-party/include/
cp build/${BUILD_TYPE}/cargs.lib ../../third-party/build-libs/win/x86/
cp build/${BUILD_TYPE}/cargs.dll ../../third-party/runtime-libs/win/x86/
cd ..

#
# build libzedmd and copy to external
#

curl -sL https://github.com/PPUC/libzedmd/archive/${LIBZEDMD_SHA}.zip -o libzedmd.zip
unzip libzedmd.zip
cd libzedmd-$LIBZEDMD_SHA
platforms/win/x86/external.sh
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
cp third-party/build-libs/win/x86/libserialport.lib ../../third-party/build-libs/win/x86/
cp third-party/runtime-libs/win/x86/libserialport.dll ../../third-party/runtime-libs/win/x86/
cp build/${BUILD_TYPE}/zedmd.lib ../../third-party/build-libs/win/x86/
cp build/${BUILD_TYPE}/zedmd.dll ../../third-party/runtime-libs/win/x86/
cp -r test ../../
cd ..

#
# build libserum and copy to external
#

curl -sL https://github.com/zesinger/libserum/archive/${LIBSERUM_SHA}.zip -o libserum.zip
unzip libserum.zip
cd libserum-$LIBSERUM_SHA
cmake \
   -G "Visual Studio 17 2022" \
   -A Win32 \
   -DPLATFORM=win \
   -DARCH=x86 \
   -DBUILD_SHARED=ON \
   -DBUILD_STATIC=OFF \
   -B build
cmake --build build --config ${BUILD_TYPE}
cp src/serum-decode.h ../../third-party/include/
cp build/${BUILD_TYPE}/serum.lib ../../third-party/build-libs/win/x86/
cp build/${BUILD_TYPE}/serum.dll ../../third-party/runtime-libs/win/x86/
cd ..

#
# build sockpp and copy to external
#

curl -sL https://github.com/fpagliughi/sockpp/archive/${SOCKPP_SHA}.zip -o sockpp.zip
unzip sockpp.zip
cd sockpp-$SOCKPP_SHA
cmake \
   -G "Visual Studio 17 2022" \
   -A Win32 \
   -B build
cmake --build build --config ${BUILD_TYPE}
cp -r include/sockpp ../../third-party/include/
cp build/${BUILD_TYPE}/sockpp.lib ../../third-party/build-libs/win/x86/
cp build/${BUILD_TYPE}/sockpp.dll ../../third-party/runtime-libs/win/x86/
cd ..

#
# build libpupdmd and copy to external
#

curl -sL https://github.com/ppuc/libpupdmd/archive/${LIBPUPDMD_SHA}.zip -o libpupdmd.zip
unzip libpupdmd.zip
cd libpupdmd-$LIBPUPDMD_SHA
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
