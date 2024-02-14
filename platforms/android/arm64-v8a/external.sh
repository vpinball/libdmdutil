#!/bin/bash

set -e

LIBZEDMD_SHA=44a8f03cd2f6c8c358714d871f4c716f447052ec
LIBSERUM_SHA=b69d2b436bc93570a2e7e78d0946cd3c43f7aed5

if [[ $(uname) == "Linux" ]]; then
   NUM_PROCS=$(nproc)
elif [[ $(uname) == "Darwin" ]]; then
   NUM_PROCS=$(sysctl -n hw.ncpu)
else
   NUM_PROCS=1
fi

echo "Building libraries..."
echo "  LIBZEDMD_SHA: ${LIBZEDMD_SHA}"
echo "  LIBSERUM_SHA: ${LIBSERUM_SHA}"
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
cp src/ZeDMD.h ../../third-party/include
platforms/android/arm64-v8a/external.sh
cmake -DPLATFORM=android -DARCH=arm64-v8a -DBUILD_SHARED=ON -DBUILD_STATIC=OFF -DCMAKE_BUILD_TYPE=${BUILD_TYPE} -B build
cmake --build build -- -j${NUM_PROCS}
cp build/libzedmd.so ../../third-party/runtime-libs/android/arm64-v8a
cd ..

#
# build libserum and copy to external
#

curl -sL https://github.com/zesinger/libserum/archive/${LIBSERUM_SHA}.zip -o libserum.zip
unzip libserum.zip
cd libserum-$LIBSERUM_SHA
cp src/serum-decode.h ../../third-party/include
cmake -DPLATFORM=android -DARCH=arm64-v8a -DBUILD_SHARED=ON -DBUILD_STATIC=OFF -DCMAKE_BUILD_TYPE=${BUILD_TYPE} -B build
cmake --build build -- -j${NUM_PROCS}
cp build/libserum.so ../../third-party/runtime-libs/android/arm64-v8a
cd ..