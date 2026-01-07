#!/bin/bash

set -e

LIBZEDMD_SHA=22cdc59bdb110a13cf860766e39a50acc3a6fc1f
LIBSERUM_SHA=9beac4e47d83ea2384316150c5b131c467d1ef8d
LIBPUPDMD_SHA=124f45e5ddd59ceb339591de88fcca72f8c54612

if [ -z "${BUILD_TYPE}" ]; then
   BUILD_TYPE="Release"
fi

echo "Build type: ${BUILD_TYPE}"
echo ""
