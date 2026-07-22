#!/bin/bash

set -e

source ./platforms/config.sh

echo "Building libraries..."
echo "  LIBZEDMD_SHA: ${LIBZEDMD_SHA}"
print_dependency_source LIBZEDMD "${LIBZEDMD_SHA}" LIBZEDMD_SOURCE_DIR
echo "  LIBSERUM_SHA: ${LIBSERUM_SHA}"
print_dependency_source LIBSERUM "${LIBSERUM_SHA}" LIBSERUM_SOURCE_DIR
echo "  LIBPUPDMD_SHA: ${LIBPUPDMD_SHA}"
echo "  LIBVNI_SHA: ${LIBVNI_SHA}"
print_dependency_source LIBVNI "${LIBVNI_SHA}" LIBVNI_SOURCE_DIR
echo ""

NUM_PROCS=$(sysctl -n hw.ncpu)


rm -rf external
mkdir -p \
   external \
   third-party/include \
   third-party/build-libs/ios-simulator/arm64 \
   third-party/runtime-libs/ios-simulator/arm64
cd external

#
# build libzedmd and copy to external
#

prepare_dependency_source libzedmd "${LIBZEDMD_SHA}" "https://github.com/PPUC/libzedmd/archive/${LIBZEDMD_SHA}.tar.gz" tar LIBZEDMD_SOURCE_DIR
cd libzedmd
BUILD_TYPE=${BUILD_TYPE} platforms/ios-simulator/arm64/external.sh
cmake \
   -DPLATFORM=ios-simulator \
   -DARCH=arm64 \
   -DBUILD_SHARED=OFF \
   -DBUILD_STATIC=ON \
   -DCMAKE_BUILD_TYPE=${BUILD_TYPE} \
   -B build
cmake --build build -- -j${NUM_PROCS}
cp src/ZeDMD.h ${PROJECT_SOURCE_ROOT}/third-party/include/
cp -r third-party/include/komihash ${PROJECT_SOURCE_ROOT}/third-party/include/
cp -r third-party/include/sockpp ${PROJECT_SOURCE_ROOT}/third-party/include/
cp third-party/include/FrameUtil.h ${PROJECT_SOURCE_ROOT}/third-party/include/
cp -a third-party/build-libs/ios-simulator/arm64/libsockpp.a ${PROJECT_SOURCE_ROOT}/third-party/build-libs/ios-simulator/arm64/
cp build/libzedmd.a ${PROJECT_SOURCE_ROOT}/third-party/build-libs/ios-simulator/arm64/
cp -r test ../../
cd ..

#
# build libserum and copy to external
#

prepare_dependency_source libserum "${LIBSERUM_SHA}" "https://github.com/PPUC/libserum/archive/${LIBSERUM_SHA}.tar.gz" tar LIBSERUM_SOURCE_DIR
cd libserum
cmake \
   -DPLATFORM=ios-simulator \
   -DARCH=arm64 \
   -DBUILD_SHARED=OFF \
   -DBUILD_STATIC=ON \
   -DCMAKE_BUILD_TYPE=${BUILD_TYPE} \
   -B build
cmake --build build -- -j${NUM_PROCS}
cp -r third-party/include/lz4 ${PROJECT_SOURCE_ROOT}/third-party/include/
cp src/LZ4Stream.h ${PROJECT_SOURCE_ROOT}/third-party/include/
cp src/SceneGenerator.h ${PROJECT_SOURCE_ROOT}/third-party/include/
cp src/serum.h ${PROJECT_SOURCE_ROOT}/third-party/include/
cp src/TimeUtils.h ${PROJECT_SOURCE_ROOT}/third-party/include/
cp src/serum-decode.h ${PROJECT_SOURCE_ROOT}/third-party/include/
cp build/libserum.a ${PROJECT_SOURCE_ROOT}/third-party/build-libs/ios-simulator/arm64/
cd ..

#
# build libpupdmd and copy to external
#

curl -sL https://github.com/PPUC/libpupdmd/archive/${LIBPUPDMD_SHA}.tar.gz -o libpupdmd-${LIBPUPDMD_SHA}.tar.gz
tar xzf libpupdmd-${LIBPUPDMD_SHA}.tar.gz
mv libpupdmd-${LIBPUPDMD_SHA} libpupdmd
cd libpupdmd
cmake \
   -DPLATFORM=ios-simulator \
   -DARCH=arm64 \
   -DBUILD_SHARED=OFF \
   -DBUILD_STATIC=ON \
   -DCMAKE_BUILD_TYPE=${BUILD_TYPE} \
   -B build
cmake --build build -- -j${NUM_PROCS}
cp src/pupdmd.h ${PROJECT_SOURCE_ROOT}/third-party/include/
cp build/libpupdmd.a ${PROJECT_SOURCE_ROOT}/third-party/build-libs/ios-simulator/arm64/
cd ..

#
# build libvni and copy to external
#

prepare_dependency_source libvni "${LIBVNI_SHA}" "https://github.com/PPUC/libvni/archive/${LIBVNI_SHA}.tar.gz" tar LIBVNI_SOURCE_DIR
cd libvni
platforms/ios-simulator/arm64/external.sh
cmake \
   -DPLATFORM=ios-simulator \
   -DARCH=arm64 \
   -DBUILD_SHARED=OFF \
   -DBUILD_STATIC=ON \
   -DCMAKE_BUILD_TYPE=${BUILD_TYPE} \
   -B build
cmake --build build -- -j${NUM_PROCS}
cp src/vni.h ${PROJECT_SOURCE_ROOT}/third-party/include/
cp build/libvni.a ${PROJECT_SOURCE_ROOT}/third-party/build-libs/ios-simulator/arm64/
cd ..
