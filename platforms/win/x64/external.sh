#!/bin/bash

set -e

LIBCARGS_SHA=5949a20a926e902931de4a32adaad9f19c76f251
LIBINICPP_SHA=138ae81911379e79bf1a9be86726cb77af4a57b0
LIBZEDMD_SHA=08e98a858eb6e1394b4844bec7dd27c7c0d9a845
LIBSERUM_SHA=b69d2b436bc93570a2e7e78d0946cd3c43f7aed5
LIBSOCKPP_SHA=e6c4688a576d95f42dd7628cefe68092f6c5cd0f

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
patch -p1 < ../../platforms/win/x64/cargs/001.patch
cp include/cargs.h ../../third-party/include/
cmake -G "Visual Studio 17 2022" -DBUILD_SHARED_LIBS=ON -B build
cmake --build build --config ${BUILD_TYPE}
cp build/${BUILD_TYPE}/cargs64.lib ../../third-party/build-libs/win/x64/
cp build/${BUILD_TYPE}/cargs64.dll ../../third-party/runtime-libs/win/x64/
cd ..

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
platforms/win/x64/external.sh
cmake -G "Visual Studio 17 2022" -DPLATFORM=win -DARCH=x64 -DBUILD_SHARED=ON -DBUILD_STATIC=OFF -B build
cmake --build build --config ${BUILD_TYPE}
cp third-party/include/libserialport.h ../../third-party/include/
cp third-party/build-libs/win/x64/libserialport64.lib ../../third-party/build-libs/win/x64/
cp third-party/runtime-libs/win/x64/libserialport64.dll ../../third-party/runtime-libs/win/x64/
cp build/${BUILD_TYPE}/zedmd64.lib ../../third-party/build-libs/win/x64/
cp build/${BUILD_TYPE}/zedmd64.dll ../../third-party/runtime-libs/win/x64/
cp -r test ../../
cd ..

#
# build libserum and copy to external
#

curl -sL https://github.com/zesinger/libserum/archive/${LIBSERUM_SHA}.zip -o libserum.zip
unzip libserum.zip
cd libserum-$LIBSERUM_SHA
cp src/serum-decode.h ../../third-party/include/
cmake -G "Visual Studio 17 2022" -DPLATFORM=win -DARCH=x64 -DBUILD_SHARED=ON -DBUILD_STATIC=OFF -B build
cmake --build build --config ${BUILD_TYPE}
cp build/${BUILD_TYPE}/serum64.lib ../../third-party/build-libs/win/x64/
cp build/${BUILD_TYPE}/serum64.dll ../../third-party/runtime-libs/win/x64/
cd ..

#
# build libsockpp and copy to external
#

curl -sL https://github.com/fpagliughi/sockpp/archive/${LIBSOCKPP_SHA}.zip -o sockpp.zip
unzip sockpp.zip
cd sockpp-$LIBSOCKPP_SHA
patch -p1 < ../../platforms/win/x64/sockpp/001.patch
cp -r include/sockpp ../../third-party/include/
cmake -G "Visual Studio 17 2022" -B build
cmake --build build --config ${BUILD_TYPE}
cp build/${BUILD_TYPE}/sockpp64.lib ../../third-party/build-libs/win/x64/
cp build/${BUILD_TYPE}/sockpp64.dll ../../third-party/runtime-libs/win/x64/
cd ..
