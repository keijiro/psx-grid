#!/bin/zsh
set -euo pipefail

SCRIPT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
PROJECT_ROOT=$(CDPATH= cd -- "${SCRIPT_DIR}/.." && pwd)

if [ "$#" -gt 1 ]; then
  echo "Usage: ./scripts/build.sh [debug|release]" >&2
  exit 2
fi

CONFIGURATION=${1:-debug}
case "${CONFIGURATION}" in
  debug|release) ;;
  *)
    echo "Usage: ./scripts/build.sh [debug|release]" >&2
    exit 2
    ;;
esac

source "${SCRIPT_DIR}/env.sh"
cd "${PROJECT_ROOT}"
CACHE="${PROJECT_ROOT}/build/${CONFIGURATION}/CMakeCache.txt"
CACHED_TOOLCHAIN=
CACHED_ELF2X=
if [[ -f "${CACHE}" ]]; then
  CACHED_TOOLCHAIN=$(sed -n 's/^CMAKE_TOOLCHAIN_FILE:FILEPATH=//p' "${CACHE}")
  CACHED_ELF2X=$(sed -n 's/^ELF2X:FILEPATH=//p' "${CACHE}")
fi
# CMake retains SDK tools in the cache when the toolchain path changes.
if [[ -f "${CACHE}" && ( "${CACHED_TOOLCHAIN}" != "${PSN00BSDK_LIBS}/cmake/sdk.cmake" || "${CACHED_ELF2X}" != "${PSN00BSDK_HOME}/bin/elf2x" ) ]]; then
  cmake --fresh --preset "${CONFIGURATION}"
else
  cmake --preset "${CONFIGURATION}"
fi
cmake --build --preset "${CONFIGURATION}"
