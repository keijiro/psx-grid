# psx-grid

A GUI prototype for editing Jacquard's Score Plane with a PlayStation gamepad.
It starts with an empty 128 x 64-cell plane. Audio generation, playback, and
saving are outside its scope.

## Setup and Execution

The setup requires an Apple Silicon Mac, Xcode Command Line Tools, Homebrew,
and Git. It uses the same pinned dependencies as the `../psx-test` reference
project: PSn00bSDK v0.24, MIPS GCC 16.2.0, binutils 2.47, PCSX-Redux build 250,
and its bundled OpenBIOS. See [toolchain.lock](toolchain.lock) for the pinned
details.

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
to a gamepad or keyboard. Editing pauses while the controller is disconnected
and while the application waits for held buttons to be released after a
reconnection.

## Controls

| State | D-pad | X / Cross | O / Circle |
| --- | --- | --- | --- |
| Plane | Move the cursor; scroll at edges | Open menu | No action |
| Menu | Select an item with Up/Down | Execute | Close |
| Change length | Change the candidate with Left/Right | Confirm | Cancel |
| Delete lane confirmation | Select Cancel/Delete with Left/Right | Confirm | Cancel |

1. At the initial position `(1,1)`, press X and confirm `NEW LANE` with X.
2. Move right, then select `PLACE TILE` with X to place a tile.
3. At the same position, select `DELETE TILE` with X to remove only the tile.
4. Select `CHANGE LENGTH` to resize the lane. A yellow outline marks the
   candidate endpoint. An off-screen endpoint appears as an edge arrow with
   its `END` coordinate. Press Circle to return without changing the data.
5. Return to the green lane head, then select `DELETE LANE`, Right, and X to
   delete the lane and its tiles.

The lane head is a green square, its endpoint is an orange vertical line, a
regular tile is a blue square, and the cursor is a white outline. A held
direction starts repeating after 18 frames and then repeats every 3 frames
(about 300 ms and 50 ms under NTSC). Opposite directions cancel each other;
horizontal movement takes precedence when both axes are active.

The model supports up to 16 lanes of 1-64 steps each; new lanes contain 16
steps. A lane occupies its head, every step, and its endpoint. The editor
rejects collisions, positions outside the plane, and capacity overflows. If a
shorter length would discard tiles, it asks the user to delete those tiles
first. Menus and canceled operations preserve the cursor position.

## Structure and Validation

- `src/score.*`: SDK-independent model and edit validation.
- `src/input.*`: Button presses, repeats, disconnection, and reconnection.
- `src/editor.*`: State transitions, menus, candidate lengths, and deletion
  confirmation.
- `src/render.*`: 320 x 240 NTSC output, double buffering, scrolling, and
  render-packet management.
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
[docs/validation.md](docs/validation.md) for verified behavior and remaining
work.
