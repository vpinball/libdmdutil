#!/bin/bash

set -e

LIBZEDMD_SHA=ab59ccf67bdded2fe88a3f14368a2b4d7fe8a4c6
LIBSERUM_SHA=91dd4ca8f51a3a54eb50a3c15b2a1a0615ecfb96
LIBPUPDMD_SHA=124f45e5ddd59ceb339591de88fcca72f8c54612

if [ -z "${BUILD_TYPE}" ]; then
   BUILD_TYPE="Release"
fi

echo "Build type: ${BUILD_TYPE}"
echo ""