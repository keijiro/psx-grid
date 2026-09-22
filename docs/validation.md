# Validation Record

Validation date: 2026-09-22, macOS 27.0 / Apple Silicon. Host tests use Clang
with AddressSanitizer and UndefinedBehaviorSanitizer. Console builds use the
pinned PSn00bSDK v0.24 toolchain and the Debug and Release presets.

## Score editor verification

Both console configurations build without warnings. The host suite passes
model, input/editor, and renderer checks. Reproduce with `./scripts/test.sh`
and the build commands in [development.md](development.md). The current host
log is `build/validation/score-editor-host-tests.log`.

| Area | Automated coverage |
| --- | --- |
| Properties | Pitch, duration, period, and chance bounds; remembered note values; cycle switches retained outside a shortened period; division choices and branch inheritance |
| Stacks | Insertion into occupied stacks through movement, interior deletion, upward/downward reordering, moving suffixes between steps, terminator extension, independent clipboard values |
| Branches | Four-step creation, source ownership, inherited division, moving heads without moving connections, descendant cycle rejection, deleting a branch/source/regular parent |
| Atomic edits | Byte-for-byte preservation on collisions, boundary failures, lane capacity, tile capacity, partial-paste capacity failure, and invalid moves; planning without score mutation |
| Capacity | An actual 4,096-tile score; stack depth to row 63; 16 lanes; deletion and pool reuse; 1,000 create/place/delete cycles |
| Input/editor | X tap release, simultaneous X/direction, hold/release movement, invalid drops, Circle cancellation, disconnect cancellation, all-buttons-up reconnect, direction repeat and repeat reset, property cancellation and pattern confirmation |
| Rendering | Every plane coordinate in all 12 editor modes with maximum tile density; sparse lanes and all modes at plane corners; 14 jump connections; host captures |
| GPU packets | SDK-sized packets, screen and texture bounds, CLUT transparency and grayscale, ordering-table texture setup, panel layering, no packet overflow |

The renderer submits 98,420 tested frames. The measured packet peak is
24,528 / 32,768 bytes per buffer (74.9%), with zero overflows. The result is
also recorded at the end of the host log.
`render_packet_peak` and `render_overflows` remain visible to a debugger.
Fixed storage and allocation-free frames remain in use.

## Visual evidence

The current host renderer captures are:

- [Stack and branch plane](captures/host-plane.png)
- [Four-kind picker](captures/host-picker.png)
- [32-lap cycle pattern](captures/host-pattern.png)
- [Off-screen resize endpoint](captures/host-resize.png)

These images replay submitted GPU primitives in ordering-table order and
approximate RGB555 output. They check panel occlusion and label placement,
but do not establish actual GPU behavior, emulator scaling, or frame timing.

The original [Jacquard illustration](captures/jacquard-tiles.png) and
[16-pixel](captures/study-16.png) / [18-pixel](captures/study-18.png) studies are
historical design references. They include parameter tiles removed from the
current editor. Asset provenance and licensing are in
[assets/ui/README.md](../assets/ui/README.md).

## Emulator and physical console

PCSX-Redux build 250 was launched with the Debug executable using
`scripts/run.sh`. Computer-use access failed with `Invalid app` for both the
application path and display name. Window enumeration is unavailable through
that interface. The launch log is
`build/validation/score-editor-emulator.log`; launch alone is not verification.

**The emulator walkthrough remains unverified.** Run the chord/gate, property,
copy, cross-lane move, and branch deletion sequence in
[usage.md](usage.md#walkthrough) on Debug and Release. Also inspect edge menus,
valid/invalid movement previews, off-screen connections, reconnect behavior,
and frame timing at high density on the actual emulator GPU path.

**Physical-console testing remains unverified**, independently of the emulator
check. Neither host rendering nor a successful MIPS build substitutes for it.

## Platform limits

Output remains 320 by 240 NTSC. PAL timing, lowercase, Japanese, and general
Unicode rendering are not implemented. See [usage.md](usage.md) for editing
limits and the intentionally excluded audio and persistence features.
