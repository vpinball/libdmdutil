#!/bin/bash

set -e

LIBZEDMD_SHA=dc6613ddfabece90dcb345001aa5ff313d40a1e1
LIBSERUM_SHA=ea1d6a31e60e0708d04ccd05d60fdaaef72a4448
LIBPUPDMD_SHA=cd186754ba0dcc1ea418d5557d59d7bf2ed628a3
LIBVNI_SHA=ba43c5abff7fbbb831a4beb9be54447df1532f0c
LIBUSB_SHA=15a7ebb4d426c5ce196684347d2b7cafad862626

if [ -z "${BUILD_TYPE}" ]; then
   BUILD_TYPE="Release"
fi

echo "Build type: ${BUILD_TYPE}"
echo ""
