#!/bin/bash

set -e

LIBZEDMD_SHA=1d930c3ab3a17433861f9f97900062f1088d1e7b
LIBSERUM_SHA=9beac4e47d83ea2384316150c5b131c467d1ef8d
LIBPUPDMD_SHA=124f45e5ddd59ceb339591de88fcca72f8c54612

if [ -z "${BUILD_TYPE}" ]; then
   BUILD_TYPE="Release"
fi

echo "Build type: ${BUILD_TYPE}"
echo ""
