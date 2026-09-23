# Development

## Setup and Execution

The setup requires an Apple Silicon Mac, Xcode Command Line Tools, Homebrew,
Git, and Python 3. The pinned dependencies are PSn00bSDK v0.24, MIPS GCC
16.2.0, binutils 2.47, PCSX-Redux build 250, and its bundled OpenBIOS. See [toolchain.lock](../toolchain.lock) for the pinned
details.

Run these commands from the repository root:

```sh
./scripts/setup.sh
source scripts/env.sh  # zsh
cmake --preset debug
cmake --build --preset debug
./scripts/run.sh
```

For a release build:

```sh
source scripts/env.sh
cmake --preset release
cmake --build --preset release
./scripts/run.sh build/release/psx-grid.exe
```

The setup places the SDK, emulator, and configuration under `.local/`, and the
SDK sources under `third_party/`. It reuses an existing system toolchain when
the installed version matches the pinned version. The setup can be run more
than once. `env.sh` does not modify shell configuration files. `run.sh` enables
the interpreter and debugger and sends logs to standard output. Override the
launch targets with `PCSX_REDUX`, `PCSX_REDUX_BIOS`, and `PCSX_REDUX_DATA`.

When the emulator first asks about automatic updates, complete the prompt.
Disable automatic updates to retain the pinned version. If macOS blocks the
application, open it from Finder and approve it. In the emulator, use
`Configuration > Controls` to assign Port 1's D-pad, Cross, Circle, and START buttons
to a gamepad or keyboard. See [usage.md](usage.md) for the editing walkthrough.

## Structure and Validation

- `src/score.*`: SDK-independent model and edit validation.
- `src/sequencer.*`: SDK-independent runners, exact absolute deadlines,
  live runner reconciliation, ordered held locks, generation-tagged gate-offs,
  and bounded catch-up.
- `src/audio.*`: SDK-independent voice allocation and volume envelopes with
  an injected register driver for host tests.
- `src/audio_psx.c`: Double-buffered score publication, SPU upload/registers,
  timer interrupts, lifecycle, and debugger-visible measurements.
- `scripts/generate-audio.py`: Independent sine ADPCM generation, pitch table,
  and decoded error report (`generated/sine_samples.txt`).
- `src/input.*`: Ordered input history, button presses, repeats, disconnection,
  and reconnection.
- `src/pad.*`: Port 1 asynchronous SIO polling and completed-report publication.
- `src/editor.*`: Menus, candidate properties, clipboard, deletion confirmation,
  and press/hold/release movement transitions.
- `src/render.*`: 320 x 240 NTSC output, double buffering, scrolling, and
  render-packet management.
- [`assets/ui/`](../assets/ui/README.md), `scripts/generate-assets.py`: editable
  masks, font attribution, and deterministic indexed-atlas generation
  (Python 3, no extra packages).
- `src/ui_style.h`: shared screen geometry and grayscale roles.
- `src/main.c`: Input history consumption, editor processing, START, and frame/status updates.
- `build/{debug,release}/psx-grid.{elf,exe}`: ELF and PS-X EXE outputs.

Run the host tests with Clang, ASan, and UBSan:

```sh
./scripts/test.sh
```

The tests cover boundaries, collisions, capacity, cancellation, input, and
repeated edits. A GPU stub also checks render bounds and buffer usage across
all coordinates and screen states. It does not substitute for testing the
actual GPU or evaluating the interaction. See
[validation.md](validation.md) for verified behavior and remaining
work.

## Design Context

This prototype is a preparatory step toward porting Jacquard to PlayStation.
Its main interaction experiment replaces Jacquard's touch input with D-pad
positioning and a Cross-button context menu. See [usage.md](usage.md) for the
current editing rules and scope.

The development environment was adapted from the sibling `../psx-test`
project. The model and editor use Jacquard's integer-grid and cell-role
concepts in a small SDK-independent C implementation, rather than a direct
port of its C# and Unity UI. The original reference checkout is
`~/Projects/jacquard/main`; relevant files are `Assets/Core/Model/Lane.cs`,
`Assets/Core/Model/Score.cs`, `Assets/Jacquard/App/ScoreEditor.cs`, and
`Assets/Jacquard/UI/ScoreView.cs`.

The model owns edit validation, the editor owns interaction state, and the
renderer owns presentation. Tile values, stable pool IDs, branch ownership, and transactional validation
live in the model; it does not depend on atlas coordinates or grayscale values. Asset
provenance and licensing live with the [editable sources](../assets/ui/README.md).
Visual study results and remaining acceptance checks are in
[validation.md](validation.md).

## Audio fixture

Build and run the standalone development fixture without changing the editor's
empty startup score:

```sh
source scripts/env.sh
cmake --preset debug -DAUDIO_FIXTURE=ON
cmake --build --preset debug
python3 scripts/test-audio-emulator.py debug
cmake --preset release -DAUDIO_FIXTURE=ON
cmake --build --preset release
python3 scripts/test-audio-emulator.py release
```

The runner uses PCSX-Redux's headless test mode in isolated directories under
`build/validation`. It logs service cost/intervals, actual key-on lateness,
SPU decoded-buffer peaks, envelope register timing, and dense-score overload.
The fixture's exit port is emulator-specific; use `psx-grid.exe` on hardware.
A completed fixture is not a substitute for the listening/controller walkthrough.

`audio_service_peak`, `audio_interval_peak`, and `audio_dispatch_peak` use
4,233,600-Hz clock ticks. Voice steals, skipped notes, and catch-up overloads
have separate counters. Linker maps are emitted beside both executables.
The mutable editor score, model scratch score, and two playback buffers are
separate fixed allocations. The main thread prepares score revisions; the
interrupt adopts them and reconciles playback between complete time slices.
No interrupt allocates, copies the score, logs, renders, or starts DMA.
Lifecycle preparation runs on the main thread. Regular platform updates also
run without input so that coalesced revisions can reach playback.

## Input fixture

`AUDIO_FIXTURE=ON` also builds `input-fixture.exe`. After the builds above, run:

```sh
python3 scripts/test-input-emulator.py debug digital
python3 scripts/test-input-emulator.py debug analog
python3 scripts/test-input-emulator.py release digital
python3 scripts/test-input-emulator.py release analog
```

The runner injects one-frame Right/Cross/START taps through Redux's controller
API, so they pass through SIO and the application's real pad driver. It checks
stopped playback, a 24-voice score, consumption delayed by eight rendered frames,
a held-button disconnect/reconnect, and a 4,096-tile overloaded score. Logs are written to
`build/validation/input-{debug,release}-{digital,analog}-emulator.log`.
Host tests separately exercise queue wraparound, overflow recovery and editor
mode transitions. Manual controller feel and physical-console communication
remain separate checks.

Platform initialization proceeds from rendering (SDK IRQ setup), to audio
(Timer 2 clock and the periodic service), to pad callbacks. The shared timer
services both audio and deferred pad transfers even when playback is stopped.
The editor consumes completed pad reports on the main thread; rendering and
score transactions never run inside the input callbacks. Port 2, memory cards,
rumble and analog-axis editing are not implemented.
