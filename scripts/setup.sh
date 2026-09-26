#!/bin/bash
set -euo pipefail

SCRIPT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
PROJECT_ROOT=$(CDPATH= cd -- "${SCRIPT_DIR}/.." && pwd)
. "${SCRIPT_DIR}/dependency-paths.sh"
psx_grid_set_dependency_paths "${PROJECT_ROOT}"

SDK_TAG="v0.24"
SDK_COMMIT="${PSX_GRID_SDK_COMMIT}"
PCSX_COMMIT="c2e2dec197d3eb8f3db2ee63b8037321dbe1085e"
BINUTILS_VERSION="2.47"
GCC_VERSION="${PSX_GRID_GCC_VERSION}"

FORMULA_BASE="https://raw.githubusercontent.com/grumpycoders/pcsx-redux/${PCSX_COMMIT}/tools/macos-mips"
BINUTILS_FORMULA_SHA256="20c8043f71773402683d30e0a611586bf6755f721421a4b435f09b050c955f03"
GCC_FORMULA_SHA256="d6a9737b2bd04031ef7a4dfa8e163338feb214837fa466f0d37bed03166a3936"
PCSX_DMG_URL="https://distrib.app/storage/assets/df7/d54/7b1/7fb5a8875b24e0329d2e496aed2e88d0b800935f5df8ac3172c0578/PCSX-Redux-c2e2dec1-Arm.dmg"
PCSX_DMG_SHA256="8022e3b3158fc2aa6f742b83e582783552fc2adebf9af6cd94da22786703d882"

SDK_SOURCE="${PSX_GRID_SDK_SOURCE}"
SDK_INSTALL="${PSX_GRID_SDK_INSTALL}"
SDK_BUILD="${PSX_GRID_SDK_BUILD}"
PATCH_FILE="${PROJECT_ROOT}/patches/psn00bsdk-v0.24-macos-clang.patch"
FORMULA_DIR="${PSX_GRID_DEPS}/toolchain-formulas"
DOWNLOAD_DIR="${PSX_GRID_DEPS}/downloads"
PCSX_APP="${PSX_GRID_PCSX_APP}"
PCSX_DMG="${DOWNLOAD_DIR}/PCSX-Redux-c2e2dec1-Arm.dmg"

die() {
  echo "setup: $*" >&2
  exit 1
}

sha256_of() {
  shasum -a 256 "$1" | awk '{ print $1 }'
}

fetch_checked() {
  local url=$1
  local destination=$2
  local expected=$3
  local temporary="${destination}.part"

  if [ -f "${destination}" ] && [ "$(sha256_of "${destination}")" = "${expected}" ]; then
    return
  fi

  rm -f "${temporary}"
  curl -fL --retry 3 --output "${temporary}" "${url}"
  [ "$(sha256_of "${temporary}")" = "${expected}" ] || die "checksum mismatch: ${url}"
  mv "${temporary}" "${destination}"
}

[ "$(uname -s)" = "Darwin" ] || die "this setup currently supports macOS only"
[ "$(uname -m)" = "arm64" ] || die "this setup is pinned to Apple Silicon (arm64)"
command -v brew >/dev/null || die "Homebrew is required"
command -v git >/dev/null || die "Git is required"
command -v rustup >/dev/null || die "rustup is required; install it from https://rustup.rs"

# Worktrees may start setup together; serialize clone, patch, and install.
if [ "${1:-}" != "--cache-locked" ]; then
  [ "$#" -eq 0 ] || die "Usage: ./scripts/setup.sh"
  mkdir -p "${PSX_GRID_DEPS}"
  exec lockf -k "${PSX_GRID_DEPS}/setup.lock" "${SCRIPT_DIR}/setup.sh" --cache-locked
fi

rustup toolchain install nightly-2026-09-26 --profile minimal \
  --component rust-src --component rustfmt --component clippy

mkdir -p "${FORMULA_DIR}" "${DOWNLOAD_DIR}" "${PSX_GRID_SDK_DIR}" "$(dirname "${PCSX_APP}")"

brew tap nikitabobko/tap

HOST_FORMULAE=(cmake ninja texinfo libmpc mpfr gnu-sed nikitabobko/tap/brew-install-path)
MISSING_FORMULAE=()
for formula in "${HOST_FORMULAE[@]}"; do
  if ! brew list --versions "${formula}" >/dev/null 2>&1; then
    MISSING_FORMULAE+=("${formula}")
  fi
done
if [ "${#MISSING_FORMULAE[@]}" -gt 0 ]; then
  brew install "${MISSING_FORMULAE[@]}"
fi

BINUTILS_FORMULA="${FORMULA_DIR}/mipsel-none-elf-binutils.rb"
GCC_FORMULA="${FORMULA_DIR}/mipsel-none-elf-gcc.rb"
fetch_checked "${FORMULA_BASE}/mipsel-none-elf-binutils.rb" "${BINUTILS_FORMULA}" "${BINUTILS_FORMULA_SHA256}"
fetch_checked "${FORMULA_BASE}/mipsel-none-elf-gcc.rb" "${GCC_FORMULA}" "${GCC_FORMULA_SHA256}"

if ! brew list --versions mipsel-none-elf-binutils 2>/dev/null | grep -q " ${BINUTILS_VERSION}$"; then
  brew install-path "${BINUTILS_FORMULA}"
fi
if ! brew list --versions mipsel-none-elf-gcc 2>/dev/null | grep -q " ${GCC_VERSION}$"; then
  brew install-path "${GCC_FORMULA}"
fi

command -v mipsel-none-elf-gcc >/dev/null || die "mipsel-none-elf-gcc is unavailable after installation"
[ "$(mipsel-none-elf-gcc -dumpfullversion)" = "${GCC_VERSION}" ] || die "unexpected mipsel-none-elf-gcc version"

if [ ! -d "${SDK_SOURCE}/.git" ]; then
  git clone --branch "${SDK_TAG}" --depth 1 --recurse-submodules --shallow-submodules \
    https://github.com/Lameguy64/PSn00bSDK.git "${SDK_SOURCE}"
fi

[ "$(git -C "${SDK_SOURCE}" rev-parse HEAD)" = "${SDK_COMMIT}" ] || \
  die "${SDK_SOURCE} exists at a different revision"
git -C "${SDK_SOURCE}" submodule update --init --recursive
[ "$(sha256_of "${PATCH_FILE}")" = "${PSX_GRID_SDK_PATCH}" ] || die "unexpected SDK patch"
[ "$(git -C "${SDK_SOURCE}/tools/mkpsxiso" rev-parse HEAD)" = "9f6275f08829ea9de8122c8232a019e8724acbbd" ] || die "unexpected mkpsxiso revision"
[ "$(git -C "${SDK_SOURCE}/tools/tinyxml2" rev-parse HEAD)" = "e05956094c27117f989d22f25b75633123d72a83" ] || die "unexpected tinyxml2 revision"

if sed -n '1,9p' "${SDK_SOURCE}/libpsn00b/lzp/compress.c" | grep -q '#ifdef LZP_USE_MALLOC'; then
  patch -d "${SDK_SOURCE}" -p1 < "${PATCH_FILE}"
fi

grep -q 'void (\*func)(uint32_t, uint32_t, uint32_t)' "${SDK_SOURCE}/libpsn00b/include/psxgpu.h" || \
  die "PSn00bSDK compatibility patch is incomplete"
grep -q '#define stat64 stat' "${SDK_SOURCE}/tools/mkpsxiso/src/shared/platform.h" || \
  die "mkpsxiso compatibility patch is incomplete"

SDK_MARKER="${SDK_INSTALL}/.installed-${SDK_COMMIT}-${GCC_VERSION}"
if [ ! -f "${SDK_MARKER}" ]; then
  cmake -S "${SDK_SOURCE}" -B "${SDK_BUILD}" -G Ninja \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_INSTALL_PREFIX="${SDK_INSTALL}" \
    -DPSN00BSDK_GIT_TAG="${SDK_TAG}" \
    -DPSN00BSDK_GIT_COMMIT="${SDK_COMMIT}"
  cmake --build "${SDK_BUILD}"
  cmake --install "${SDK_BUILD}"
  touch "${SDK_MARKER}"
fi

[ -x "${SDK_INSTALL}/bin/elf2x" ] || die "PSn00bSDK host tools are missing"
[ -f "${SDK_INSTALL}/lib/libpsn00b/cmake/sdk.cmake" ] || die "PSn00bSDK libraries are missing"

fetch_checked "${PCSX_DMG_URL}" "${PCSX_DMG}" "${PCSX_DMG_SHA256}"

PCSX_VERSION_JSON="${PCSX_APP}/Contents/Resources/share/pcsx-redux/resources/version.json"
if [ -d "${PCSX_APP}" ] && ! grep -q "${PCSX_COMMIT}" "${PCSX_VERSION_JSON}" 2>/dev/null; then
  die "${PCSX_APP} exists but is not the pinned build; move it aside before rerunning setup"
fi

if [ ! -x "${PCSX_APP}/Contents/MacOS/PCSX-Redux" ]; then
  MOUNT_DIR=$(mktemp -d "${TMPDIR:-/tmp}/pcsx-redux-mount.XXXXXX")
  cleanup_mount() {
    hdiutil detach "${MOUNT_DIR}" >/dev/null 2>&1 || true
    rmdir "${MOUNT_DIR}" >/dev/null 2>&1 || true
  }
  trap cleanup_mount EXIT
  hdiutil attach "${PCSX_DMG}" -nobrowse -readonly -mountpoint "${MOUNT_DIR}" >/dev/null
  ditto "${MOUNT_DIR}/PCSX-Redux.app" "${PCSX_APP}"
  cleanup_mount
  trap - EXIT
fi

BIOS="${PCSX_APP}/Contents/Resources/share/pcsx-redux/resources/openbios.bin"
[ -f "${BIOS}" ] || die "the pinned PCSX-Redux build does not contain OpenBIOS"
[ "$(sha256_of "${BIOS}")" = "713ea2aed58606282b3ff5e91d77f8c5892ea36f5adcd279d2c7dd26604a625d" ] || die "unexpected OpenBIOS checksum"
mkdir -p "${PROJECT_ROOT}/.local/pcsx-redux-data"
"${PCSX_APP}/Contents/MacOS/PCSX-Redux" \
  -portable "${PROJECT_ROOT}/.local/pcsx-redux-data" -dumpproto >/dev/null

echo "Setup complete."
echo "Next: ./scripts/build.sh"
