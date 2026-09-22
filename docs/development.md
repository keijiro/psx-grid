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
`Configuration > Controls` to assign Port 1's D-pad, Cross, and Circle buttons
to a gamepad or keyboard. See [usage.md](usage.md) for the editing walkthrough.

## Structure and Validation

- `src/score.*`: SDK-independent model and edit validation.
- `src/input.*`: Button presses, repeats, disconnection, and reconnection.
- `src/editor.*`: State transitions, menus, candidate lengths, and deletion
  confirmation.
- `src/render.*`: 320 x 240 NTSC output, double buffering, scrolling, and
  render-packet management.
- [`assets/ui/`](../assets/ui/README.md), `scripts/generate-assets.py`: editable
  masks, font attribution, and deterministic indexed-atlas generation
  (Python 3, no extra packages).
- `src/ui_style.h`: shared screen geometry and grayscale roles.
- `src/main.c`: Controller polling and the frame loop.
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
renderer owns presentation. Visual tile kinds identify appearance only; the
model does not depend on atlas coordinates or grayscale values. Asset
provenance and licensing live with the [editable sources](../assets/ui/README.md).
Visual study results and remaining acceptance checks are in
[validation.md](validation.md).
