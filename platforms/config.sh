#!/bin/bash

set -e

LIBZEDMD_SHA=c09a71c9d3c6c10f5cca341b491e1db9696945ac
LIBSERUM_SHA=d936ba7b33da61e00a5eaccc29968c8fd90792d4
LIBPUPDMD_SHA=64f98eab4713d6c20e11dc552a3147e6de90635d
LIBVNI_SHA=eb910403834184e1d098bcc310054ac17a00b53d
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
