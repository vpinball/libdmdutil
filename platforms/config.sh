#!/bin/bash

set -e

LIBZEDMD_SHA=faa1dbb188a490ffc2fc8a017ef2c197f22c73e6
LIBSERUM_SHA=29ac63678c2ca448767a81723e8b830e94cddeb0
LIBPUPDMD_SHA=4a1123220e6dce73c87cc584494df2ac82cb6f4c
LIBVNI_SHA=cef652e8e543ced5ec9af7663014b240a2a99ebd
LIBUSB_SHA=15a7ebb4d426c5ce196684347d2b7cafad862626

PROJECT_SOURCE_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd -P)"

dependency_source_dir() {
   local var_name="$1"
   local source_dir="${!var_name:-}"

   if [ -z "${source_dir}" ]; then
      return 0
   fi

   (cd "${PROJECT_SOURCE_ROOT}" && cd "${source_dir}" && pwd -P)
}

print_dependency_source() {
   local label="$1"
   local sha="$2"
   local source_var="$3"
   local source_dir

   source_dir="$(dependency_source_dir "${source_var}")"
   if [ -n "${source_dir}" ]; then
      echo "  ${label}_SOURCE_DIR: ${source_dir}"
   else
      echo "  ${label}_SOURCE: archive ${sha}"
   fi
}

prepare_dependency_source() {
   local name="$1"
   local sha="$2"
   local url="$3"
   local archive_type="${4:-tar}"
   local source_var="$5"
   local source_dir

   source_dir="$(dependency_source_dir "${source_var}")"
   if [ -n "${source_dir}" ]; then
      echo "Using ${source_var}: ${source_dir}"
      ln -s "${source_dir}" "${name}"
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
