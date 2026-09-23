# Emulator control over HTTP and Lua

## Purpose and scope

PCSX-Redux's web server and a startup Lua script provide a working alternative
for controlling the **emulated application** when computer-use cannot bind or
operate the native Redux window. The existing
[computer-use blocker](validation.md#computer-use-blocker) remains a limitation
of that access path; it need not block automated pad-driven editor checks.

The control loop is: send a bounded input command over HTTP, inject pad buttons
through Redux's SIO API, wait for emulated video frames, pause, and fetch the
GPU output as PNG. No application input or editor memory is modified. This
runs the ordinary `psx-grid.exe`, including its pad driver, editor and renderer.

| Capability | Route | Evidence / boundary |
| --- | --- | --- |
| Launch without native menus | CLI `-dofile`, `-webserver`, `-no-ui` | Tested on pinned build 250 |
| Pause, resume, query execution | `/api/v1/execution-flow` | Tested; Lua status remains responsive while paused |
| Button press, release, combinations | Custom `/api/v1/lua/grid/step` | Tested through SIO; duration uses GPU vsync events |
| Capture application display | `/api/v1/screen/still` | Tested 320 × 240 PNG; excludes Redux window, menus and host shader presentation |
| Inspect RAM / VRAM | `/api/v1/cpu/ram/raw`, `/api/v1/gpu/vram/raw` | Available upstream; not exercised by this smoke test |
| Soft / hard reset | POST execution-flow with `function=reset&type=soft` or `hard` | Confirmed in pinned source, not included in this test; restart for a fresh test session |
| Native Redux menus and OS dialogs | No replacement supplied here | Continue using CLI/config/Lua for supported operations |
| Listening, physical controller feel, console timing | Separate manual / hardware checks | Not established by screenshots or scripted input |

This establishes a test mechanism and a small editor smoke test, not completion
of the full [usage walkthrough](usage.md), storage acceptance, or sound-quality
validation. Existing host tests and audio/input/storage fixtures remain useful
for assertions that cannot be inferred from pixels.

## Reproduce the smoke test

Build the normal editor using [development.md](development.md), then run:

```sh
python3 scripts/test-web-emulator.py debug
python3 scripts/test-web-emulator.py release --port 8081
```

The runner requires only Python's standard library and the pinned emulator.
It honors `PCSX_REDUX` and `PCSX_REDUX_BIOS`. Each launch gets a temporary portable
directory with disposable cards, and the process is terminated on success or
failure. An occupied port is rejected. The runner does not use interactive
emulator settings or cards.

Results go to `build/validation/web-{debug,release}/`: `emulator.log`,
`result.json` with executable hash and input/frame records, and seven numbered
PNGs. Each run replaces files of the same name; use the current run's exit
status, not old captures, to determine success.

Automated assertions check pause/resume, bounded command completion, PNG format
and dimensions, a changed image after opening the ground menu, exact image
restoration after cancelling, a changed image after creating a lane, and
rejection of an invalid button. The script also captures the main menu and
playback start/stop for **visual review**. Pixel difference alone does not prove
that a particular menu item or lane was drawn correctly.

## Use the bridge for exploratory testing

Start a separate session from the repository root, in one terminal:

```sh
source scripts/env.sh
mkdir -p build/validation/web-manual
"$PCSX_REDUX" \
  -portable "$PWD/build/validation/web-manual" \
  -no-ui -no-gui-log -stdout -lua_stdout -interpreter \
  -webserver -webserver-port 8080 \
  -bios "$PCSX_REDUX_BIOS" -exe "$PWD/build/debug/psx-grid.exe" \
  -dofile "$PWD/tests/web_control.lua" -run
```

Use a fresh portable directory for a reproducible initial score. Let boot finish
before issuing input; the smoke runner allows 300 initial vsyncs and checks the
subsequent images. Stop this process with Ctrl-C when finished. A persistent
manual directory may retain its own cards and settings.

From another terminal:

```sh
# Inspect the booted application and pause it.
curl --fail 'http://127.0.0.1:8080/api/v1/screen/still' -o /tmp/grid.png
curl --fail -X POST 'http://127.0.0.1:8080/api/v1/execution-flow?function=pause'

# Tap Cross, then allow neutral frames for the release and rendered result.
curl --fail -X POST \
  'http://127.0.0.1:8080/api/v1/lua/grid/step?buttons=CROSS&hold=4&settle=12'
curl --fail 'http://127.0.0.1:8080/api/v1/lua/grid/status'

# Repeat status until remaining is 0 and completed has increased, then capture.
curl --fail 'http://127.0.0.1:8080/api/v1/screen/still' -o /tmp/grid-menu.png

# Release any injected buttons and pause, including an interrupted command.
curl --fail -X POST 'http://127.0.0.1:8080/api/v1/lua/grid/cancel'
```

`step` accepts `UP`, `DOWN`, `LEFT`, `RIGHT`, `CROSS`, `CIRCLE`, `START`,
`SELECT`, or comma-separated combinations such as `CROSS,RIGHT`. An empty
`buttons=` advances neutral frames. `hold` and `settle` are integer vsync counts
from 1 through 600. Commands are asynchronous: the initial response acknowledges
submission, and `completed` increments only when the scheduled command finishes.
Do not submit another step until then. A command ends with all injected buttons
released and emulation paused. For uninterrupted playback, use POST
`/api/v1/execution-flow?function=resume` after the command finishes.

These are emulated-frame boundaries, not an exact instruction or SIO poll
boundary. A one-frame tap may be phase-sensitive; the smoke test uses a short
multi-frame press below the editor's directional-repeat threshold. Pausing
between commands is appropriate for editor snapshots, not evidence of continuous
audio behavior. `cancel` releases overrides immediately, but the application
must run again to consume that release.

## Pinned-version findings

Verified against PCSX-Redux macOS ARM build 250, commit
`c2e2dec197d3eb8f3db2ee63b8037321dbe1085e`, with its bundled OpenBIOS. The
[CLI documentation](https://pcsx-redux.consoledev.net/cli_flags/),
[REST documentation](https://pcsx-redux.consoledev.net/web_server/) and
[Lua web-server documentation](https://pcsx-redux.consoledev.net/Lua/web-server/)
are useful entry points, but do not completely describe this pinned build:

- `-webserver -webserver-port PORT` enables the server without opening settings.
  `-no-ui` works with both Lua handlers and GPU PNG capture.
- `/api/v1/screen/still` is implemented even though it is absent from the REST
  page's endpoint list. It returns the emulated display, not a desktop capture.
- The documented URL-encoded POST form path did not deliver named fields in
  this build. Its multipart implementation exposes part headers in `req.form`
  and concatenates part payloads internally; it does not expose the expected
  named field values. The bridge uses POST query parameters instead.
- There is no stock REST controller endpoint or arbitrary-Lua evaluation
  endpoint. `/api/v1/lua/NAME` dispatches to an already registered handler.
  Load the bridge at startup; keep it alive while making requests.
- The server binds **0.0.0.0**, despite the documentation's localhost wording.
  Using a 127.0.0.1 client URL does not restrict listening to loopback. This
  upstream server has no authentication and exposes mutating APIs; use it on
  a trusted or network-isolated host and stop it after testing.

Pinned implementation references:
[CLI](https://github.com/grumpycoders/pcsx-redux/blob/c2e2dec197d3eb8f3db2ee63b8037321dbe1085e/src/main/main.cc),
[HTTP dispatch, parsing, binding and PNG](https://github.com/grumpycoders/pcsx-redux/blob/c2e2dec197d3eb8f3db2ee63b8037321dbe1085e/src/core/web-server.cc),
[pad API](https://github.com/grumpycoders/pcsx-redux/blob/c2e2dec197d3eb8f3db2ee63b8037321dbe1085e/src/core/pad.cc).
