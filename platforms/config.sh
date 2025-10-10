#!/bin/bash

set -e

LIBZEDMD_SHA=ab59ccf67bdded2fe88a3f14368a2b4d7fe8a4c6
LIBSERUM_SHA=3dbfd32201c5cf21f1a4d18142b9917a8b81eefa
LIBPUPDMD_SHA=124f45e5ddd59ceb339591de88fcca72f8c54612

if [ -z "${BUILD_TYPE}" ]; then
   BUILD_TYPE="Release"
fi

echo "Build type: ${BUILD_TYPE}"
echo ""