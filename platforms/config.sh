#!/bin/bash

set -e

LIBZEDMD_SHA=78a8ff76e4560d29e9eb76d02913a7601f324872
LIBSERUM_SHA=4996e52b8551ab244f44fedb2499c81c35292fdc
LIBPUPDMD_SHA=124f45e5ddd59ceb339591de88fcca72f8c54612

if [ -z "${BUILD_TYPE}" ]; then
   BUILD_TYPE="Release"
fi

echo "Build type: ${BUILD_TYPE}"
echo ""
