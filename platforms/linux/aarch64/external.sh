#!/bin/bash

set -e

CARGS_SHA=5949a20a926e902931de4a32adaad9f19c76f251
LIBZEDMD_SHA=42d95ed6f1fe2065ecbd247502d177d7e5eb7e4c
LIBSERUM_SHA=b69d2b436bc93570a2e7e78d0946cd3c43f7aed5
SOCKPP_SHA=e6c4688a576d95f42dd7628cefe68092f6c5cd0f
LIBPUPDMD_SHA=de1401a521d0f88e2f5d182dfb14f7ce78d68679

NUM_PROCS=$(nproc)

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
echo "Procs: ${NUM_PROCS}"
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
   -DBUILD_SHARED_LIBS=ON \
   -DCMAKE_BUILD_TYPE=${BUILD_TYPE} \
   -B build
cmake --build build -- -j${NUM_PROCS}
cp include/cargs.h ../../third-party/include/
cp -a build/*.so ../../third-party/runtime-libs/linux/aarch64/
cd ..

#
# build libzedmd and copy to external
#

curl -sL https://github.com/PPUC/libzedmd/archive/${LIBZEDMD_SHA}.zip -o libzedmd.zip
unzip libzedmd.zip
cd libzedmd-$LIBZEDMD_SHA
platforms/linux/aarch64/external.sh
cmake \
   -DPLATFORM=linux \
   -DARCH=aarch64 \
   -DBUILD_SHARED=ON \
   -DBUILD_STATIC=OFF \
   -DCMAKE_BUILD_TYPE=${BUILD_TYPE} \
   -B build
cmake --build build -- -j${NUM_PROCS}
cp src/ZeDMD.h ../../third-party/include/
cp third-party/include/libserialport.h ../../third-party/include/
ln -s $(ls -v third-party/runtime-libs/linux/aarch64/libserialport.so.* | tail -n 1 | xargs basename) third-party/runtime-libs/linux/aarch64/libserialport.so
cp -a third-party/runtime-libs/linux/aarch64/libserialport.{so,so.*} ../../third-party/runtime-libs/linux/aarch64/
cp -a build/libzedmd.{so,so.*} ../../third-party/runtime-libs/linux/aarch64/
cp -r test ../../
cd ..

#
# build libserum and copy to external
#

curl -sL https://github.com/zesinger/libserum/archive/${LIBSERUM_SHA}.zip -o libserum.zip
unzip libserum.zip
cd libserum-$LIBSERUM_SHA
cmake \
   -DPLATFORM=linux \
   -DARCH=aarch64 \
   -DBUILD_SHARED=ON \
   -DBUILD_STATIC=OFF \
   -DCMAKE_BUILD_TYPE=${BUILD_TYPE} \
   -B build
cmake --build build -- -j${NUM_PROCS}
cp src/serum-decode.h ../../third-party/include/
cp -a build/libserum.{so,so.*} ../../third-party/runtime-libs/linux/aarch64/
cd ..

#
# build sockpp and copy to external
#

curl -sL https://github.com/fpagliughi/sockpp/archive/${SOCKPP_SHA}.zip -o sockpp.zip
unzip sockpp.zip
cd sockpp-$SOCKPP_SHA
cmake \
   -DCMAKE_BUILD_TYPE=${BUILD_TYPE} \
   -B build
cmake --build build -- -j${NUM_PROCS}
cp -r include/sockpp ../../third-party/include/
cp -a build/libsockpp.{so,so.*} ../../third-party/runtime-libs/linux/aarch64/
cd ..

#
# build libpupdmd and copy to external
#

curl -sL https://github.com/ppuc/libpupdmd/archive/${LIBPUPDMD_SHA}.zip -o libpupdmd.zip
unzip libpupdmd.zip
cd libpupdmd-$LIBPUPDMD_SHA
cmake \
   -DPLATFORM=linux \
   -DARCH=aarch64 \
   -DBUILD_SHARED=ON \
   -DBUILD_STATIC=OFF \
   -DCMAKE_BUILD_TYPE=${BUILD_TYPE} \
   -B build
cmake --build build -- -j${NUM_PROCS}
cp src/pupdmd.h ../../third-party/include/
cp -a build/libpupdmd.{so,so.*} ../../third-party/runtime-libs/linux/aarch64/
cd ..
