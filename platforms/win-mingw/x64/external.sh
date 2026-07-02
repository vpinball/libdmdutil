#!/bin/bash

set -e

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

NUM_PROCS=$(nproc)


rm -rf external
mkdir -p \
   external \
   third-party/include \
   third-party/build-libs/win-mingw/x64 \
   third-party/runtime-libs/win-mingw/x64
cd external

#
# build libusb and copy to third-party
#

curl -sL https://github.com/libusb/libusb/archive/${LIBUSB_SHA}.tar.gz -o libusb-${LIBUSB_SHA}.tar.gz
tar xzf libusb-${LIBUSB_SHA}.tar.gz
mv libusb-${LIBUSB_SHA} libusb
cd libusb
sed -i.bak 's/libusb-1\.0/libusb64-1.0/g' libusb/Makefile.am
sed -i.bak 's/libusb_1_0/libusb64_1_0/g' libusb/Makefile.am
mv libusb/libusb-1.0.def libusb/libusb64-1.0.def
mv libusb/libusb-1.0.rc libusb/libusb64-1.0.rc
sed -i.bak 's/libusb-1\.0/libusb64-1.0/g' libusb/libusb64-1.0.def
sed -i.bak 's/libusb-1\.0/libusb64-1.0/g' libusb/libusb64-1.0.rc
./autogen.sh
./configure \
   --enable-shared
make -j${NUM_PROCS}
mkdir -p ${PROJECT_SOURCE_ROOT}/third-party/include/libusb-1.0
cp libusb/libusb.h ${PROJECT_SOURCE_ROOT}/third-party/include/libusb-1.0
cp libusb/.libs/libusb64-1.0.dll.a ${PROJECT_SOURCE_ROOT}/third-party/build-libs/win-mingw/x64/
cp libusb/.libs/libusb64-1.0.dll ${PROJECT_SOURCE_ROOT}/third-party/runtime-libs/win-mingw/x64/
cd ..

#
# build libzedmd and copy to external
#

prepare_dependency_source libzedmd "${LIBZEDMD_SHA}" "https://github.com/PPUC/libzedmd/archive/${LIBZEDMD_SHA}.tar.gz" tar LIBZEDMD_SOURCE_DIR
cd libzedmd
BUILD_TYPE=${BUILD_TYPE} platforms/win-mingw/x64/external.sh
cmake \
   -DCMAKE_BUILD_TYPE=${BUILD_TYPE} \
   -DPLATFORM=win-mingw \
   -DARCH=x64 \
   -DBUILD_SHARED=ON \
   -DBUILD_STATIC=OFF \
   -B build
cmake --build build -- -j${NUM_PROCS}
cp src/ZeDMD.h ${PROJECT_SOURCE_ROOT}/third-party/include/
cp third-party/include/cargs.h ${PROJECT_SOURCE_ROOT}/third-party/include/
cp -r third-party/include/komihash ${PROJECT_SOURCE_ROOT}/third-party/include/
cp -r third-party/include/sockpp ${PROJECT_SOURCE_ROOT}/third-party/include/
cp third-party/include/FrameUtil.h ${PROJECT_SOURCE_ROOT}/third-party/include/
cp third-party/include/libserialport.h ${PROJECT_SOURCE_ROOT}/third-party/include/
cp third-party/build-libs/win-mingw/x64/libcargs64.dll.a ${PROJECT_SOURCE_ROOT}/third-party/build-libs/win-mingw/x64/
cp third-party/runtime-libs/win-mingw/x64/libcargs64.dll ${PROJECT_SOURCE_ROOT}/third-party/runtime-libs/win-mingw/x64/
cp third-party/build-libs/win-mingw/x64/libserialport64.dll.a ${PROJECT_SOURCE_ROOT}/third-party/build-libs/win-mingw/x64/
cp third-party/runtime-libs/win-mingw/x64/libserialport64-0.dll ${PROJECT_SOURCE_ROOT}/third-party/runtime-libs/win-mingw/x64/
cp third-party/build-libs/win-mingw/x64/libsockpp64.dll.a ${PROJECT_SOURCE_ROOT}/third-party/build-libs/win-mingw/x64/
cp third-party/runtime-libs/win-mingw/x64/libsockpp64.dll ${PROJECT_SOURCE_ROOT}/third-party/runtime-libs/win-mingw/x64/
cp build/zedmd64.dll.a ${PROJECT_SOURCE_ROOT}/third-party/build-libs/win-mingw/x64/
cp build/zedmd64.dll ${PROJECT_SOURCE_ROOT}/third-party/runtime-libs/win-mingw/x64/
cp -r test ../../
cd ..

#
# build libserum and copy to external
#

prepare_dependency_source libserum "${LIBSERUM_SHA}" "https://github.com/PPUC/libserum/archive/${LIBSERUM_SHA}.tar.gz" tar LIBSERUM_SOURCE_DIR
cd libserum
cmake \
   -DCMAKE_BUILD_TYPE=${BUILD_TYPE} \
   -DPLATFORM=win-mingw \
   -DARCH=x64 \
   -DBUILD_SHARED=ON \
   -DBUILD_STATIC=OFF \
   -B build
cmake --build build -- -j${NUM_PROCS}
cp -r third-party/include/lz4 ${PROJECT_SOURCE_ROOT}/third-party/include/
cp src/LZ4Stream.h ${PROJECT_SOURCE_ROOT}/third-party/include/
cp src/SceneGenerator.h ${PROJECT_SOURCE_ROOT}/third-party/include/
cp src/serum.h ${PROJECT_SOURCE_ROOT}/third-party/include/
cp src/TimeUtils.h ${PROJECT_SOURCE_ROOT}/third-party/include/
cp src/serum-decode.h ${PROJECT_SOURCE_ROOT}/third-party/include/
cp build/serum64.dll.a ${PROJECT_SOURCE_ROOT}/third-party/build-libs/win-mingw/x64/
cp build/serum64.dll ${PROJECT_SOURCE_ROOT}/third-party/runtime-libs/win-mingw/x64/
cd ..

#
# build libpupdmd and copy to external
#

curl -sL https://github.com/PPUC/libpupdmd/archive/${LIBPUPDMD_SHA}.tar.gz -o libpupdmd-${LIBPUPDMD_SHA}.tar.gz
tar xzf libpupdmd-${LIBPUPDMD_SHA}.tar.gz
mv libpupdmd-${LIBPUPDMD_SHA} libpupdmd
cd libpupdmd
cmake \
   -DCMAKE_BUILD_TYPE=${BUILD_TYPE} \
   -DPLATFORM=win-mingw \
   -DARCH=x64 \
   -DBUILD_SHARED=ON \
   -DBUILD_STATIC=OFF \
   -B build
cmake --build build -- -j${NUM_PROCS}
cp src/pupdmd.h ${PROJECT_SOURCE_ROOT}/third-party/include/
cp build/pupdmd64.dll.a ${PROJECT_SOURCE_ROOT}/third-party/build-libs/win-mingw/x64/
cp build/pupdmd64.dll ${PROJECT_SOURCE_ROOT}/third-party/runtime-libs/win-mingw/x64/
cd ..

#
# build libvni and copy to external
#

prepare_dependency_source libvni "${LIBVNI_SHA}" "https://github.com/PPUC/libvni/archive/${LIBVNI_SHA}.tar.gz" tar LIBVNI_SOURCE_DIR
cd libvni
platforms/win-mingw/x64/external.sh
cmake \
   -DCMAKE_BUILD_TYPE=${BUILD_TYPE} \
   -DPLATFORM=win-mingw \
   -DARCH=x64 \
   -DBUILD_SHARED=ON \
   -DBUILD_STATIC=OFF \
   -B build
cmake --build build -- -j${NUM_PROCS}
cp src/vni.h ${PROJECT_SOURCE_ROOT}/third-party/include/
cp build/vni64.dll.a ${PROJECT_SOURCE_ROOT}/third-party/build-libs/win-mingw/x64/
cp build/vni64.dll ${PROJECT_SOURCE_ROOT}/third-party/runtime-libs/win-mingw/x64/
cd ..

#
# copy UCRT64 runtime DLLs
#

UCRT64_BIN="${MINGW_PREFIX}/bin"

cp "${UCRT64_BIN}/libgcc_s_seh-1.dll" ${PROJECT_SOURCE_ROOT}/third-party/runtime-libs/win-mingw/x64/
cp "${UCRT64_BIN}/libstdc++-6.dll" ${PROJECT_SOURCE_ROOT}/third-party/runtime-libs/win-mingw/x64/
cp "${UCRT64_BIN}/libwinpthread-1.dll" ${PROJECT_SOURCE_ROOT}/third-party/runtime-libs/win-mingw/x64/
