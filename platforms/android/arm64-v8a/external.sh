#!/bin/bash

set -e

source ./platforms/config.sh

echo "Building libraries..."
echo "  LIBZEDMD_SHA: ${LIBZEDMD_SHA}"
ppuc_print_dependency_source LIBZEDMD libzedmd "${LIBZEDMD_SHA}"
echo "  LIBSERUM_SHA: ${LIBSERUM_SHA}"
ppuc_print_dependency_source LIBSERUM libserum "${LIBSERUM_SHA}"
echo "  LIBPUPDMD_SHA: ${LIBPUPDMD_SHA}"
echo "  LIBVNI_SHA: ${LIBVNI_SHA}"
ppuc_print_dependency_source LIBVNI libvni "${LIBVNI_SHA}"
echo ""

if [[ $(uname) == "Linux" ]]; then
   NUM_PROCS=$(nproc)
elif [[ $(uname) == "Darwin" ]]; then
   NUM_PROCS=$(sysctl -n hw.ncpu)
else
   NUM_PROCS=1
fi


rm -rf external
mkdir -p \
   external \
   third-party/include \
   third-party/build-libs/android/arm64-v8a \
   third-party/runtime-libs/android/arm64-v8a
cd external

#
# build libzedmd and copy to external
#

ppuc_prepare_dependency_source libzedmd "${LIBZEDMD_SHA}" "https://github.com/PPUC/libzedmd/archive/${LIBZEDMD_SHA}.tar.gz"
cd libzedmd
BUILD_TYPE=${BUILD_TYPE} platforms/android/arm64-v8a/external.sh
cmake \
   -DPLATFORM=android \
   -DARCH=arm64-v8a \
   -DBUILD_SHARED=ON \
   -DBUILD_STATIC=OFF \
   -DCMAKE_BUILD_TYPE=${BUILD_TYPE} \
   -B build
cmake --build build -- -j${NUM_PROCS}
cp src/ZeDMD.h ${PPUC_SOURCE_ROOT}/third-party/include/
cp -r third-party/include/komihash ${PPUC_SOURCE_ROOT}/third-party/include/
cp -r third-party/include/sockpp ${PPUC_SOURCE_ROOT}/third-party/include/
cp third-party/include/FrameUtil.h ${PPUC_SOURCE_ROOT}/third-party/include/
cp third-party/runtime-libs/android/arm64-v8a/libsockpp.so ${PPUC_SOURCE_ROOT}/third-party/runtime-libs/android/arm64-v8a/
cp build/libzedmd.so ${PPUC_SOURCE_ROOT}/third-party/runtime-libs/android/arm64-v8a/
cp -r test ../../
cd ..

#
# build libserum and copy to external
#

ppuc_prepare_dependency_source libserum "${LIBSERUM_SHA}" "https://github.com/PPUC/libserum/archive/${LIBSERUM_SHA}.tar.gz"
cd libserum
cmake \
   -DPLATFORM=android \
   -DARCH=arm64-v8a \
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
cp build/libserum.so ${PPUC_SOURCE_ROOT}/third-party/runtime-libs/android/arm64-v8a/
cd ..

#
# build libpupdmd and copy to external
#

curl -sL https://github.com/PPUC/libpupdmd/archive/${LIBPUPDMD_SHA}.tar.gz -o libpupdmd-${LIBPUPDMD_SHA}.tar.gz
tar xzf libpupdmd-${LIBPUPDMD_SHA}.tar.gz
mv libpupdmd-${LIBPUPDMD_SHA} libpupdmd
cd libpupdmd
cmake -DPLATFORM=android \
   -DARCH=arm64-v8a \
   -DBUILD_SHARED=ON \
   -DBUILD_STATIC=OFF \
   -DCMAKE_BUILD_TYPE=${BUILD_TYPE} \
   -B build
cmake --build build -- -j${NUM_PROCS}
cp src/pupdmd.h ${PPUC_SOURCE_ROOT}/third-party/include/
cp build/libpupdmd.so ${PPUC_SOURCE_ROOT}/third-party/runtime-libs/android/arm64-v8a/
cd ..

#
# build libvni and copy to external
#

ppuc_prepare_dependency_source libvni "${LIBVNI_SHA}" "https://github.com/PPUC/libvni/archive/${LIBVNI_SHA}.tar.gz"
cd libvni
platforms/android/arm64-v8a/external.sh
cmake \
   -DPLATFORM=android \
   -DARCH=arm64-v8a \
   -DBUILD_SHARED=ON \
   -DBUILD_STATIC=OFF \
   -DCMAKE_BUILD_TYPE=${BUILD_TYPE} \
   -B build
cmake --build build -- -j${NUM_PROCS}
cp src/vni.h ${PPUC_SOURCE_ROOT}/third-party/include/
cp build/libvni.so ${PPUC_SOURCE_ROOT}/third-party/runtime-libs/android/arm64-v8a/
cd ..
