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
cmake --preset "${CONFIGURATION}"
cmake --build --preset "${CONFIGURATION}"
