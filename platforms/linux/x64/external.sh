#!/bin/bash

set -e

LIBZEDMD_SHA=08e98a858eb6e1394b4844bec7dd27c7c0d9a845
LIBSERUM_SHA=b69d2b436bc93570a2e7e78d0946cd3c43f7aed5
LIBSOCKPP_SHA=4a0d8e087452b5c74179b268c0aceadef90906b9

NUM_PROCS=$(nproc)

echo "Building libraries..."
echo "  LIBZEDMD_SHA: ${LIBZEDMD_SHA}"
echo "  LIBSERUM_SHA: ${LIBSERUM_SHA}"
echo "  LIBSOCKPP_SHA: ${LIBSOCKPP_SHA}"
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

curl -sL https://github.com/PPUC/libzedmd/archive/${LIBZEDMD_SHA}.zip -o libzedmd.zip
unzip libzedmd.zip
cd libzedmd-$LIBZEDMD_SHA
cp src/ZeDMD.h ../../third-party/include/
platforms/linux/x64/external.sh
cmake -DPLATFORM=linux -DARCH=x64 -DBUILD_SHARED=ON -DBUILD_STATIC=OFF -DCMAKE_BUILD_TYPE=${BUILD_TYPE} -B build
cmake --build build -- -j${NUM_PROCS}
cp third-party/include/libserialport.h ../../third-party/include/
ln -s $(ls -v third-party/runtime-libs/linux/x64/libserialport.so.* | tail -n 1) third-party/runtime-libs/linux/x64/libserialport.so
cp -P third-party/runtime-libs/linux/x64/libserialport.{so,so.*} ../../third-party/runtime-libs/linux/x64/
cp -P build/libzedmd.{so,so.*} ../../third-party/runtime-libs/linux/x64/
cp -r test ../../
cd ..

#
# build libserum and copy to external
#

curl -sL https://github.com/zesinger/libserum/archive/${LIBSERUM_SHA}.zip -o libserum.zip
unzip libserum.zip
cd libserum-$LIBSERUM_SHA
cp src/serum-decode.h ../../third-party/include/
cmake -DPLATFORM=linux -DARCH=x64 -DBUILD_SHARED=ON -DBUILD_STATIC=OFF -DCMAKE_BUILD_TYPE=${BUILD_TYPE} -B build
cmake --build build -- -j${NUM_PROCS}
cp -P build/libserum.{so,so.*} ../../third-party/runtime-libs/linux/x64/
cd ..

#
# build libsockpp and copy to external
#

curl -sL https://github.com/fpagliughi/sockpp/archive/${LIBSOCKPP_SHA}.zip -o sockpp.zip
unzip sockpp.zip
cd sockpp-$LIBSOCKPP_SHA
cp -r include/sockpp ../../third-party/include/
cmake -DCMAKE_BUILD_TYPE=${BUILD_TYPE} -B build
cmake --build build -- -j${NUM_PROCS}
cp -P build/libsockpp.{so,so.*} ../../third-party/runtime-libs/linux/x64/
cd ..
