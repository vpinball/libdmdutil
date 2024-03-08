#!/bin/bash

set -e

LIBZEDMD_SHA=08e98a858eb6e1394b4844bec7dd27c7c0d9a845
LIBSERUM_SHA=b69d2b436bc93570a2e7e78d0946cd3c43f7aed5
LIBSOCKPP_SHA=e6c4688a576d95f42dd7628cefe68092f6c5cd0f

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
platforms/android/arm64-v8a/external.sh
cmake -DPLATFORM=android -DARCH=arm64-v8a -DBUILD_SHARED=ON -DBUILD_STATIC=OFF -DCMAKE_BUILD_TYPE=${BUILD_TYPE} -B build
cmake --build build -- -j${NUM_PROCS}
cp build/libzedmd.so ../../third-party/runtime-libs/android/arm64-v8a/
cp -r test ../../
cd ..

#
# build libserum and copy to external
#

curl -sL https://github.com/zesinger/libserum/archive/${LIBSERUM_SHA}.zip -o libserum.zip
unzip libserum.zip
cd libserum-$LIBSERUM_SHA
cp src/serum-decode.h ../../third-party/include/
cmake -DPLATFORM=android -DARCH=arm64-v8a -DBUILD_SHARED=ON -DBUILD_STATIC=OFF -DCMAKE_BUILD_TYPE=${BUILD_TYPE} -B build
cmake --build build -- -j${NUM_PROCS}
cp build/libserum.so ../../third-party/runtime-libs/android/arm64-v8a/
cd ..

#
# build libsockpp and copy to external
#

curl -sL https://github.com/fpagliughi/sockpp/archive/${LIBSOCKPP_SHA}.zip -o sockpp.zip
unzip sockpp.zip
cd sockpp-$LIBSOCKPP_SHA
patch -p1 < ../../platforms/android/arm64-v8a/sockpp/001.patch
cp -r include/sockpp ../../third-party/include/
cmake -DSOCKPP_BUILD_SHARED=ON \
   -DSOCKPP_BUILD_STATIC=OFF \
   -DCMAKE_SYSTEM_NAME=Android \
   -DCMAKE_SYSTEM_VERSION=30 \
   -DCMAKE_ANDROID_ARCH_ABI=arm64-v8a \
   -DCMAKE_BUILD_WITH_INSTALL_RPATH=TRUE \
   -DCMAKE_INSTALL_RPATH="\$ORIGIN" \
   -DCMAKE_BUILD_TYPE=${BUILD_TYPE} \
   -B build
cmake --build build -- -j${NUM_PROCS}
cp build/libsockpp.so ../../third-party/runtime-libs/android/arm64-v8a/
cd ..
