#!/bin/bash

set -e

LIBZEDMD_SHA=41c84011247dd55650b711e8680ae7088a86b822
LIBSERUM_SHA=3c74edc68e42b6bb2f39b5408b18624b9c15ad1e
LIBPUPDMD_SHA=124f45e5ddd59ceb339591de88fcca72f8c54612

if [ -z "${BUILD_TYPE}" ]; then
   BUILD_TYPE="Release"
fi

echo "Build type: ${BUILD_TYPE}"
echo ""