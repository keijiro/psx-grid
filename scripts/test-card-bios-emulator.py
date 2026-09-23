#!/usr/bin/env python3
"""Exercise BIOS files without audio/pad on a private disposable card."""
import hashlib
import os
from pathlib import Path
import re
import subprocess
import sys

root = Path(__file__).resolve().parents[1]
configuration = sys.argv[1] if len(sys.argv) > 1 else 'debug'
if configuration not in ('debug', 'release'):
    raise SystemExit('Usage: test-card-bios-emulator.py [debug|release]')
bios = os.environ.get('PCSX_REDUX_BIOS')
if not bios:
    raise SystemExit('Set PCSX_REDUX_BIOS to the BIOS under test')
emulator = Path(os.environ.get('PCSX_REDUX',
    root / '.local/PCSX-Redux.app/Contents/MacOS/PCSX-Redux'))
data = root / 'build/validation' / ('card-file-' + configuration)
data.mkdir(parents=True, exist_ok=True)
card = bytearray(128 * 1024)
card[:2] = b'MC'
card[127] = ord('M') ^ ord('C')
for sector in range(1, 16):
    card[sector * 128] = 0xa0
    card[sector * 128 + 8:sector * 128 + 10] = b'\xff\xff'
    card[sector * 128 + 127] = 0xa0
for sector in range(16, 36):
    card[sector * 128:sector * 128 + 4] = b'\xff' * 4
(data / 'memcard1.mcd').write_bytes(card)
(data / 'memcard2.mcd').write_bytes(card)
lock = (root / 'toolchain.lock').read_text()
revision = re.search(r'(?ms)^\[psn00bsdk\].*?^commit = "([0-9a-f]+)"', lock)[1]
def sha256(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()
(data / 'environment.txt').write_text(
    f'SDK revision: {revision}\nEmulator: {emulator}\n'
    f'Emulator SHA-256: {sha256(emulator)}\nBIOS: {bios}\n'
    f'BIOS SHA-256: {sha256(bios)}\nTimeout: 180 seconds per run\n')
command = [str(emulator), '-portable', str(data), '-no-ui', '-no-gui-log',
    '-testmode', '-stdout', '-interpreter', '-bios', bios, '-exe',
    str(root / 'build' / configuration / 'card-file-fixture.exe'), '-run']
for run in range(2):
    path = data / f'run-{run}.log'
    with path.open('w') as log:
        result = subprocess.run(command, cwd=root, stdout=log,
            stderr=subprocess.STDOUT, timeout=180)
    output = path.read_text()
    if result.returncode or 'CARD_FILE COMPLETE' not in output:
        raise SystemExit(f'BIOS file fixture failed; see {path}')
    print(' '.join(line for line in output.splitlines()
        if line.startswith('CARD_FILE ')))
