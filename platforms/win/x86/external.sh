#!/bin/bash

set -e

if [ -z "${MSYS2_PATH}" ]; then
   MSYS2_PATH="/c/msys64"
fi

echo "MSYS2_PATH: ${MSYS2_PATH}"
echo ""

source ./platforms/config.sh

echo "Building libraries..."
echo "  LIBUSB_SHA: ${LIBUSB_SHA}"
echo "  LIBZEDMD_SHA: ${LIBZEDMD_SHA}"
print_dependency_source LIBZEDMD "${LIBZEDMD_SHA}" LIBZEDMD_SOURCE_DIR
echo "  LIBSERUM_SHA: ${LIBSERUM_SHA}"
print_dependency_source LIBSERUM "${LIBSERUM_SHA}" LIBSERUM_SOURCE_DIR
echo "  LIBPUPDMD_SHA: ${LIBPUPDMD_SHA}"
echo "  LIBVNI_SHA: ${LIBVNI_SHA}"
print_dependency_source LIBVNI "${LIBVNI_SHA}" LIBVNI_SOURCE_DIR
echo ""


rm -rf external
mkdir -p \
   external \
   third-party/include \
   third-party/build-libs/win/x86 \
   third-party/runtime-libs/win/x86
cd external

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
mkdir -p ${PROJECT_SOURCE_ROOT}/third-party/include/libusb-1.0
cp libusb/libusb.h ${PROJECT_SOURCE_ROOT}/third-party/include/libusb-1.0
cp libusb/.libs/libusb-1.0.dll.a ${PROJECT_SOURCE_ROOT}/third-party/build-libs/win/x86/libusb-1.0.lib
cp libusb/.libs/libusb-1.0.dll ${PROJECT_SOURCE_ROOT}/third-party/runtime-libs/win/x86/
cd ..

#
# build libzedmd and copy to external
#

prepare_dependency_source libzedmd "${LIBZEDMD_SHA}" "https://github.com/PPUC/libzedmd/archive/${LIBZEDMD_SHA}.tar.gz" tar LIBZEDMD_SOURCE_DIR
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
cp src/ZeDMD.h ${PROJECT_SOURCE_ROOT}/third-party/include/
cp third-party/include/libserialport.h ${PROJECT_SOURCE_ROOT}/third-party/include/
cp third-party/include/cargs.h ${PROJECT_SOURCE_ROOT}/third-party/include/
cp -r third-party/include/komihash ${PROJECT_SOURCE_ROOT}/third-party/include/
cp -r third-party/include/sockpp ${PROJECT_SOURCE_ROOT}/third-party/include/
cp third-party/include/FrameUtil.h ${PROJECT_SOURCE_ROOT}/third-party/include/
cp third-party/build-libs/win/x86/cargs.lib ${PROJECT_SOURCE_ROOT}/third-party/build-libs/win/x86/
cp third-party/runtime-libs/win/x86/cargs.dll ${PROJECT_SOURCE_ROOT}/third-party/runtime-libs/win/x86/
cp third-party/build-libs/win/x86/libserialport.lib ${PROJECT_SOURCE_ROOT}/third-party/build-libs/win/x86/
cp third-party/runtime-libs/win/x86/libserialport-0.dll ${PROJECT_SOURCE_ROOT}/third-party/runtime-libs/win/x86/
cp third-party/build-libs/win/x86/sockpp.lib ${PROJECT_SOURCE_ROOT}/third-party/build-libs/win/x86/
cp third-party/runtime-libs/win/x86/sockpp.dll ${PROJECT_SOURCE_ROOT}/third-party/runtime-libs/win/x86/
cp third-party/runtime-libs/win/x86/libgcc_s_dw2-1.dll ${PROJECT_SOURCE_ROOT}/third-party/runtime-libs/win/x86/
cp third-party/runtime-libs/win/x86/libstdc++-6.dll ${PROJECT_SOURCE_ROOT}/third-party/runtime-libs/win/x86/
cp third-party/runtime-libs/win/x86/libwinpthread-1.dll ${PROJECT_SOURCE_ROOT}/third-party/runtime-libs/win/x86/
cp build/${BUILD_TYPE}/zedmd.lib ${PROJECT_SOURCE_ROOT}/third-party/build-libs/win/x86/
cp build/${BUILD_TYPE}/zedmd.dll ${PROJECT_SOURCE_ROOT}/third-party/runtime-libs/win/x86/
cp -r test ../../
cd ..

#
# build libserum and copy to external
#

prepare_dependency_source libserum "${LIBSERUM_SHA}" "https://github.com/PPUC/libserum/archive/${LIBSERUM_SHA}.tar.gz" tar LIBSERUM_SOURCE_DIR
cd libserum
cmake \
   -G "Visual Studio 18 2026" \
   -A Win32 \
   -DPLATFORM=win \
   -DARCH=x86 \
   -DBUILD_SHARED=ON \
   -DBUILD_STATIC=OFF \
   -B build
cmake --build build --config ${BUILD_TYPE}
cp -r third-party/include/lz4 ${PROJECT_SOURCE_ROOT}/third-party/include/
cp src/LZ4Stream.h ${PROJECT_SOURCE_ROOT}/third-party/include/
cp src/SceneGenerator.h ${PROJECT_SOURCE_ROOT}/third-party/include/
cp src/serum.h ${PROJECT_SOURCE_ROOT}/third-party/include/
cp src/TimeUtils.h ${PROJECT_SOURCE_ROOT}/third-party/include/
cp src/serum-decode.h ${PROJECT_SOURCE_ROOT}/third-party/include/
cp build/${BUILD_TYPE}/serum.lib ${PROJECT_SOURCE_ROOT}/third-party/build-libs/win/x86/
cp build/${BUILD_TYPE}/serum.dll ${PROJECT_SOURCE_ROOT}/third-party/runtime-libs/win/x86/
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
   -A Win32 \
   -DPLATFORM=win \
   -DARCH=x86 \
   -DBUILD_SHARED=ON \
   -DBUILD_STATIC=OFF \
   -B build
cmake --build build --config ${BUILD_TYPE}
cp src/pupdmd.h ${PROJECT_SOURCE_ROOT}/third-party/include/
cp build/${BUILD_TYPE}/pupdmd.lib ${PROJECT_SOURCE_ROOT}/third-party/build-libs/win/x86/
cp build/${BUILD_TYPE}/pupdmd.dll ${PROJECT_SOURCE_ROOT}/third-party/runtime-libs/win/x86/
cd ..

#
# build libvni and copy to external
#

prepare_dependency_source libvni "${LIBVNI_SHA}" "https://github.com/PPUC/libvni/archive/${LIBVNI_SHA}.tar.gz" tar LIBVNI_SOURCE_DIR
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
cp src/vni.h ${PROJECT_SOURCE_ROOT}/third-party/include/
cp build/${BUILD_TYPE}/vni.lib ${PROJECT_SOURCE_ROOT}/third-party/build-libs/win/x86/
cp build/${BUILD_TYPE}/vni.dll ${PROJECT_SOURCE_ROOT}/third-party/runtime-libs/win/x86/
cd ..
