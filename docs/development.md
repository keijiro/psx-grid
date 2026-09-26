# Development

See [C code style](c-style.md) for source formatting and comments.

## Setup and Execution

The setup requires an Apple Silicon Mac, Xcode Command Line Tools, Homebrew,
Git, Python 3, and rustup. The pinned dependencies are PSn00bSDK v0.24,
MIPS GCC 16.2.0, binutils 2.47, Rust nightly 2026-09-26, PCSX-Redux build
250, and its bundled OpenBIOS. See [toolchain.lock](../toolchain.lock) for the
pinned details.

Run these commands from the repository root:

```sh
./scripts/setup.sh
./scripts/build.sh
./scripts/run.sh
```

For a release build:

```sh
./scripts/build.sh release
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
`Configuration > Controls` to assign Port 1's D-pad, Cross, Circle, START, and SELECT buttons
to a gamepad or keyboard. See [usage.md](usage.md) for the editing walkthrough.

## Structure and Validation

- `src/score.*`: SDK-independent model and transactional edit admission.
- `src/score_format.*`: Portable v1 encoding, staging decode, and shared sizing.
- `src/storage.*`: Numbered BIOS files, discovery and recovery.
- `src/card.h`, `src/card_bios.c`: File-level card interface and BIOS backend.
- `src/sequencer.*`: SDK-independent runners, exact absolute deadlines,
  live runner reconciliation, ordered held locks, generation-tagged gate-offs,
  and bounded catch-up.
- `src/audio.*`: SDK-independent 12-note pair allocation, amplitude/mix envelopes,
  and fixed-point pitch sweep with
  an injected register driver for host tests.
- `src/audio_psx.c`: Double-buffered score publication, SPU upload/registers,
  timer interrupts, lifecycle, and debugger-visible measurements.
- `scripts/generate-audio.py`: Deterministic five-wave ADPCM banks
  (`generated/wave_samples.h`), fixed-point control tables
  (`generated/audio_tables.h`), and asset/trajectory report
  (`generated/wave_samples.txt`). Both headers share one build rule and are
  used by the editor and fixture executables.
- `rust/src/input.rs`: Ordered input history, button presses, repeats,
  disconnection, and reconnection in a `no_std` static library.
- `src/input.c`, `src/input.h`: C ABI adapter and interrupt-facing sample queue.
- `src/pad.*`: Port 1 asynchronous SIO polling and completed-report publication.
- `src/editor.*`: Menus, inline property edits, clipboard, deletion confirmation,
  and press/hold/release movement transitions.
- `src/render.*`: 320 x 240 NTSC output, double buffering, scrolling, and
  render-packet management.
- [`assets/ui/`](../assets/ui/README.md), `scripts/generate-assets.py`: editable
  masks, font attribution, and deterministic indexed-atlas generation
  (Python 3, no extra packages).
- `src/ui_style.h`: shared screen geometry and grayscale roles.
- `src/main.c`: Input history consumption, editor processing, START, and frame/status updates.
- `build/{debug,release}/psx-grid.{elf,exe}`: ELF and PS-X EXE outputs.

The Rust crate targets `mipsel-sony-psx` and builds `core` from pinned
`rust-src` with `noabicalls` to match the SDK's fixed GP. CMake links its
static library into the game and fixtures.
`scripts/elf2x-rust.py` omits Rust's GNU_STACK metadata from the temporary ELF
passed to the SDK converter. The linked ELF itself is unchanged. Host tests
link the same Rust sources as a native static library.

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

## Channel model

The score contains eight shared sound configurations, independently of its
16 lane slots and 12-note voice pool. Regular lanes store zero-based channel
indices; branch channel lookup follows Jump ancestry. Assignments and sound
edits use the same revision/publication boundary as tile edits. The sequencer
resolves playback from each runner's regular origin and maintains a working
sound bank per slice, with ordered Relative Locks confined to that channel.
The editor captures the selected channel when opening Sound; tempo and reverb
Size/Amount remain global. See [usage.md](usage.md) for selection and editing.

## Synthesis contract

Each logical note owns a fixed pair of hardware voices throughout its lifetime.
The portable driver passes a root bank and captured wave choices at start,
then paired gains and a shared pitch register without retriggering. Flush masks
use logical slots; the platform expands them to hardware pairs. Gate tokens and
pending gate-offs address logical notes. Each note also captures its reverb
send for both hardware voices. One shared network uses global Size/Amount;
channel sound publication affects future notes, while held/releasing notes
retain their sends and the shared wet return remains independent of them.
Relative Locks change only amplitude attack/release; the remaining captured
settings pass through unchanged.

Asset generation retains ten octave-root banks and selects one bank for the
whole clamped sweep trajectory. Generated control tables implement normalized
Snap decay and pitch conversion without floating point in the timer callback.
The existing absolute clock, service cadence, and 1 ms dispatch deadline remain
the timing contract checked by the emulator fixtures.

The current wavetable measurements and remaining listening/UI checks are in
[validation.md](validation.md). Its historical sections describe the preceding
sine implementation and must not be used as paired-voice measurements.

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
Channel checks read the actual pair-send registers during mixed wet/dry
playback, publication, release, reuse, stop, disconnect, and restart. They
also check 16 runners with 12 simultaneous notes across all eight channels.
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

## HTTP and Lua editor control

For pad-driven checks of the ordinary editor without computer-use, see
[emulator-automation.md](emulator-automation.md). It includes a reproducible
web/Lua smoke runner, GPU capture commands, and pinned-version limitations.

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
stopped playback, a 12-note score, consumption delayed by eight rendered frames,
a held-button disconnect/reconnect, and a 4,096-tile overloaded score. Logs are written to
`build/validation/input-{debug,release}-{digital,analog}-emulator.log`.
Host tests separately exercise queue wraparound, overflow recovery and editor
mode transitions. Manual controller feel and physical-console communication
remain separate checks.

Platform initialization proceeds from rendering (SDK IRQ setup), to audio
(Timer 2 clock and the periodic service), to pad callbacks. The shared timer
services both audio and deferred pad transfers even when playback is stopped.
The editor consumes completed pad reports on the main thread; rendering and
score transactions never run inside the input callbacks. Port 2, rumble and analog-axis editing are not implemented. Port 1 memory-card
access uses a modal BIOS session as described below.

## Score storage verification

The [storage design](score-storage-design.md) owns the v1 layout and behavior.
`./scripts/test.sh` includes golden codec/compatibility cases, exact capacity
boundaries, in-memory file failure injection, menu/input checks, and event
traces for replacement seams. The full-pool audio/input and dense renderer
fixtures still deliberately bypass admission to retain overload coverage.

The BIOS file path is the sole production card path. The pinned OpenBIOS can
exercise normal Save/Load and persistence across emulator process restarts.
It does not implement `_card_clear()`, which blocks the tested unformatted-card
scenario. Build isolated fixtures with disposable cards:

```sh
source scripts/env.sh
cmake --preset debug -DSTORAGE_FIXTURE=ON -DBIOS_CARD_FIXTURE=ON
cmake --build build/debug
python3 scripts/test-card-bios-emulator.py debug
python3 scripts/test-storage-emulator.py debug
python3 scripts/test-storage-emulator.py debug missing
python3 scripts/test-storage-emulator.py debug held
python3 scripts/test-storage-emulator.py debug remove
cmake --preset release -DSTORAGE_FIXTURE=ON -DBIOS_CARD_FIXTURE=ON
cmake --build build/release
python3 scripts/test-card-bios-emulator.py release
python3 scripts/test-storage-emulator.py release
```

For the unformatted-card case, set `PCSX_REDUX_BIOS` to a BIOS that implements
`_card_clear()`, then run the `unformatted` scenario. Physical-card behavior
remains a separate gate.

The runner creates fresh private cards under `build/validation`, saves via the
application's menu request/coordinator, restarts the executable, and verifies
content plus the directory generation. Error scenarios check input resumption
and stopped transport; `remove` detaches the card during Save and checks recovery
of the previous generation, while `held` exercises the release guard with
Cross/START held across I/O. The normal case
checks zero service/pad polling under BIOS ownership and stack watermarks. These scripts never use interactive
application cards. The fixture executable must only be used with disposable
cards: it writes logical slot 01. A fixture timeout is an external test bound;
it cannot recover a synchronous BIOS call stuck inside the application.
The isolated card fixture runs before the application fixture, without audio
or custom pad setup, and records SDK revision and emulator/BIOS hashes beside
its two runs across an application restart.

The separate replacement fixture exercises pending publication, Stop/disconnect,
repeated Load, and reverb memory behavior under twelve-note load:

```sh
source scripts/env.sh
cmake --preset debug -B build/replacement-debug -DREPLACEMENT_FIXTURE=ON
cmake --build build/replacement-debug --target replacement-fixture
python3 scripts/test-replacement-emulator.py replacement-debug
cmake --preset release -B build/replacement-release -DREPLACEMENT_FIXTURE=ON
cmake --build build/replacement-release --target replacement-fixture
python3 scripts/test-replacement-emulator.py replacement-release
```

Current results, memory measurements, captures, and remaining hardware/listening
acceptance work are in [validation.md](validation.md).
