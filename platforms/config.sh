#!/bin/bash

set -e

LIBZEDMD_SHA=41c84011247dd55650b711e8680ae7088a86b822
LIBSERUM_SHA=c89ed9f6f1abbca6eca6750221c9706c97a67f2b
LIBPUPDMD_SHA=124f45e5ddd59ceb339591de88fcca72f8c54612

if [ -z "${BUILD_TYPE}" ]; then
   BUILD_TYPE="Release"
fi

echo "Build type: ${BUILD_TYPE}"
echo ""
