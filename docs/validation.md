# Validation Record

Validation date: 2026-09-22 / macOS 27.0 (26A428), Apple Silicon, Apple Clang
21.0.0. Builds use PSn00bSDK v0.24, GCC 16.2.0, binutils 2.47, and the project's
pinned configuration. PCSX-Redux build 250 is installed with bundled OpenBIOS.

## Appearance implementation status

The model, picker, atlas, typography, and plane renderer from
[the appearance plan](ui-appearance-plan.md) are implemented. Debug and Release
builds and host tests pass. **Actual GPU appearance, the emulator editing
walkthrough, and frame timing remain unverified.** The plan's emulator exit
conditions have not been met; host captures below are not emulator evidence.

The existing Jacquard tile illustration was inspected before producing the
native-size studies. A fresh capture of a running Jacquard application was not
obtained. Its source and attribution are recorded with the
[editable assets](../assets/ui/README.md).

## Visual study

- [Jacquard reference illustration](captures/jacquard-tiles.png)
- [16-pixel pitch study](captures/study-16.png)
- [18-pixel pitch study](captures/study-18.png)
- [Host-rendered comparison plane](captures/host-plane.png)
- [Host-rendered picker](captures/host-picker.png)
- [Host-rendered off-screen resize endpoint](captures/host-resize.png)

The selected geometry is 16 x 16 pitch, 13 x 14 bodies, and a 19 x 10 viewport.
At native size and integer enlargement, the relative fader modifier remains
separate, cycle counters remain open, and the two flow arrows differ visibly.
The 18-pixel study adds space without resolving a remaining legibility problem,
so the original navigation density is retained. Single-pixel lattice dots are
visible in these images; their final emulator presentation still needs review.

PSX Grid Bitmap, a custom 5 x 7 monoline face, was selected over offline Jura
at 9 and 10 pixels. Jura's fine strokes were much fainter in this study. The
bitmap sample keeps `0/O`, `1/I`, `C#4`, coordinates, and long guidance readable
on dark and light grounds. Heads use `CH`; the sample shows that `CH1` would
crowd a compact body. This is a visual study result, not a display calibration.

The host renderer replays submitted primitives in reverse ordering-table order,
loads the generated indexed texture and CLUT, and approximates RGB555 output.
It checks texture setup precedes sprites and shows panel/cursor layering. It
cannot reproduce the GPU, emulator scaling, pixel aspect, or timing.

## Automated verification

Reproduce with `./scripts/test.sh` and the Debug/Release commands in the README.
The latest host log is `build/validation/ui-host-tests.log`.

| Area | Result |
| --- | --- |
| Debug / Release | Both produce MIPS ELF and PS-X EXE outputs with no compiler warnings. |
| Model | Existing classification, bounds, collision, capacity, resizing, deletion, and slot-reuse tests pass. |
| Visual kinds | Each of the six explicit IDs is placed and removed; invalid IDs, occupied cells, heads, endpoints, and empty plane reject placement without changing score bytes. Every kind blocks shortening across it. Slot reuse clears kind data. |
| Picker | From an empty editor, create a lane and place all six kinds through input/editor transitions. Browsing and both cancellation steps preserve score bytes, coordinates, and the last successful kind. Selection clamps to both ends. |
| Input | Held Cross does not commit on entry; direction delay/repeat, mode reset, disconnect suspension, and release-to-rearm reconnect behavior pass, including reconnect inside the picker. Scrolling away/back preserves all kind bytes. |
| Repeated editing | Existing 1,000 create/delete cycles pass. |
| Render | 81,954 frames: every plane coordinate in all five modes for full and sparse long lanes, all picker choices at all four plane corners, disconnected guidance, and comparison captures. |
| GPU packets | TILE=16, SPRT=20, DR_TPAGE=8 bytes, matching the SDK. Screen bounds, UV bounds, VRAM uploads, CLUT transparency/grayscale, primitive grayscale, and texture-page ordering pass. |
| Memory | AddressSanitizer and UndefinedBehaviorSanitizer pass; no packet overflow. |

The measured host packet peak is **20,136 / 32,768 bytes** (61.5%) per buffer;
`render_overflows` remains **0**. This replaces the old renderer's 12,752-byte
measurement. Fixed storage, double buffering, and allocation-free frames remain
in use. Debugger-visible `render_packet_peak` and `render_overflows` are retained.
The measurement includes the sparse-rail workload and full picker overlay.

The previous setup validation verified pinned hashes and successful idempotent
installation. Its logs remain under `build/validation`. No dependency version
was changed for this appearance work. Python 3 now generates the atlas during
builds; Pillow is only an optional dependency for the comparison study.

## Reproducing the comparison arrangement

The normal executable always starts empty. To reproduce the plane capture
through ordinary controls:

1. Create a lane at `(1,1)`.
2. On steps `(2,1)` through `(7,1)`, place Note, Absolute Parameter, Relative
   Parameter, Cycle Gate, Probability Gate, and Jump respectively.
3. Change the lane length to 10, leaving four empty steps and the endpoint at
   `(12,1)`. Leave the cursor on Jump at `(7,1)`.
4. Move to `(8,1)` and open the picker for the overlay comparison; cancel it
   twice and open Change Length, increasing its candidate to 64, for the
   off-screen endpoint comparison.

The render test constructs the same arrangement for repeatable host captures,
writing `build/tests/{plane,picker,resize}.pgm`. Convert these directly to PNG
with an image tool to refresh the checked-in host images. Do not smooth when
inspecting integer enlargements. The test fixture is not part of startup.

## Emulator and outstanding acceptance

Launching the new Debug executable via `scripts/run.sh` started PCSX-Redux.
Both full-path and display-name connections through the UI automation tool
returned `Invalid app`. No screen or controller interaction could be obtained.
The process was stopped after this attempt; the launch log is
`build/validation/ui-emulator.log`. Launching a process alone is not a passed
emulator test. The earlier baseline's emulator checks were also unverified.

Still required on Debug and Release:

- Run the ordinary empty-start editing walkthrough, placing/deleting all six
  kinds and scrolling away/back with the gamepad.
- Inspect native and normal emulator presentations against Jacquard, including
  text on both polarities, note borders, gate counters, relative modifiers,
  selection on bright tiles, and panel corners.
- Check lattice alignment and rail continuity while scrolling, candidate
  endpoints and arrows, held buttons, cancellation, and controller reconnection.
- Verify atlas uploads, transparency, draw order, and buffer switching on the
  actual GPU path. Observe packet counters and frame behavior under full and
  sparse load; host results do not establish a frame-rate measurement.
- Obtain a representative running Jacquard capture for the final comparison.

No physical-console testing has been performed.

## Scope limits

Kinds describe appearance only. Audio, playback, musical tile behavior,
parameter editing, jump destinations/connections, stacks, saving, and loading
remain outside this prototype. There is one tile per step, up to 16 lanes of
1–64 steps, on a 128 x 64 plane. Output is 320 x 240 NTSC; PAL repeat tuning,
lowercase, Japanese, and general Unicode rendering are not implemented.
