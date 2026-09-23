#!/usr/bin/env python3
"""Exercise card persistence only in a private, disposable emulator directory."""
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys

root = Path(__file__).resolve().parents[1]
configuration = sys.argv[1] if len(sys.argv) > 1 else 'debug'
scenario = sys.argv[2] if len(sys.argv) > 2 else 'normal'
if scenario not in ('normal', 'missing', 'unformatted', 'remove', 'held'):
    raise SystemExit('Scenario must be normal, missing, unformatted, remove or held')
if configuration not in ('debug', 'release'):
    raise SystemExit('Usage: test-storage-emulator.py [debug|release]')
output = root / 'build/validation'
data = output / ('storage-emulator-' + configuration + '-' + scenario)
data.mkdir(parents=True, exist_ok=True)
# Never inherit the interactive application's card paths or user data. A fresh
# formatted image makes generation and restart assertions reproducible.
card = bytearray(128 * 1024)
card[:2] = b'MC'
card[127] = ord('M') ^ ord('C')
for sector in range(1, 16):
    card[sector * 128] = 0xa0
    card[sector * 128 + 8:sector * 128 + 10] = b'\xff\xff'
    card[sector * 128 + 127] = 0xa0
for sector in range(16, 36):
    card[sector * 128:sector * 128 + 4] = b'\xff' * 4
(data / 'memcard1.mcd').write_bytes(bytes(len(card)) if scenario == 'unformatted' else card)
(data / 'memcard2.mcd').write_bytes(card)
emulator = os.environ.get('PCSX_REDUX', str(root / '.local/PCSX-Redux.app/Contents/MacOS/PCSX-Redux'))
bios = os.environ.get('PCSX_REDUX_BIOS')
if not bios:
    raise SystemExit('Set PCSX_REDUX_BIOS to the BIOS under test')
command = [emulator, '-portable', str(data), '-no-ui', '-no-gui-log', '-testmode', '-stdout',
           '-interpreter', '-bios', bios, '-exe', str(root / 'build' / configuration / 'storage-fixture.exe'), '-run']
nm = shutil.which('mipsel-none-elf-nm') or '/opt/homebrew/bin/mipsel-none-elf-nm'
symbols = subprocess.check_output([nm, str(root / 'build' / configuration / 'storage-fixture.elf')], text=True)
def address(name):
    return int(re.search(r'^([0-9a-f]+) \w ' + name + '$', symbols, re.MULTILINE)[1], 16)
def offset(name):
    return address(name) & 0x1fffff
script = data / 'inject.lua'
neutral_script = data / 'inject-stack.lua'
neutral_script.write_text("local ffi = require('ffi')\nlocal ram = PCSX.getMemPtr()\n"
                          f"local target = ffi.cast('uint32_t*', ram + {offset('storage_fixture_isr_stack')})\n"
                          f"local ready = ffi.cast('uint32_t*', ram + {offset('storage_fixture_stack_ready')})\n"
                          "stack_listener = PCSX.Events.createEventListener('GPU::Vsync', function()\n"
                          f"  if ready[0] ~= 0 and target[0] == 0 then target[0] = 0x{address('_isr_stack'):08x} end\n"
                          "end)\n")
script.write_text("local ffi = require('ffi')\nlocal ram = PCSX.getMemPtr()\n"
                      f"local phase = ffi.cast('uint32_t*', ram + {offset('storage_fixture_phase')})\n"
                      f"local expected = ffi.cast('uint32_t*', ram + {offset('storage_fixture_error')})\n"
                      f"local removed = ffi.cast('uint32_t*', ram + {offset('storage_fixture_removed')})\n"
                      f"local isr_stack = ffi.cast('uint32_t*', ram + {offset('storage_fixture_isr_stack')})\n"
                      f"local stack_ready = ffi.cast('uint32_t*', ram + {offset('storage_fixture_stack_ready')})\n"
                      "local save_frames, resume_frames = 0, 0\n"
                      "local pad = PCSX.SIO0.slots[1].pads[1]\n"
                      "local buttons = PCSX.CONSTS.PAD.BUTTON\n"
                      "storage_listener = PCSX.Events.createEventListener('GPU::Vsync', function()\n"
                      f"  if stack_ready[0] ~= 0 and isr_stack[0] == 0 then isr_stack[0] = 0x{address('_isr_stack'):08x} end\n"
                      + (f"  if phase[0] > 0 then expected[0] = {2 if scenario == 'held' else 1} end\n" if scenario != 'normal' else "")
                      + ("  if phase[0] == 2 then save_frames = save_frames + 1 end\n"
                         "  if save_frames >= 80 then PCSX.settings.emulator.Mcd1Inserted = false; removed[0] = 1 end\n" if scenario == 'remove'
                         else "  PCSX.settings.emulator.Mcd1Inserted = false\n" if scenario == 'missing'
                         else "  if phase[0] == 4 then resume_frames = resume_frames + 1 end\n"
                              "  local held = phase[0] >= 2 and (resume_frames < 5 or resume_frames == 12)\n"
                              "  for _,button in ipairs({buttons.CROSS, buttons.START}) do\n"
                              "    if held then pad.setOverride(button) else pad.clearOverride(button) end\n"
                              "  end\n" if scenario == 'held' else '')
                      + "end)\n")
command.extend(['-dofile', str(script)])
neutral_command = command[:-2] + ['-dofile', str(neutral_script)]
if scenario == 'remove':
    with (output / f'storage-{configuration}-remove-seed.log').open('w') as log:
        seeded = subprocess.run(neutral_command, cwd=root, stdout=log, stderr=subprocess.STDOUT, timeout=180)
    assert seeded.returncode == 0, 'Unable to seed the previous valid generation'
    previous = (data / 'memcard1.mcd').read_bytes()
for run in range(2 if scenario == 'normal' else 1):
    path = output / f'storage-{configuration}-{scenario}-{run}.log'
    with path.open('w') as log:
        try:
            result = subprocess.run(command, cwd=root, stdout=log, stderr=subprocess.STDOUT, timeout=180)
        except subprocess.TimeoutExpired:
            raise SystemExit(f'Fixture timed out; see {path}')
    log = path.read_text()
    rows = re.findall(r'^STORAGE stage=.*$', log, re.MULTILINE)
    print('\n'.join(rows), flush=True)
    assert result.returncode == 0 and 'STORAGE FIXTURE COMPLETE' in log, path
    stack = re.search(r'^STACK main_peak=(\d+) main_limit=(\d+) isr_peak=(\d+) isr_limit=(\d+)$', log, re.MULTILINE)
    assert stack, path
    assert int(stack[1]) <= int(stack[2]) and int(stack[3]) <= int(stack[4]), stack.group(0)
    assert int(stack[1]) < 64 * 1024, 'Main stack exhausted measured window: ' + stack.group(0)
    for row in rows:
        values = {key: int(value) for key, value in re.findall(r'(\w+)=(-?\d+)', row)}
        assert values['io_services'] == 0 and values['io_polls'] == 0, row
    if scenario in ('missing', 'unformatted', 'remove'):
        assert 'stage=error-resume' in log and 'stage=adopt' not in log, path
        if scenario in ('missing', 'unformatted'):
            assert f'stage=refresh result={6 if scenario == "missing" else 10} ' in log, path
        for row in rows:
            values = {key: int(value) for key, value in re.findall(r'(\w+)=(-?\d+)', row)}
            assert values['playing'] == 0, row
        if scenario == 'unformatted':
            assert (data / 'memcard1.mcd').read_bytes() == bytes(len(card))
        if scenario == 'remove':
            save = next(row for row in rows if 'stage=save ' in row)
            assert 'removed=1' in save and re.search(r'result=(6|7|8) ', save), save
            saved = (data / 'memcard1.mcd').read_bytes()
            assert saved[128:256] == previous[128:256] and saved[8192:16384] == previous[8192:16384]
            with (output / f'storage-{configuration}-remove-recovery.log').open('w') as recovery_log:
                recovered = subprocess.run(neutral_command, cwd=root, stdout=recovery_log, stderr=subprocess.STDOUT, timeout=180)
            recovery = (output / f'storage-{configuration}-remove-recovery.log').read_text()
            assert recovered.returncode == 0 and 'stage=restart-load result=2' in recovery and 'STORAGE FIXTURE COMPLETE' in recovery
        continue
    assert ('stage=restart-load' in log) == bool(run), path
    for row in rows:
        values = {key: int(value) for key, value in re.findall(r'(\w+)=(-?\d+)', row)}
        assert values['playing'] == 0 and values['services'] > 0, row
    saved = (data / 'memcard1.mcd').read_bytes()
    entries = [saved[i*128:(i+1)*128] for i in range(1, 16) if saved[i*128] == 0x51]
    assert len(entries) == 1 and entries[0][10:30] == f'BIJACQUARD01{run+1:08X}'.encode(), entries
    block = next(i for i in range(1, 16) if saved[i*128] == 0x51)
    payload = saved[block*8192:(block+1)*8192]
    assert entries[0][4:8] == (8192).to_bytes(4, 'little') and entries[0][8:10] == b'\xff\xff'
    assert payload[:4] == b'SC\x11\x01' and payload[4:26] == 'ＪＡＣＱＵＡＲＤ　０１'.encode('shift_jis')
    assert any(payload[96:128]) and any(payload[128:256]), 'Missing palette/icon'
print(f'PASS: isolated card scenario {scenario}, stopped transport and pad resumption')
