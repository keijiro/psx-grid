#!/usr/bin/env python3
"""Exercise platform replacement ownership in a private emulator directory."""
import os
from pathlib import Path
import re
import subprocess
import sys
from tool_paths import bios_path, emulator_path

root = Path(__file__).resolve().parents[1]
configuration = sys.argv[1] if len(sys.argv) > 1 else 'replacement-debug'
if configuration not in ('replacement-debug', 'replacement-release'):
    raise SystemExit('Usage: test-replacement-emulator.py [replacement-debug|replacement-release]')
exe = root / 'build' / configuration / 'replacement-fixture.exe'
if not exe.exists():
    raise SystemExit(f'Configure {configuration} with -DREPLACEMENT_FIXTURE=ON and build first')
output = root / 'build/validation'
output.mkdir(parents=True, exist_ok=True)
data = output / ('storage-replacement-emulator-' + configuration)
data.mkdir(parents=True, exist_ok=True)
emulator = os.environ.get('PCSX_REDUX', str(emulator_path(root)))
bios = os.environ.get('PCSX_REDUX_BIOS', str(bios_path(root)))
command = [emulator, '-portable', str(data), '-no-ui', '-no-gui-log', '-testmode', '-stdout',
           '-interpreter', '-bios', bios, '-exe', str(exe), '-run']
path = output / f'storage-replacement-{configuration}-emulator.log'
with path.open('w') as log:
    try:
        result = subprocess.run(command, cwd=root, stdout=log, stderr=subprocess.STDOUT, timeout=90)
    except subprocess.TimeoutExpired:
        raise SystemExit(f'Fixture timed out; see {path}')
log = path.read_text()
rows = re.findall(r'^REPLACEMENT case=.*$', log, re.MULTILINE)
print('\n'.join(rows), flush=True)
assert result.returncode == 0 and 'REPLACEMENT FIXTURE COMPLETE' in log, path
states = {}
for row in rows:
    name = re.search(r'case=([^ ]+)', row).group(1)
    states[name] = {key: int(value) for key, value in re.findall(r'(\w+)=(-?\d+)', row)}
assert set(states) == {'supersede', 'stop-adopt', 'disconnect-adopt',
                       'reverb-same', 'reverb-different', 'twelve-note-load'}, states
assert states['supersede']['a'] == 1 and states['supersede']['b'] == 0
for phase in ('stop-adopt', 'disconnect-adopt'):
    assert states[phase]['a'] == 1 and states[phase]['playing'] == 0, states[phase]
assert states['reverb-same']['a'] == states['reverb-different']['a'] == 1
assert states['reverb-same']['b'] == states['reverb-different']['b'] == 16383
for phase in ('reverb-same', 'reverb-different', 'twelve-note-load'):
    state = states[phase]
    assert state['cost'] <= 4233 and state['interval'] <= 4233, (phase, state)
for phase, state in states.items():
    if state['playing']:
        assert 0 < state['dispatch'] <= 4233, (phase, state)
load = states['twelve-note-load']
assert load['a'] >= 12 and load['playing'] == 1, load
print('PASS: pending edit supersession, repeated rejection, stop/disconnect adoption, reverb memory and 12-note deadlines')
