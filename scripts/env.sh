if [[ -z "${ZSH_VERSION:-}" ]]; then
  print -u2 "scripts/env.sh must be sourced from zsh"
  return 1
fi

typeset _psx_env_script="${(%):-%N}"
typeset _psx_env_root="${_psx_env_script:A:h:h}"
source "${_psx_env_script:A:h}/dependency-paths.sh"
psx_grid_set_dependency_paths "${_psx_env_root}"

export PSN00BSDK_HOME="${PSX_GRID_SDK_INSTALL}"
export PSN00BSDK_LIBS="${PSN00BSDK_HOME}/lib/libpsn00b"
export PCSX_REDUX="${PSX_GRID_PCSX_APP}/Contents/MacOS/PCSX-Redux"
export PCSX_REDUX_BIOS="${PSX_GRID_PCSX_APP}/Contents/Resources/share/pcsx-redux/resources/openbios.bin"
export PCSX_REDUX_DATA="${_psx_env_root}/.local/pcsx-redux-data"
export PATH="${PSN00BSDK_HOME}/bin:/opt/homebrew/bin:${PATH}"

unset _psx_env_script _psx_env_root PSX_GRID_COMMON_DIR PSX_GRID_DEPS
unset PSX_GRID_SDK_COMMIT PSX_GRID_SDK_PATCH PSX_GRID_GCC_VERSION PSX_GRID_PCSX_BUILD
unset PSX_GRID_SDK_DIR PSX_GRID_SDK_SOURCE PSX_GRID_SDK_BUILD PSX_GRID_SDK_INSTALL PSX_GRID_PCSX_APP
unfunction psx_grid_set_dependency_paths
