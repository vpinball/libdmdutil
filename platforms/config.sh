#!/bin/bash

set -e

LIBZEDMD_SHA=ec736a2eb938ebccfc1c0c29918f906e920f480a
LIBSERUM_SHA=2da4ca73fb4f30f99b998345ff42cf426180fd57
LIBPUPDMD_SHA=124f45e5ddd59ceb339591de88fcca72f8c54612

if [ -z "${BUILD_TYPE}" ]; then
   BUILD_TYPE="Release"
fi

echo "Build type: ${BUILD_TYPE}"
echo ""
