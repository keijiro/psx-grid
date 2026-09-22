# Validation Record

Validation date: 2026-09-22 / macOS 27.0 (26A428), Apple Silicon, Apple Clang
21.0.0. The host fields in `toolchain.lock` describe the environment used when
the pinned configuration was created for the reference project. This project
also used PSn00bSDK v0.24, GCC 16.2.0, binutils 2.47, and PCSX-Redux build 250.

## Verified

| Area | Result |
| --- | --- |
| Setup | Installed the SDK and emulator inside the project. A second run succeeded and skipped rebuilding the SDK. |
| Pinned inputs | Verified the SDK and major submodule commits, patches, distributed DMG, and OpenBIOS SHA-256 hashes. |
| Debug / Release | Generated `psx-grid.elf` and `psx-grid.exe` for both configurations and identified them as MIPS-I ELF and Sony PlayStation EXE files. |
| Model | Covered head, endpoint, empty-step, and tile classification; boundaries; overlap; minimum and maximum lengths; the 16-lane limit; tile protection; deletion; and slot reuse. |
| Failure atomicity | Compared the model byte for byte after rejected creation, extension, shortening, and placement operations. |
| Input | Covered initial movement, the 18-frame delay, the 3-frame repeat interval, direction changes, cancellation of opposite directions, horizontal priority, holding X, and waiting for button release after connection. |
| Editing | Covered create, place, delete, resize, confirmed lane deletion, cancellation, cursor retention, plane edges, suspension while disconnected, and 1,000 edit cycles. |
| Render stub | Rendered 32,769 frames: every position on the 128 x 64 plane in four states, plus the disconnected state, with 16 lanes of 64 tiles. |
| Memory | Passed the host tests under AddressSanitizer and UndefinedBehaviorSanitizer. |

Reproduce these results with the build commands in the README and
`./scripts/test.sh`. Logs are stored in
`build/validation/{setup,setup-rerun,host-tests}.log`.

The SDK emits a GNUInstallDirs developer warning during CMake configuration,
but the build succeeds. The render source alone uses `-fno-strict-aliasing`
because an SDK macro accesses the GPU DMA tag through a different type.

## Render Volume

Each buffer is 32,768 bytes. The GPU stub uses the same primitive sizes as the
SDK: TILE=16, SPRT_8=16, and DR_TPAGE=8 bytes. The highest reservation measured
by the tests was **12,752 bytes**. The tests verified that all shape and text
coordinates remain within 320 x 240 and that bounds are checked before each
reservation. Every drawing call checks capacity first and skips the write if
the buffer would overflow. The debugger exposes `render_packet_peak` and
`render_overflows`.

The viewport contains 19 x 10 cells and 31 grid lines. Even with the
conservative assumption that every cell uses a rail and two shapes, the cell
area consumes 9,120 bytes. Text reservations use the actual byte length. The
model, input state, and render packets use fixed storage; no allocation occurs
per frame.

The host stub cannot validate execution on the actual GPU, draw order, font
composition, or VSync timing.

## Emulator and Outstanding Checks

Launching the Debug and Release configurations through this project's
`scripts/run.sh` started PCSX-Redux processes that remained running. Logs are
stored in `build/validation/pcsx-*.log`. UI automation could not connect to the
application because both its path and name produced `Invalid app`. Standard
output contained font-search messages, but no log established that the EXE was
visible or that its game loop had started. **The emulator acceptance test is
therefore not recorded as passed.** The processes started for validation have
been stopped.

The following checks still require direct interaction in both Debug and
Release configurations:

- Complete the README walkthrough from the initial empty plane.
- Inspect menus in all four corners, the off-screen endpoint of a long lane,
  and visibility while scrolling away and back.
- Hold and combine D-pad directions, hold X, cancel with Circle, and disconnect
  and reconnect the controller.
- With multiple lanes, inspect collision, shortening protection, and limit
  rejection messages.
- Continue editing under maximum load while checking that display and data
  remain consistent, updates occur on every VSync, and the measured frame rate
  remains acceptable.
- Evaluate the interaction with the 18/3-frame repeat timing, 16-pixel cell
  spacing, and current colors.

No physical-console testing has been performed. The hands-on tuning and
acceptance work in Step 5 of the implementation plan remains outstanding.

## Differences from Jacquard and Known Limitations

The prototype provides one kind of regular tile and allows at most one tile per
step. It supports up to 16 horizontal lanes of 1-64 steps on a 128 x 64 plane.
New lanes have a fixed length of 16; the editor neither shortens them
automatically nor moves them to another row. A lane with its head at `(x,y)`
places its first step at `(x+1,y)`.

The prototype omits stacks, branches, moving, copying, and automatic extension
from the endpoint. Audio, sequence playback, tile behavior, parameters, saving,
and loading are also outside its scope. Output is fixed at 320 x 240 NTSC with
short English labels; repeat timing has not been adjusted for PAL. When a
candidate endpoint is off-screen, a yellow edge outline, arrow, and `END`
coordinate indicate its position.
