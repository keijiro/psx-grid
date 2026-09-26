"""Resolve tools shared by all worktrees of this repository."""

from pathlib import Path
import subprocess


def pcsx_app(root):
    common_dir = subprocess.check_output(
        ['git', '-C', str(root), 'rev-parse', '--path-format=absolute',
         '--git-common-dir'], text=True).strip()
    return Path(common_dir) / 'psx-grid-deps/pcsx-redux-250/PCSX-Redux.app'


def emulator_path(root):
    return pcsx_app(root) / 'Contents/MacOS/PCSX-Redux'


def bios_path(root):
    return pcsx_app(root) / 'Contents/Resources/share/pcsx-redux/resources/openbios.bin'
