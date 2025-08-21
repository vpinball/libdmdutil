#!/bin/bash

set -e

LIBZEDMD_SHA=542340d5d230ab78a175747a45f6cef415e2c774
LIBSERUM_SHA=47b08931903aba795bf3c3183b14c6efd6e06bc4
LIBPUPDMD_SHA=124f45e5ddd59ceb339591de88fcca72f8c54612

if [ -z "${BUILD_TYPE}" ]; then
   BUILD_TYPE="Release"
fi

echo "Build type: ${BUILD_TYPE}"
echo ""