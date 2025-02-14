#!/bin/bash

set -e

LIBZEDMD_SHA=4487023aef5019f3b0ce6814834603fa758abcf1
LIBSERUM_SHA=b0cc2a871d9d5b6395658c56c65402ae388eb78c
LIBPUPDMD_SHA=124f45e5ddd59ceb339591de88fcca72f8c54612

NUM_PROCS=$(nproc)

echo "Building libraries..."
echo "  LIBZEDMD_SHA: ${LIBZEDMD_SHA}"
echo "  LIBSERUM_SHA: ${LIBSERUM_SHA}"
echo "  LIBPUPDMD_SHA: ${LIBPUPDMD_SHA}"
echo ""

if [ -z "${BUILD_TYPE}" ]; then
   BUILD_TYPE="Release"
fi

echo "Build type: ${BUILD_TYPE}"
echo "Procs: ${NUM_PROCS}"
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
BUILD_TYPE=${BUILD_TYPE} platforms/linux/x64/external.sh
cmake \
   -DPLATFORM=linux \
   -DARCH=x64 \
   -DBUILD_SHARED=ON \
   -DBUILD_STATIC=OFF \
   -DCMAKE_BUILD_TYPE=${BUILD_TYPE} \
   -B build
cmake --build build -- -j${NUM_PROCS}
cp src/ZeDMD.h ../../third-party/include/
cp third-party/include/libserialport.h ../../third-party/include/
cp third-party/include/cargs.h ../../third-party/include/
cp -r third-party/include/sockpp ../../third-party/include/
cp third-party/include/FrameUtil.h ../../third-party/include/
cp -a third-party/runtime-libs/linux/x64/*.{so,so.*} ../../third-party/runtime-libs/linux/x64/
cp -a build/*.{so,so.*} ../../third-party/runtime-libs/linux/x64/
cp -r test ../../
cd ..

#
# build libserum and copy to external
#

curl -sL https://github.com/zesinger/libserum/archive/${LIBSERUM_SHA}.tar.gz -o libserum-${LIBSERUM_SHA}.tar.gz
tar xzf libserum-${LIBSERUM_SHA}.tar.gz
mv libserum-${LIBSERUM_SHA} libserum
cd libserum
cmake \
   -DPLATFORM=linux \
   -DARCH=x64 \
   -DBUILD_SHARED=ON \
   -DBUILD_STATIC=OFF \
   -DCMAKE_BUILD_TYPE=${BUILD_TYPE} \
   -B build
cmake --build build -- -j${NUM_PROCS}
cp src/serum.h ../../third-party/include/
cp src/serum-decode.h ../../third-party/include/
cp -a build/libserum.{so,so.*} ../../third-party/runtime-libs/linux/x64/
cd ..

#
# build libpupdmd and copy to external
#

curl -sL https://github.com/ppuc/libpupdmd/archive/${LIBPUPDMD_SHA}.tar.gz -o libpupdmd-${LIBPUPDMD_SHA}.tar.gz
tar xzf libpupdmd-${LIBPUPDMD_SHA}.tar.gz
mv libpupdmd-${LIBPUPDMD_SHA} libpupdmd
cd libpupdmd
cmake \
   -DPLATFORM=linux \
   -DARCH=x64 \
   -DBUILD_SHARED=ON \
   -DBUILD_STATIC=OFF \
   -DCMAKE_BUILD_TYPE=${BUILD_TYPE} \
   -B build
cmake --build build -- -j${NUM_PROCS}
cp src/pupdmd.h ../../third-party/include/
cp -a build/libpupdmd.{so,so.*} ../../third-party/runtime-libs/linux/x64/
cd ..
