#!/usr/bin/env python3
"""Run the bounded SPU/clock fixture with the pinned emulator, without its UI."""
import os
import re
from pathlib import Path
import subprocess
import sys
root = Path(__file__).resolve().parents[1]
configuration = sys.argv[1] if len(sys.argv)>1 else 'debug'
if configuration not in ('debug', 'release'):
    raise SystemExit('Usage: test-audio-emulator.py [debug|release]')
exe = root / 'build' / configuration / 'audio-fixture.exe'
if not exe.exists():
    raise SystemExit(f'Configure {configuration} with -DAUDIO_FIXTURE=ON and build first')
output = root / 'build/validation'
output.mkdir(parents=True, exist_ok=True)
data = output / ('emulator-'+configuration)
data.mkdir(exist_ok=True)
emulator = os.environ.get('PCSX_REDUX', str(root / '.local/PCSX-Redux.app/Contents/MacOS/PCSX-Redux'))
bios = os.environ.get('PCSX_REDUX_BIOS', str(root / '.local/PCSX-Redux.app/Contents/Resources/share/pcsx-redux/resources/openbios.bin'))
command = [emulator, '-portable', str(data), '-no-ui', '-no-gui-log', '-testmode', '-stdout', '-interpreter', '-bios', bios, '-exe', str(exe), '-run']
result = subprocess.run(command, cwd=root, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=120)
log = result.stdout.decode(errors='replace')
(output / f'audio-{configuration}-emulator.log').write_text(log)
for line in log.splitlines():
    if line.startswith(('AUDIO ', 'CAPTURE ', 'ENVELOPE ', 'DISPATCH ', 'LIVE ')):
        print(line)
if result.returncode or 'AUDIO FIXTURE COMPLETE' not in log:
    raise SystemExit(f'Fixture failed: exit {result.returncode}; see {output}')

# Check device-path measurements as well as successful process completion.
# All thresholds are in the hardware clock, independent of host execution speed.
def fields(prefix):
    match = re.search(r'^'+re.escape(prefix)+r': (.*)$', log, re.MULTILINE)
    assert match, f'Missing measurement: {prefix}'
    return {key:int(value) for key,value in re.findall(r'(\w+)=(\d+)', match[1])}
for phase in ('C4 loop', '24-note chord', 'live pitch'):
    timing, dispatch = fields('AUDIO '+phase), fields('DISPATCH '+phase)
    assert timing['skipped']==0 and timing['overloads']==0, (phase, timing)
    assert 0 < dispatch['peak'] <= 4233 and timing['interval'] <= 4233, (phase, dispatch, timing)
chord = fields('DISPATCH 24-note chord')
assert chord['count']==24 and chord['span']==0, chord
loop = fields('DISPATCH C4 loop')
assert abs(loop['span']-(loop['count']-1)*529200) <= 8467, loop
for phase in ('stopped', 'final stop', 'pending stop', 'pending disconnect'):
    state = fields('AUDIO '+phase)
    # ADSR readback can lag in Redux's separate SPU thread. Every left/right
    # volume register at zero establishes silence independently of that value.
    assert state['vol']==state['volumes']==0, state
for pitch in (0,4,9):
    match = re.search(r'CAPTURE C'+str(pitch)+r' peak=(\d+)', log)
    assert match and 0 < int(match[1]) < 32767
envelopes = re.findall(r'ENVELOPE request=(\d+) attack_ticks=(\d+) release_ticks=(\d+)', log)
assert {int(entry[0]) for entry in envelopes} == {0,1,5,100}
for request,attack,release in envelopes:
    expected = int(request)*4233600//1000
    assert abs(int(attack)-expected) <= 4233 and abs(int(release)-expected) <= 4233
stress = fields('AUDIO 4096 tiles')
assert stress['cost'] < 65536 and stress['interval'] < 65536, stress
assert stress['skipped']>0 and stress['overloads']>0, stress
for phase,count in (('live pitch',25),('live 4096 tiles',32)):
    live=fields('LIVE '+phase)
    assert live['edits']==live['adopted']==count and live['playing']==1, live
    timing=fields('AUDIO '+phase)
    assert timing['cost']<65536 and timing['interval']<65536, timing
registers=fields('LIVE pitch registers')
assert registers['before']>0 and registers['after']>0 and registers['before']!=registers['after'], registers
coalesced=fields('LIVE coalesced')
assert coalesced['pending']==coalesced['deferred']==coalesced['adopted']==coalesced['playing']==1, coalesced
for phase in ('pending stop','pending disconnect'):
    state=fields('LIVE '+phase)
    assert state['pending']==state['unchanged']==1 and state['playing']==0, state
print('PASS: emulator dispatch, loop duration, envelopes, signal, live publication, overload and pending-stop checks')
