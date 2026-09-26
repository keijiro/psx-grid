#!/bin/sh

psx_grid_set_dependency_paths() {
  PSX_GRID_COMMON_DIR=$(git -C "$1" rev-parse --path-format=absolute --git-common-dir) || return 1
  PSX_GRID_DEPS="${PSX_GRID_COMMON_DIR}/psx-grid-deps"

  PSX_GRID_SDK_COMMIT=06e65bea3a778b2dae5af77a7935ae3868ddd4d3
  PSX_GRID_SDK_PATCH=b82973981b81362284426b9b7b77d79ee299226928abc75f4e9fa2ae3c51ce45
  PSX_GRID_GCC_VERSION=16.2.0
  PSX_GRID_PCSX_BUILD=250

  # The SDK source is patched in place, so its cache key includes the patch.
  PSX_GRID_SDK_DIR="${PSX_GRID_DEPS}/psn00bsdk-${PSX_GRID_SDK_COMMIT}-${PSX_GRID_SDK_PATCH}-${PSX_GRID_GCC_VERSION}"
  PSX_GRID_SDK_SOURCE="${PSX_GRID_SDK_DIR}/source"
  PSX_GRID_SDK_BUILD="${PSX_GRID_SDK_DIR}/build"
  PSX_GRID_SDK_INSTALL="${PSX_GRID_SDK_DIR}/install"
  PSX_GRID_PCSX_APP="${PSX_GRID_DEPS}/pcsx-redux-${PSX_GRID_PCSX_BUILD}/PCSX-Redux.app"
}
