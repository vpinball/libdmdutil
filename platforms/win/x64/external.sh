#!/bin/bash

set -e

source ./platforms/config.sh

echo "Building libraries..."
echo "  LIBUSB_SHA: ${LIBUSB_SHA}"
echo "  LIBZEDMD_SHA: ${LIBZEDMD_SHA}"
echo "  LIBSERUM_SHA: ${LIBSERUM_SHA}"
echo "  LIBPUPDMD_SHA: ${LIBPUPDMD_SHA}"
echo "  LIBVNI_SHA: ${LIBVNI_SHA}"
echo ""

rm -rf external
mkdir external
cd external

#
# build libusb and copy to third-party
#

curl -sL https://github.com/libusb/libusb/archive/${LIBUSB_SHA}.tar.gz -o libusb-${LIBUSB_SHA}.tar.gz
tar xzf libusb-${LIBUSB_SHA}.tar.gz
mv libusb-${LIBUSB_SHA} libusb
cd libusb
sed -i.bak 's/LIBRARY.*libusb-1.0/LIBRARY libusb64-1.0/' libusb/libusb-1.0.def
# remove patch after this is fixed: https://github.com/libusb/libusb/issues/1649#issuecomment-2940138443
cp ../../platforms/win/x64/libusb/libusb_dll.vcxproj msvc
msbuild.exe msvc/libusb_dll.vcxproj \
   -p:TargetName=libusb64-1.0 \
   -p:Configuration=${BUILD_TYPE} \
   -p:Platform=x64 \
   -p:PlatformToolset=v143
mkdir -p ../../third-party/include/libusb-1.0
cp libusb/libusb.h ../../third-party/include/libusb-1.0
cp build/v143/x64/${BUILD_TYPE}/libusb_dll/../dll/libusb64-1.0.lib ../../third-party/build-libs/win/x64
cp build/v143/x64/${BUILD_TYPE}/libusb_dll/../dll/libusb64-1.0.dll ../../third-party/runtime-libs/win/x64
cd ..

#
# build libzedmd and copy to external
#

curl -sL https://github.com/PPUC/libzedmd/archive/${LIBZEDMD_SHA}.tar.gz -o libzedmd-${LIBZEDMD_SHA}.tar.gz
tar xzf libzedmd-${LIBZEDMD_SHA}.tar.gz
mv libzedmd-${LIBZEDMD_SHA} libzedmd
cd libzedmd
BUILD_TYPE=${BUILD_TYPE} platforms/win/x64/external.sh
cmake \
   -G "Visual Studio 18 2026" \
   -DPLATFORM=win \
   -DARCH=x64 \
   -DBUILD_SHARED=ON \
   -DBUILD_STATIC=OFF \
   -B build
cmake --build build --config ${BUILD_TYPE}
cp src/ZeDMD.h ../../third-party/include/
cp third-party/include/cargs.h ../../third-party/include/
cp -r third-party/include/komihash ../../third-party/include/
cp -r third-party/include/sockpp ../../third-party/include/
cp third-party/include/FrameUtil.h ../../third-party/include/
cp third-party/include/libserialport.h ../../third-party/include/
cp third-party/build-libs/win/x64/cargs64.lib ../../third-party/build-libs/win/x64/
cp third-party/runtime-libs/win/x64/cargs64.dll ../../third-party/runtime-libs/win/x64/
cp third-party/build-libs/win/x64/libserialport64.lib ../../third-party/build-libs/win/x64/
cp third-party/runtime-libs/win/x64/libserialport64.dll ../../third-party/runtime-libs/win/x64/
cp third-party/build-libs/win/x64/sockpp64.lib ../../third-party/build-libs/win/x64/
cp third-party/runtime-libs/win/x64/sockpp64.dll ../../third-party/runtime-libs/win/x64/
cp build/${BUILD_TYPE}/zedmd64.lib ../../third-party/build-libs/win/x64/
cp build/${BUILD_TYPE}/zedmd64.dll ../../third-party/runtime-libs/win/x64/
cp -r test ../../
cd ..

#
# build libserum and copy to external
#

curl -sL https://github.com/PPUC/libserum/archive/${LIBSERUM_SHA}.tar.gz -o libserum-${LIBSERUM_SHA}.tar.gz
tar xzf libserum-${LIBSERUM_SHA}.tar.gz
mv libserum-${LIBSERUM_SHA} libserum
cd libserum
cmake \
   -G "Visual Studio 18 2026" \
   -DPLATFORM=win \
   -DARCH=x64 \
   -DBUILD_SHARED=ON \
   -DBUILD_STATIC=OFF \
   -B build
cmake --build build --config ${BUILD_TYPE}
cp -r third-party/include/lz4 ../../third-party/include/
cp src/LZ4Stream.h ../../third-party/include/
cp src/SceneGenerator.h ../../third-party/include/
cp src/serum.h ../../third-party/include/
cp src/TimeUtils.h ../../third-party/include/
cp src/serum-decode.h ../../third-party/include/
cp build/${BUILD_TYPE}/serum64.lib ../../third-party/build-libs/win/x64/
cp build/${BUILD_TYPE}/serum64.dll ../../third-party/runtime-libs/win/x64/
cd ..

#
# build libpupdmd and copy to external
#

curl -sL https://github.com/PPUC/libpupdmd/archive/${LIBPUPDMD_SHA}.tar.gz -o libpupdmd-${LIBPUPDMD_SHA}.tar.gz
tar xzf libpupdmd-${LIBPUPDMD_SHA}.tar.gz
mv libpupdmd-${LIBPUPDMD_SHA} libpupdmd
cd libpupdmd
cmake \
   -G "Visual Studio 18 2026" \
   -DPLATFORM=win \
   -DARCH=x64 \
   -DBUILD_SHARED=ON \
   -DBUILD_STATIC=OFF \
   -B build
cmake --build build --config ${BUILD_TYPE}
cp src/pupdmd.h ../../third-party/include/
cp build/${BUILD_TYPE}/pupdmd64.lib ../../third-party/build-libs/win/x64/
cp build/${BUILD_TYPE}/pupdmd64.dll ../../third-party/runtime-libs/win/x64/
cd ..

#
# build libvni and copy to external
#

curl -sL https://github.com/PPUC/libvni/archive/${LIBVNI_SHA}.tar.gz -o libvni-${LIBVNI_SHA}.tar.gz
tar xzf libvni-${LIBVNI_SHA}.tar.gz
mv libvni-${LIBVNI_SHA} libvni
cd libvni
cmake \
   -G "Visual Studio 17 2022" \
   -DPLATFORM=win \
   -DARCH=x64 \
   -DBUILD_SHARED=ON \
   -DBUILD_STATIC=OFF \
   -B build
cmake --build build --config ${BUILD_TYPE}
cp src/vni.h ../../third-party/include/
cp build/${BUILD_TYPE}/vni64.lib ../../third-party/build-libs/win/x64/
cp build/${BUILD_TYPE}/vni64.dll ../../third-party/runtime-libs/win/x64/
cd ..
