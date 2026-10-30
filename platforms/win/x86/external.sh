#!/bin/bash

set -e

if [ -z "${MSYS2_PATH}" ]; then
   MSYS2_PATH="/c/msys64"
fi

echo "MSYS2_PATH: ${MSYS2_PATH}"
echo ""

source ./platforms/config.sh

echo "Building libraries..."
if [ "${ENABLE_PIN2DMD}" != "OFF" ]; then
   echo "  LIBUSB_SHA: ${LIBUSB_SHA}"
fi
echo "  LIBZEDMD_SHA: ${LIBZEDMD_SHA}"
if [ "${ENABLE_SERUM}" != "OFF" ]; then
   echo "  LIBSERUM_SHA: ${LIBSERUM_SHA}"
fi
if [ "${ENABLE_PUP}" != "OFF" ]; then
   echo "  LIBPUPDMD_SHA: ${LIBPUPDMD_SHA}"
fi
if [ "${ENABLE_VNI}" != "OFF" ]; then
   echo "  LIBVNI_SHA: ${LIBVNI_SHA}"
fi
echo ""

rm -rf external
mkdir external
cd external

if [ "${ENABLE_PIN2DMD}" != "OFF" ]; then
#
# build libusb and copy to third-party
#

curl -sL https://github.com/libusb/libusb/archive/${LIBUSB_SHA}.tar.gz -o libusb-${LIBUSB_SHA}.tar.gz
tar xzf libusb-${LIBUSB_SHA}.tar.gz
mv libusb-${LIBUSB_SHA} libusb
cd libusb
CURRENT_DIR="$(pwd)"
MSYSTEM=MINGW32 "${MSYS2_PATH}/usr/bin/bash.exe" -l -c "
   cd \"${CURRENT_DIR}\" &&
   ./autogen.sh &&
   ./configure --enable-shared &&
   make -j\$(nproc)
"
mkdir -p ../../third-party/include/libusb-1.0
cp libusb/libusb.h ../../third-party/include/libusb-1.0
cp libusb/.libs/libusb-1.0.dll.a ../../third-party/build-libs/win/x86/libusb-1.0.lib
cp libusb/.libs/libusb-1.0.dll ../../third-party/runtime-libs/win/x86/
cd ..
fi

#
# build libzedmd and copy to external
#

curl -sL https://github.com/PPUC/libzedmd/archive/${LIBZEDMD_SHA}.tar.gz -o libzedmd-${LIBZEDMD_SHA}.tar.gz
tar xzf libzedmd-${LIBZEDMD_SHA}.tar.gz
mv libzedmd-${LIBZEDMD_SHA} libzedmd
cd libzedmd
BUILD_TYPE=${BUILD_TYPE} platforms/win/x86/external.sh
cmake \
   -G "Visual Studio 18 2026" \
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
cp third-party/runtime-libs/win/x86/libserialport-0.dll ../../third-party/runtime-libs/win/x86/
cp third-party/build-libs/win/x86/sockpp.lib ../../third-party/build-libs/win/x86/
cp third-party/runtime-libs/win/x86/sockpp.dll ../../third-party/runtime-libs/win/x86/
cp third-party/runtime-libs/win/x86/libgcc_s_dw2-1.dll ../../third-party/runtime-libs/win/x86/
cp third-party/runtime-libs/win/x86/libstdc++-6.dll ../../third-party/runtime-libs/win/x86/
cp third-party/runtime-libs/win/x86/libwinpthread-1.dll ../../third-party/runtime-libs/win/x86/
cp build/${BUILD_TYPE}/zedmd.lib ../../third-party/build-libs/win/x86/
cp build/${BUILD_TYPE}/zedmd.dll ../../third-party/runtime-libs/win/x86/
cp -r test ../../
cd ..

if [ "${ENABLE_SERUM}" != "OFF" ]; then
#
# build libserum and copy to external
#

curl -sL https://github.com/PPUC/libserum/archive/${LIBSERUM_SHA}.tar.gz -o libserum-${LIBSERUM_SHA}.tar.gz
tar xzf libserum-${LIBSERUM_SHA}.tar.gz
mv libserum-${LIBSERUM_SHA} libserum
cd libserum
./platforms/win/x86/external.sh
cmake \
   -G "Visual Studio 18 2026" \
   -A Win32 \
   -DPLATFORM=win \
   -DARCH=x86 \
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
cp build/${BUILD_TYPE}/serum.lib ../../third-party/build-libs/win/x86/
cp build/${BUILD_TYPE}/serum.dll ../../third-party/runtime-libs/win/x86/
cd ..
fi

if [ "${ENABLE_PUP}" != "OFF" ]; then
#
# build libpupdmd and copy to external
#

curl -sL https://github.com/PPUC/libpupdmd/archive/${LIBPUPDMD_SHA}.tar.gz -o libpupdmd-${LIBPUPDMD_SHA}.tar.gz
tar xzf libpupdmd-${LIBPUPDMD_SHA}.tar.gz
mv libpupdmd-${LIBPUPDMD_SHA} libpupdmd
cd libpupdmd
cmake \
   -G "Visual Studio 18 2026" \
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
fi

if [ "${ENABLE_VNI}" != "OFF" ]; then
#
# build libvni and copy to external
#

curl -sL https://github.com/PPUC/libvni/archive/${LIBVNI_SHA}.tar.gz -o libvni-${LIBVNI_SHA}.tar.gz
tar xzf libvni-${LIBVNI_SHA}.tar.gz
mv libvni-${LIBVNI_SHA} libvni
cd libvni
platforms/win/x86/external.sh
cmake \
   -G "Visual Studio 18 2026" \
   -A Win32 \
   -DPLATFORM=win \
   -DARCH=x86 \
   -DBUILD_SHARED=ON \
   -DBUILD_STATIC=OFF \
   -B build
cmake --build build --config ${BUILD_TYPE}
cp src/vni.h ../../third-party/include/
cp build/${BUILD_TYPE}/vni.lib ../../third-party/build-libs/win/x86/
cp build/${BUILD_TYPE}/vni.dll ../../third-party/runtime-libs/win/x86/
cd ..
fi
