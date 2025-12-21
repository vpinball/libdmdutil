#!/bin/bash

set -e

LIBZEDMD_SHA=eadc9b32a6d5225fc815aee9061c926514dfcb9c
LIBSERUM_SHA=4996e52b8551ab244f44fedb2499c81c35292fdc
LIBPUPDMD_SHA=124f45e5ddd59ceb339591de88fcca72f8c54612

if [ -z "${BUILD_TYPE}" ]; then
   BUILD_TYPE="Release"
fi

echo "Build type: ${BUILD_TYPE}"
echo ""
