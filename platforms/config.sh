#!/bin/bash

set -e

LIBZEDMD_SHA=1fe8c2e38f13749f1d240924fd5ff928d9c2eb07
LIBSERUM_SHA=3d5a285ab93322fc9f08b25ba5208c8ad88b0b24
LIBPUPDMD_SHA=64f98eab4713d6c20e11dc552a3147e6de90635d
LIBVNI_SHA=c878f7dbfd9e9cec33bcbf0e896f1188a01933b8
LIBUSB_SHA=87a55632db62c9bdc58cd31d3ccfa673f1bb017f

if [ -z "${BUILD_TYPE}" ]; then
   BUILD_TYPE="Release"
fi

if [ -z "${ENABLE_SERUM}" ]; then
   ENABLE_SERUM="ON"
fi

if [ -z "${ENABLE_VNI}" ]; then
   ENABLE_VNI="ON"
fi

if [ -z "${ENABLE_PUP}" ]; then
   ENABLE_PUP="ON"
fi

if [ -z "${ENABLE_PIN2DMD}" ]; then
   ENABLE_PIN2DMD="ON"
fi

echo "Build type: ${BUILD_TYPE}"
echo ""
