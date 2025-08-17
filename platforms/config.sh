#!/bin/bash

set -e

LIBZEDMD_SHA=154772800e8f36378c629f066bfee563862728ac
LIBSERUM_SHA=a6d43762884b36151ab4e967ebe04cc1a1dde4e8
LIBPUPDMD_SHA=124f45e5ddd59ceb339591de88fcca72f8c54612

if [ -z "${BUILD_TYPE}" ]; then
   BUILD_TYPE="Release"
fi

echo "Build type: ${BUILD_TYPE}"
echo ""