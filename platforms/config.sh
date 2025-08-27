#!/bin/bash

set -e

LIBZEDMD_SHA=542340d5d230ab78a175747a45f6cef415e2c774
LIBSERUM_SHA=ecf1fb6e9e0464593646150ca82ef6d5f177a113
LIBPUPDMD_SHA=124f45e5ddd59ceb339591de88fcca72f8c54612

if [ -z "${BUILD_TYPE}" ]; then
   BUILD_TYPE="Release"
fi

echo "Build type: ${BUILD_TYPE}"
echo ""