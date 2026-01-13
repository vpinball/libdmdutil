#!/bin/bash

set -e

LIBZEDMD_SHA=cb969273720234df2f11f2fd7c81fe375f83cfe2
LIBSERUM_SHA=75852af0eaa614436d366a5ff5c7283654941b26
LIBPUPDMD_SHA=124f45e5ddd59ceb339591de88fcca72f8c54612

if [ -z "${BUILD_TYPE}" ]; then
   BUILD_TYPE="Release"
fi

echo "Build type: ${BUILD_TYPE}"
echo ""
