#!/bin/bash

set -e

LIBZEDMD_SHA=ab59ccf67bdded2fe88a3f14368a2b4d7fe8a4c6
LIBSERUM_SHA=7bda96192623787ee757ead39e1806b5230a4891
LIBPUPDMD_SHA=124f45e5ddd59ceb339591de88fcca72f8c54612

if [ -z "${BUILD_TYPE}" ]; then
   BUILD_TYPE="Release"
fi

echo "Build type: ${BUILD_TYPE}"
echo ""