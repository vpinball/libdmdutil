#!/bin/bash

set -e

LIBZEDMD_SHA=a9e856e7cd3fdb3a2a9bd994bd382f68a0b5da18
LIBSERUM_SHA=55939dfe645f6a2e01785bec2cf6d723bf62def4
LIBPUPDMD_SHA=1e2becff70450e0dd52dbaef767f89728d7957cd
LIBVNI_SHA=aec04d88f70c3ef642df9f68c4b41c9418fdf704
LIBUSB_SHA=15a7ebb4d426c5ce196684347d2b7cafad862626

if [ -z "${BUILD_TYPE}" ]; then
   BUILD_TYPE="Release"
fi

echo "Build type: ${BUILD_TYPE}"
echo ""
