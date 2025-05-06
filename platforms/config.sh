#!/bin/bash

set -e

LIBZEDMD_SHA=154772800e8f36378c629f066bfee563862728ac
LIBSERUM_SHA=b6f7ea24d1d0145ccda155f779af49a58bbdab8b
LIBPUPDMD_SHA=124f45e5ddd59ceb339591de88fcca72f8c54612

if [ -z "${BUILD_TYPE}" ]; then
   BUILD_TYPE="Release"
fi

echo "Build type: ${BUILD_TYPE}"
echo ""