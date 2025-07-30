#!/bin/bash

set -e

LIBZEDMD_SHA=154772800e8f36378c629f066bfee563862728ac
LIBSERUM_SHA=9d9d87b1dae4ef230179b5b5d71bd655fab31a95
LIBPUPDMD_SHA=124f45e5ddd59ceb339591de88fcca72f8c54612

if [ -z "${BUILD_TYPE}" ]; then
   BUILD_TYPE="Release"
fi

echo "Build type: ${BUILD_TYPE}"
echo ""