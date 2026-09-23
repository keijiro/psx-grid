#!/usr/bin/env python3
"""Exercise HTTP/Lua control and capture the ordinary editor's GPU output."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import socket
import struct
import subprocess
import tempfile
import time
import urllib.error
import urllib.parse
import urllib.request

root = Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('configuration', nargs='?', default='debug', choices=('debug', 'release'))
parser.add_argument('--port', type=int, default=8080)
args = parser.parse_args()
if not 1 <= args.port <= 65535:
    parser.error('port must be between 1 and 65535')
exe = root / 'build' / args.configuration / 'psx-grid.exe'
if not exe.is_file():
    parser.error(f'Build the editor first: {exe}')
# Refuse an occupied port rather than accidentally driving somebody else's emulator.
with socket.socket() as probe:
    probe.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    probe.bind(('0.0.0.0', args.port))
output = root / 'build/validation' / ('web-' + args.configuration)
output.mkdir(parents=True, exist_ok=True)
with tempfile.TemporaryDirectory(prefix='session-', dir=output) as directory:
    data = Path(directory)
    data.joinpath('pcsx.json').write_text(json.dumps({'emulator': {
        'AutoUpdate': False, 'ShownAutoUpdateConfig': True}}))
    emulator = os.environ.get('PCSX_REDUX', str(root / '.local/PCSX-Redux.app/Contents/MacOS/PCSX-Redux'))
    bios = os.environ.get('PCSX_REDUX_BIOS', str(root / '.local/PCSX-Redux.app/Contents/Resources/share/pcsx-redux/resources/openbios.bin'))
    command = [emulator, '-portable', str(data), '-no-ui', '-no-gui-log', '-stdout',
               '-lua_stdout', '-interpreter', '-webserver', '-webserver-port', str(args.port),
               '-bios', bios, '-exe', str(exe), '-dofile', str(root / 'tests/web_control.lua'), '-run']
    base = f'http://127.0.0.1:{args.port}/api/v1/'
    opener = urllib.request.build_opener(urllib.request.ProxyHandler({}))
    def request(path, params=None):
        if params:
            path += ('&' if '?' in path else '?') + urllib.parse.urlencode(params)
        req = urllib.request.Request(base + path, data=None if params is None else b'')
        with opener.open(req, timeout=5) as response:
            return response.read()
    def status():
        return json.loads(request('lua/grid/status'))
    def wait_for(predicate, timeout=30):
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            if process.poll() is not None:
                raise RuntimeError(f'Redux exited with {process.returncode}; see {output / "emulator.log"}')
            if predicate():
                return
            time.sleep(0.02)
        raise TimeoutError('Emulator operation timed out')
    records = []
    def step(buttons='', hold=4, settle=12):
        before = status()
        request('lua/grid/step', dict(buttons=buttons, hold=hold, settle=settle))
        wait_for(lambda: status()['completed'] == before['completed'] + 1)
        state = status()
        assert state['remaining'] == 0
        assert state['frame'] - before['frame'] == hold + settle
        assert not json.loads(request('execution-flow'))['running']
        records.append(dict(buttons=buttons, hold=hold, settle=settle, **state))
    def capture(name):
        png = request('screen/still')
        assert png[:8] == b'\x89PNG\r\n\x1a\n'
        assert struct.unpack('>II', png[16:24]) == (320, 240)
        output.joinpath(name + '.png').write_bytes(png)
        records.append(dict(capture=name, sha256=hashlib.sha256(png).hexdigest()))
        return png
    with output.joinpath('emulator.log').open('wb') as log:
        process = subprocess.Popen(command, cwd=root, stdout=log, stderr=subprocess.STDOUT)
        try:
            def ready():
                try:
                    return status()['frame'] >= 300
                except (urllib.error.URLError, OSError):
                    return False
            wait_for(ready)
            request('execution-flow?function=pause', {})
            assert not json.loads(request('execution-flow'))['running']
            paused = status()['frame']
            time.sleep(0.1)
            assert status()['frame'] == paused
            request('execution-flow?function=resume', {})
            wait_for(lambda: status()['frame'] > paused)
            request('execution-flow?function=pause', {})
            step()
            initial = capture('01-empty')
            step('CROSS')
            menu = capture('02-ground-menu')
            assert menu != initial
            step('CIRCLE')
            assert capture('03-cancelled') == initial
            step('CROSS')
            step('CROSS')
            lane = capture('04-new-lane')
            assert lane != initial
            step('SELECT')
            capture('05-main-menu')
            step('CIRCLE')
            step('START')
            capture('06-playing')
            step('START')
            capture('07-stopped')
            # Rejected requests must leave the scheduler usable.
            try:
                request('lua/grid/step', {'buttons': 'INVALID'})
            except urllib.error.HTTPError as error:
                assert error.code == 500
            else:
                raise AssertionError('Invalid button was accepted')
            assert status()['remaining'] == 0
            step()
            request('lua/grid/cancel', {})
            output.joinpath('result.json').write_text(json.dumps({
                'configuration': args.configuration,
                'exe_sha256': hashlib.sha256(exe.read_bytes()).hexdigest(),
                'checks': 'pause/resume, bounded pad steps, PNG capture, menu cancel, invalid request',
                'records': records}, indent=2) + '\n')
            print(f'PASS: web/Lua control and seven GPU captures: {output}')
            print('Menu/lane/playback content requires visual review; see result.json for automated checks.')
        finally:
            process.terminate()
            try:
                process.wait(timeout=5)
            except subprocess.TimeoutExpired:
                process.kill()
                process.wait()
