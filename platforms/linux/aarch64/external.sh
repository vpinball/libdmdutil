#!/bin/bash

set -e

LIBCARGS_SHA=5949a20a926e902931de4a32adaad9f19c76f251
LIBINICPP_SHA=138ae81911379e79bf1a9be86726cb77af4a57b0
LIBZEDMD_SHA=08e98a858eb6e1394b4844bec7dd27c7c0d9a845
LIBSERUM_SHA=b69d2b436bc93570a2e7e78d0946cd3c43f7aed5
LIBSOCKPP_SHA=e6c4688a576d95f42dd7628cefe68092f6c5cd0f

NUM_PROCS=$(nproc)

echo "Building libraries..."
echo "  LIBCARGS_SHA: ${LIBCARGS_SHA}"
echo "  LIBINICPP_SHA: ${LIBINICPP_SHA}"
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
# libcargs
#

curl -sL https://github.com/likle/cargs/archive/${LIBCARGS_SHA}.zip -o cargs.zip
unzip cargs.zip
cd cargs-${LIBCARGS_SHA}
cp include/cargs.h ../../third-party/include/
mkdir build
cd build
cmake -DBUILD_SHARED_LIBS=ON ..
make
cp -P libcargs.so* ../../../third-party/runtime-libs/linux/aarch64/
cd ../..

#
# libini-cpp
#

curl -sL https://github.com/SSARCandy/ini-cpp/archive/${LIBINICPP_SHA}.zip -o ini-cpp.zip
unzip ini-cpp.zip
cd ini-cpp-${LIBINICPP_SHA}
cp ini/ini.h ../../third-party/include/
cd ..

#
# build libzedmd and copy to external
#

curl -sL https://github.com/PPUC/libzedmd/archive/${LIBZEDMD_SHA}.zip -o libzedmd.zip
unzip libzedmd.zip
cd libzedmd-$LIBZEDMD_SHA
cp src/ZeDMD.h ../../third-party/include/
platforms/linux/aarch64/external.sh
cmake -DPLATFORM=linux -DARCH=aarch64 -DBUILD_SHARED=ON -DBUILD_STATIC=OFF -DCMAKE_BUILD_TYPE=${BUILD_TYPE} -B build
cmake --build build -- -j${NUM_PROCS}
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
cp src/serum-decode.h ../../third-party/include/
cmake -DPLATFORM=linux -DARCH=aarch64 -DBUILD_SHARED=ON -DBUILD_STATIC=OFF -DCMAKE_BUILD_TYPE=${BUILD_TYPE} -B build
cmake --build build -- -j${NUM_PROCS}
cp -a build/libserum.{so,so.*} ../../third-party/runtime-libs/linux/aarch64/
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
cp -a build/libsockpp.{so,so.*} ../../third-party/runtime-libs/linux/aarch64/
cd ..
