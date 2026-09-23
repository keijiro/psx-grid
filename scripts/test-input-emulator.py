#!/usr/bin/env python3
"""Inject one-frame taps through Redux's pad API, including stalled consumption."""
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys

root = Path(__file__).resolve().parents[1]
configuration = sys.argv[1] if len(sys.argv) > 1 else 'debug'
device = sys.argv[2] if len(sys.argv) > 2 else 'digital'
if configuration not in ('debug', 'release') or device not in ('digital', 'analog'):
    raise SystemExit('Usage: test-input-emulator.py [debug|release] [digital|analog]')
output = root / 'build/validation'
data = output / ('input-emulator-' + configuration + '-' + device)
data.mkdir(parents=True, exist_ok=True)
nm = shutil.which('mipsel-none-elf-nm') or '/opt/homebrew/bin/mipsel-none-elf-nm'
symbols = subprocess.check_output([nm, str(root / 'build' / configuration / 'input-fixture.elf')], text=True)
def offset(name):
    return int(re.search(r'^([0-9a-f]+) \w ' + name + '$', symbols, re.MULTILINE)[1], 16) & 0x1fffff
script = data / 'inject.lua'
script.write_text(f'input_analog = {str(device == "analog").lower()}\n'
                  f'input_phase_offset = {offset("input_fixture_phase")}\n'
                  f'input_expected_offset = {offset("input_fixture_expected")}\n'
                  + (root / 'tests/input_fixture.lua').read_text())
emulator = os.environ.get('PCSX_REDUX', str(root / '.local/PCSX-Redux.app/Contents/MacOS/PCSX-Redux'))
bios = os.environ.get('PCSX_REDUX_BIOS', str(root / '.local/PCSX-Redux.app/Contents/Resources/share/pcsx-redux/resources/openbios.bin'))
command = [emulator, '-portable', str(data), '-no-ui', '-no-gui-log', '-testmode', '-stdout',
           '-interpreter', '-bios', bios, '-exe', str(root / 'build' / configuration / 'input-fixture.exe'),
           '-dofile', str(script), '-run']
result = subprocess.run(command, cwd=root, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=120)
log = result.stdout.decode(errors='replace')
(output / f'input-{configuration}-{device}-emulator.log').write_text(log)
rows = re.findall(r'^INPUT phase=.*$', log, re.MULTILINE)
for row in rows:
    print(row)
assert result.returncode == 0 and 'INPUT FIXTURE COMPLETE' in log, log[-4000:]
assert len(rows) == 5, log[-4000:]
for row in rows:
    v = {key:int(value) for key,value in re.findall(r'(\w+)=(\d+)', row)}
    assert v['id'] == (0x73 if device == 'analog' else 0x41), v
    assert v['expected'] == (1 if v['phase'] == 4 else 10 if v['phase'] == 5 else 100), v
    assert all(v[key] == v['expected'] for key in ('moves', 'presses', 'releases', 'starts', 'selects')), v
    assert v['overflows'] == 0, v
    if v['phase'] == 4:
        assert v['disconnected'] >= 15 and v['timeouts'] >= 15, v
    else:
        assert v['timeouts'] == v['disconnected'] == 0, v
        # One report can straddle the measurement boundary, but no polls may vanish.
        assert abs(v['polls'] - v['reports']) <= 1, v
print('PASS: pad refresh, one-frame taps, delayed consumption and held reconnect')
