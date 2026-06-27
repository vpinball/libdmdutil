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
ppuc_print_dependency_source LIBZEDMD libzedmd "${LIBZEDMD_SHA}"
echo "  LIBSERUM_SHA: ${LIBSERUM_SHA}"
ppuc_print_dependency_source LIBSERUM libserum "${LIBSERUM_SHA}"
echo "  LIBPUPDMD_SHA: ${LIBPUPDMD_SHA}"
echo "  LIBVNI_SHA: ${LIBVNI_SHA}"
ppuc_print_dependency_source LIBVNI libvni "${LIBVNI_SHA}"
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
mkdir -p ${PPUC_SOURCE_ROOT}/third-party/include/libusb-1.0
cp libusb/libusb.h ${PPUC_SOURCE_ROOT}/third-party/include/libusb-1.0
cp libusb/.libs/libusb-1.0.dll.a ${PPUC_SOURCE_ROOT}/third-party/build-libs/win/x86/libusb-1.0.lib
cp libusb/.libs/libusb-1.0.dll ${PPUC_SOURCE_ROOT}/third-party/runtime-libs/win/x86/
cd ..

#
# build libzedmd and copy to external
#

ppuc_prepare_dependency_source libzedmd "${LIBZEDMD_SHA}" "https://github.com/PPUC/libzedmd/archive/${LIBZEDMD_SHA}.tar.gz"
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
cp src/ZeDMD.h ${PPUC_SOURCE_ROOT}/third-party/include/
cp third-party/include/libserialport.h ${PPUC_SOURCE_ROOT}/third-party/include/
cp third-party/include/cargs.h ${PPUC_SOURCE_ROOT}/third-party/include/
cp -r third-party/include/komihash ${PPUC_SOURCE_ROOT}/third-party/include/
cp -r third-party/include/sockpp ${PPUC_SOURCE_ROOT}/third-party/include/
cp third-party/include/FrameUtil.h ${PPUC_SOURCE_ROOT}/third-party/include/
cp third-party/build-libs/win/x86/cargs.lib ${PPUC_SOURCE_ROOT}/third-party/build-libs/win/x86/
cp third-party/runtime-libs/win/x86/cargs.dll ${PPUC_SOURCE_ROOT}/third-party/runtime-libs/win/x86/
cp third-party/build-libs/win/x86/libserialport.lib ${PPUC_SOURCE_ROOT}/third-party/build-libs/win/x86/
cp third-party/runtime-libs/win/x86/libserialport-0.dll ${PPUC_SOURCE_ROOT}/third-party/runtime-libs/win/x86/
cp third-party/build-libs/win/x86/sockpp.lib ${PPUC_SOURCE_ROOT}/third-party/build-libs/win/x86/
cp third-party/runtime-libs/win/x86/sockpp.dll ${PPUC_SOURCE_ROOT}/third-party/runtime-libs/win/x86/
cp third-party/runtime-libs/win/x86/libgcc_s_dw2-1.dll ${PPUC_SOURCE_ROOT}/third-party/runtime-libs/win/x86/
cp third-party/runtime-libs/win/x86/libstdc++-6.dll ${PPUC_SOURCE_ROOT}/third-party/runtime-libs/win/x86/
cp third-party/runtime-libs/win/x86/libwinpthread-1.dll ${PPUC_SOURCE_ROOT}/third-party/runtime-libs/win/x86/
cp build/${BUILD_TYPE}/zedmd.lib ${PPUC_SOURCE_ROOT}/third-party/build-libs/win/x86/
cp build/${BUILD_TYPE}/zedmd.dll ${PPUC_SOURCE_ROOT}/third-party/runtime-libs/win/x86/
cp -r test ../../
cd ..

#
# build libserum and copy to external
#

ppuc_prepare_dependency_source libserum "${LIBSERUM_SHA}" "https://github.com/PPUC/libserum/archive/${LIBSERUM_SHA}.tar.gz"
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
cp -r third-party/include/lz4 ${PPUC_SOURCE_ROOT}/third-party/include/
cp src/LZ4Stream.h ${PPUC_SOURCE_ROOT}/third-party/include/
cp src/SceneGenerator.h ${PPUC_SOURCE_ROOT}/third-party/include/
cp src/serum.h ${PPUC_SOURCE_ROOT}/third-party/include/
cp src/TimeUtils.h ${PPUC_SOURCE_ROOT}/third-party/include/
cp src/serum-decode.h ${PPUC_SOURCE_ROOT}/third-party/include/
cp build/${BUILD_TYPE}/serum.lib ${PPUC_SOURCE_ROOT}/third-party/build-libs/win/x86/
cp build/${BUILD_TYPE}/serum.dll ${PPUC_SOURCE_ROOT}/third-party/runtime-libs/win/x86/
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
cp src/pupdmd.h ${PPUC_SOURCE_ROOT}/third-party/include/
cp build/${BUILD_TYPE}/pupdmd.lib ${PPUC_SOURCE_ROOT}/third-party/build-libs/win/x86/
cp build/${BUILD_TYPE}/pupdmd.dll ${PPUC_SOURCE_ROOT}/third-party/runtime-libs/win/x86/
cd ..

#
# build libvni and copy to external
#

ppuc_prepare_dependency_source libvni "${LIBVNI_SHA}" "https://github.com/PPUC/libvni/archive/${LIBVNI_SHA}.tar.gz"
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
cp src/vni.h ${PPUC_SOURCE_ROOT}/third-party/include/
cp build/${BUILD_TYPE}/vni.lib ${PPUC_SOURCE_ROOT}/third-party/build-libs/win/x86/
cp build/${BUILD_TYPE}/vni.dll ${PPUC_SOURCE_ROOT}/third-party/runtime-libs/win/x86/
cd ..
