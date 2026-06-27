#!/bin/bash

set -e

LIBZEDMD_SHA=5c44646f2af4b1419b4cdcaed3a2799ca9439221
LIBSERUM_SHA=21b28325c4272724e719ab2d17481d851eaf9fd8
LIBPUPDMD_SHA=4a1123220e6dce73c87cc584494df2ac82cb6f4c
LIBVNI_SHA=7258e2fa0d086e1224d6510d44a61879e6b344b1
LIBUSB_SHA=15a7ebb4d426c5ce196684347d2b7cafad862626

PPUC_SOURCE_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
PPUC_LOCAL_DEPS_ROOT="${PPUC_LOCAL_DEPS_ROOT:-$(cd "${PPUC_SOURCE_ROOT}/.." && pwd)}"
PPUC_USE_LOCAL_DEPS="${PPUC_USE_LOCAL_DEPS:-1}"

ppuc_local_dependency_dir() {
   local name="$1"
   local dir="${PPUC_LOCAL_DEPS_ROOT}/${name}"

   if [ "${PPUC_USE_LOCAL_DEPS}" != "0" ] && [ -d "${dir}" ] && [ "${dir}" != "${PPUC_SOURCE_ROOT}" ]; then
      echo "${dir}"
   fi
}

ppuc_print_dependency_source() {
   local label="$1"
   local name="$2"
   local sha="$3"
   local local_dir

   local_dir="$(ppuc_local_dependency_dir "${name}")"
   if [ -n "${local_dir}" ]; then
      echo "  ${label}_SOURCE: local ${local_dir}"
   else
      echo "  ${label}_SOURCE: archive ${sha}"
   fi
}

ppuc_prepare_dependency_source() {
   local name="$1"
   local sha="$2"
   local url="$3"
   local archive_type="${4:-tar}"
   local local_dir

   local_dir="$(ppuc_local_dependency_dir "${name}")"
   if [ -n "${local_dir}" ]; then
      echo "Using local ${name}: ${local_dir}"
      ln -s "${local_dir}" "${name}"
   elif [ "${archive_type}" = "zip" ]; then
      curl -sL "${url}" -o "${name}.zip"
      unzip "${name}.zip"
      mv "${name}-${sha}" "${name}"
   else
      curl -sL "${url}" -o "${name}-${sha}.tar.gz"
      tar xzf "${name}-${sha}.tar.gz"
      mv "${name}-${sha}" "${name}"
   fi
}


if [ -z "${BUILD_TYPE}" ]; then
   BUILD_TYPE="Release"
fi

echo "Build type: ${BUILD_TYPE}"
echo ""
