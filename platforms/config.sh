#!/bin/bash

set -e

LIBZEDMD_SHA=cb969273720234df2f11f2fd7c81fe375f83cfe2
LIBSERUM_SHA=2da4ca73fb4f30f99b998345ff42cf426180fd57
LIBPUPDMD_SHA=124f45e5ddd59ceb339591de88fcca72f8c54612

if [ -z "${BUILD_TYPE}" ]; then
   BUILD_TYPE="Release"
fi

echo "Build type: ${BUILD_TYPE}"
echo ""
