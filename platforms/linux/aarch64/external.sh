#!/bin/bash

set -e

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

NUM_PROCS=$(nproc)


rm -rf external
mkdir -p \
   external \
   third-party/include \
   third-party/build-libs/linux/aarch64 \
   third-party/runtime-libs/linux/aarch64
cd external

#
# build libusb and copy to third-party
#

curl -sL https://github.com/libusb/libusb/archive/${LIBUSB_SHA}.tar.gz -o libusb-${LIBUSB_SHA}.tar.gz
tar xzf libusb-${LIBUSB_SHA}.tar.gz
mv libusb-${LIBUSB_SHA} libusb
cd libusb
./autogen.sh
./configure \
   --enable-shared
make -j${NUM_PROCS}
mkdir -p ${PPUC_SOURCE_ROOT}/third-party/include/libusb-1.0
cp libusb/libusb.h ${PPUC_SOURCE_ROOT}/third-party/include/libusb-1.0
cp -a libusb/.libs/libusb-1.0.{so,so.*} ${PPUC_SOURCE_ROOT}/third-party/runtime-libs/linux/aarch64/
cd ..

#
# build libzedmd and copy to external
#

ppuc_prepare_dependency_source libzedmd "${LIBZEDMD_SHA}" "https://github.com/PPUC/libzedmd/archive/${LIBZEDMD_SHA}.tar.gz"
cd libzedmd
BUILD_TYPE=${BUILD_TYPE} platforms/linux/aarch64/external.sh
cmake \
   -DPLATFORM=linux \
   -DARCH=aarch64 \
   -DBUILD_SHARED=ON \
   -DBUILD_STATIC=OFF \
   -DSPI_SUPPORT=ON \
   -DCMAKE_BUILD_TYPE=${BUILD_TYPE} \
   -B build
cmake --build build -- -j${NUM_PROCS}
cp src/ZeDMD.h ${PPUC_SOURCE_ROOT}/third-party/include/
cp third-party/include/libserialport.h ${PPUC_SOURCE_ROOT}/third-party/include/
cp third-party/include/cargs.h ${PPUC_SOURCE_ROOT}/third-party/include/
cp -r third-party/include/komihash ${PPUC_SOURCE_ROOT}/third-party/include/
cp -r third-party/include/sockpp ${PPUC_SOURCE_ROOT}/third-party/include/
cp third-party/include/FrameUtil.h ${PPUC_SOURCE_ROOT}/third-party/include/
cp third-party/runtime-libs/linux/aarch64/libcargs.so ${PPUC_SOURCE_ROOT}/third-party/runtime-libs/linux/aarch64/
cp -a third-party/runtime-libs/linux/aarch64/libserialport.{so,so.*} ${PPUC_SOURCE_ROOT}/third-party/runtime-libs/linux/aarch64/
cp -a third-party/runtime-libs/linux/aarch64/libsockpp.{so,so.*} ${PPUC_SOURCE_ROOT}/third-party/runtime-libs/linux/aarch64/
cp -a build/libzedmd.{so,so.*} ${PPUC_SOURCE_ROOT}/third-party/runtime-libs/linux/aarch64/
cp -r test ../../
cd ..

#
# build libserum and copy to external
#

ppuc_prepare_dependency_source libserum "${LIBSERUM_SHA}" "https://github.com/PPUC/libserum/archive/${LIBSERUM_SHA}.tar.gz"
cd libserum
cmake \
   -DPLATFORM=linux \
   -DARCH=aarch64 \
   -DBUILD_SHARED=ON \
   -DBUILD_STATIC=OFF \
   -DCMAKE_BUILD_TYPE=${BUILD_TYPE} \
   -B build
cmake --build build -- -j${NUM_PROCS}
cp -r third-party/include/lz4 ${PPUC_SOURCE_ROOT}/third-party/include/
cp src/LZ4Stream.h ${PPUC_SOURCE_ROOT}/third-party/include/
cp src/SceneGenerator.h ${PPUC_SOURCE_ROOT}/third-party/include/
cp src/serum.h ${PPUC_SOURCE_ROOT}/third-party/include/
cp src/TimeUtils.h ${PPUC_SOURCE_ROOT}/third-party/include/
cp src/serum-decode.h ${PPUC_SOURCE_ROOT}/third-party/include/
cp -a build/libserum.{so,so.*} ${PPUC_SOURCE_ROOT}/third-party/runtime-libs/linux/aarch64/
cd ..

#
# build libpupdmd and copy to external
#

curl -sL https://github.com/PPUC/libpupdmd/archive/${LIBPUPDMD_SHA}.tar.gz -o libpupdmd-${LIBPUPDMD_SHA}.tar.gz
tar xzf libpupdmd-${LIBPUPDMD_SHA}.tar.gz
mv libpupdmd-${LIBPUPDMD_SHA} libpupdmd
cd libpupdmd
cmake \
   -DPLATFORM=linux \
   -DARCH=aarch64 \
   -DBUILD_SHARED=ON \
   -DBUILD_STATIC=OFF \
   -DCMAKE_BUILD_TYPE=${BUILD_TYPE} \
   -B build
cmake --build build -- -j${NUM_PROCS}
cp src/pupdmd.h ${PPUC_SOURCE_ROOT}/third-party/include/
cp -a build/libpupdmd.{so,so.*} ${PPUC_SOURCE_ROOT}/third-party/runtime-libs/linux/aarch64/
cd ..

#
# build libvni and copy to external
#

ppuc_prepare_dependency_source libvni "${LIBVNI_SHA}" "https://github.com/PPUC/libvni/archive/${LIBVNI_SHA}.tar.gz"
cd libvni
platforms/linux/aarch64/external.sh
cmake \
   -DPLATFORM=linux \
   -DARCH=aarch64 \
   -DBUILD_SHARED=ON \
   -DBUILD_STATIC=OFF \
   -DCMAKE_BUILD_TYPE=${BUILD_TYPE} \
   -B build
cmake --build build -- -j${NUM_PROCS}
cp src/vni.h ${PPUC_SOURCE_ROOT}/third-party/include/
cp -a build/libvni.{so,so.*} ${PPUC_SOURCE_ROOT}/third-party/runtime-libs/linux/aarch64/
cd ..
