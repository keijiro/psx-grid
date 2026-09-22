# Validation Record

Validation dates: 2026-09-22 (editor baseline) and 2026-09-23
(input regression and refreshed audio measurements), macOS 27.0 / Apple Silicon. Host tests use Clang
with AddressSanitizer and UndefinedBehaviorSanitizer. Console builds use the
pinned PSn00bSDK v0.24 toolchain and the Debug and Release presets.

## Score editor verification

Both console configurations build without warnings. The host suite passes
model, input/editor, and renderer checks. Reproduce with `./scripts/test.sh`
and the build commands in [development.md](development.md). The current host
log is `build/validation/audio-host-tests.log`.

| Area | Automated coverage |
| --- | --- |
| Properties | Pitch, duration, period, and chance bounds; remembered note values; cycle switches retained outside a shortened period; division choices and branch inheritance |
| Stacks | Insertion into occupied stacks through movement, interior deletion, upward/downward reordering, moving suffixes between steps, terminator extension, independent clipboard values |
| Branches | Four-step creation, source ownership, inherited division, moving heads without moving connections, descendant cycle rejection, deleting a branch/source/regular parent |
| Atomic edits | Byte-for-byte preservation on collisions, boundary failures, lane capacity, tile capacity, partial-paste capacity failure, and invalid moves; planning without score mutation |
| Capacity | An actual 4,096-tile score; stack depth to row 63; 16 lanes; deletion and pool reuse; 1,000 create/place/delete cycles |
| Input/editor | X tap release, simultaneous X/direction, hold/release movement, invalid drops, Circle cancellation, disconnect cancellation, all-buttons-up reconnect, direction repeat and repeat reset, property cancellation and pattern confirmation |
| Rendering | Every plane coordinate in all 19 editor modes with maximum tile density; sparse lanes and all modes at plane corners; 14 jump connections; host captures |
| GPU packets | SDK-sized packets, screen and texture bounds, CLUT transparency and grayscale, ordering-table texture setup, panel layering, no packet overflow |

The renderer submits 155,794 tested frames. The measured packet peak is
24,788 / 32,768 bytes per buffer (75.6%), with zero overflows. The result is
also recorded at the end of the host log.
`render_packet_peak` and `render_overflows` remain visible to a debugger.
Fixed storage and allocation-free frames remain in use.

## Visual evidence

The current host renderer captures are:

- [Stack and branch plane](captures/host-plane.png)
- [Five-kind picker](captures/host-picker.png)
- [32-lap cycle pattern](captures/host-pattern.png)
- [Off-screen resize endpoint](captures/host-resize.png)
- [Signed lock offset](captures/host-lock.png)
- [Global sound settings](captures/host-sound.png)

These images replay submitted GPU primitives in ordering-table order and
approximate RGB555 output. They check panel occlusion and label placement,
but do not establish actual GPU behavior, emulator scaling, or frame timing.

The original [Jacquard illustration](captures/jacquard-tiles.png) and
[16-pixel](captures/study-16.png) / [18-pixel](captures/study-18.png) studies are
historical design references. They include parameter tiles removed from the
current editor. Asset provenance and licensing are in
[assets/ui/README.md](../assets/ui/README.md).

## Interactive emulator and physical console

### Computer-use blocker

Observed again during audio playback validation on 2026-09-22, using
PCSX-Redux build 250 on macOS 27.0 / Apple Silicon. The emulator was launched
with the Debug executable through `scripts/run.sh`; its running process was
confirmed separately. The `mcp__cua_repl` computer-use tool then rejected both
application selectors:

```javascript
await cua.getApp('/Users/keijiro/Projects/psx/psx-grid/.local/PCSX-Redux.app');
// Invalid app: /Users/keijiro/Projects/psx/psx-grid/.local/PCSX-Redux.app

await cua.getApp('PCSX-Redux');
// Invalid app: PCSX-Redux
```

`cua.getState()` successfully returned an application inventory, but PCSX-Redux
was absent from it. The macOS interface does not expose the window-enumeration
fallback available on Linux and Windows. No application binding was obtained,
so screenshots, accessibility inspection, and controller/menu interaction
through computer-use could not proceed.

The cause is unresolved. These results do not establish an emulator crash or
a permissions or bundle-identifier problem; the failure occurs when selecting
the application through computer-use. The earlier editor launch log is
`build/validation/score-editor-emulator.log`, and the audio-stage GUI launch log
is `build/audio-emulator.log`. Neither launch log substitutes for UI verification.

For automated audio checks, `scripts/test-audio-emulator.py` successfully runs
the same pinned emulator in headless mode. That workaround covers the numeric
fixtures recorded below, but does not restore computer-use access. To close
this blocker, obtain a working application binding and capture its actual UI,
or complete and record a manual emulator walkthrough.

**The interactive emulator walkthrough remains unverified.** Run the chord/gate, property,
copy, cross-lane move, and branch deletion sequence in
[usage.md](usage.md#walkthrough) on Debug and Release. Also inspect edge menus,
valid/invalid movement previews, off-screen connections, reconnect behavior,
and frame timing at high density on the actual emulator GPU path.

**Physical-console testing remains unverified**, independently of the emulator
check. Neither host rendering nor a successful MIPS build substitutes for it.

## Platform limits

Output remains 320 by 240 NTSC. PAL timing, lowercase, Japanese, and general
Unicode rendering are not implemented. See [usage.md](usage.md) for editing
limits and the intentionally excluded persistence and transport features.

## Audio playback verification

The playback implementation now has an SDK-independent sequencer/voice test,
an independent ADPCM decoder test, and a standalone PCSX-Redux SPU/timer fixture.
The host suite uses ASan/UBSan; both console configurations build without warnings.
Logs are generated under `build/validation/audio-host-tests.log` and
`audio-{debug,release}-emulator.log`. Reproduce with the commands in
[development.md](development.md#audio-fixture).

Host coverage includes all twelve divisions over 2,048 boundaries each, times
beyond the 32-bit clock range, fractional gates, overlapping notes, seeded gate
traces, nested/multiple jumps, origin order, ordered lock saturation, chord-local
locks, cross-lane holds and exact expiry, snapshot independence, split-stack
catch-up, full-pool rollback, stale gate-offs, zero/long envelopes, release during
attack, deterministic stealing, same-time off/on, and silence after stop.
START is checked in every editor mode, including the held/reconnect guard and
uncommitted candidates. Renderer tests include the larger picker, global Sound,
engagement switches, signed limits, and both playback status strings.

The generated ten octave-root loops cover C0–C9. The calculated pitch-register
error is at most **0.151 cents** before SPU interpolation. Independently decoding
three traversals produces identical samples at each loop position, using both
rounded prediction and Redux's separate predictor truncations. Decoded SNR is
**24.47–59.92 dB** against the source sine; worst loop-boundary slope error is
630 sample units. These are compressed approximations, particularly at high
pitches, and do not establish mathematical sine purity or analog output quality.
The fixture separately reads nonzero SPU decoded-buffer samples for C0, C4,
and C9. It checks software volume timing via actual SPU register readback.

Both headless emulator fixtures pass their numeric assertions, including a
24-note simultaneous chord with no skipped starts. Measured peaks:

| Measurement | Debug | Release |
| --- | ---: | ---: |
| C4 service cost | 0.148 ms | 0.148 ms |
| C4 service interval | 0.316 ms | 0.298 ms |
| C4 key-on lateness | 0.239 ms | 0.240 ms |
| 24-note chord service cost | 0.606 ms | 0.606 ms |
| 24-note chord service interval | 0.612 ms | 0.611 ms |
| 24-note chord key-on lateness | 0.665 ms | 0.690 ms |
| Maximum-density service cost | 1.117 ms | 1.119 ms |

The normal fixtures remain below the 1 ms dispatch and service-interval targets
while the GPU frame loop scrolls and asynchronous pad polling is enabled. Normalized sixteen-step loop duration from
the observed C4 key-on span is 1.999991 s / 1.999996 s (Debug / Release).
This short emulator trace supplements the longer host clock tests; it does not
establish long-session wall-clock or analog output timing.

| Requested time | Debug Attack / Release | Release Attack / Release |
| --- | ---: | ---: |
| 0 ms | 0.075 / 0.230 ms | 0.214 / 0.001 ms |
| 1 ms | 1.289 / 1.076 ms | 1.014 / 1.002 ms |
| 5 ms | 5.091 / 5.081 ms | 5.053 / 5.046 ms |
| 100 ms | 100.221 / 100.089 ms | 100.003 / 100.136 ms |

All 48 left/right voice volume registers are zero after ordinary and overloaded
stops. ADSR level readback can lag the emulator's audio thread, so the stop
assertion uses the output volume registers rather than assuming that the held
hardware envelope itself has already read back as zero.

The linker reports 634,568 bytes of BSS in each editor build. Total text/data/BSS
is 697,378 bytes in Debug and 690,706 in Release. The Debug image ends at
`0x800ba43c`, leaving about 1.27 MiB above it before the top of console RAM.
This includes three 182,712-byte scores, the editor clipboard, held-lock/voice
state, the fixed input history, two 32 KiB render packet buffers, and the SDK's
4 KiB interrupt stack.
No event queue grows with playback duration.

The SPU hardware ADSR holds the sample open; editable envelopes use timer-driven
voice volume. The timing table measures reaching the peak and zero volume,
including polling/service quantization. It does not measure the analog waveform
or certify the SPU's first-sample latency. The software envelope has 513 amplitude
levels, so long ramps are audibly quantized; 16-second and interrupted attacks
are additionally covered by the host voice tests.

The 4,096-tile fixture consists of sixteen division-64 lanes with four steps and
64 notes in each stack. It intentionally exceeds the voice and service budgets.
Its due steps continue through bounded catch-up, overdue starts are suppressed,
and disconnect still silences playback. It is an overload/recovery test, not a
claim that every maximum-density chord can sound. Arbitrarily long interrupt
masking, clock-wrap loss, and sustained maximum-density musical output are not
accepted operating conditions.

Remaining acceptance work is explicit: listen to the full pitch range and loops,
check audible retrigger clicks/distortion and the conservative mix on chords,
exercise the interactive lock/gate/branch and controller walkthrough on both
configurations, and measure longer sessions while editing. The
[computer-use blocker](#computer-use-blocker) prevents automated UI verification;
headless fixtures do not establish that interactive walkthrough. Physical-console audio, clock,
controller, and display validation remain entirely separate and unverified.

## Input regression (2026-09-23)

The previous BIOS polling path could leave a connected pad's button buffer
unchanged across video frames. A diagnostic Debug run observed 600/600 updates
before audio initialization, 517/600 with the audio timer running while stopped,
481/600 during playback, and 600/600 after disabling that timer. Connection
status alone did not reveal the missing updates.

The replacement driver was tested through PCSX-Redux's actual SIO path, with
Lua injecting controller buttons rather than modifying application input memory.
Both Debug and Release pass with digital (`0x41`) and analog (`0x73`) reports:

| Scenario | Injected taps | Detected taps in each configuration/device |
| --- | ---: | ---: |
| Playback stopped | 100 | 100 |
| 24-voice playback | 100 | 100 |
| Consumption delayed by eight rendered frames | 100 | 100 |
| Reconnect held, release, then a fresh tap | 1 | 1 |
| 4,096-tile overloaded playback | 10 | 10 |

Each tap holds Right, Cross and START for one emulated video frame. Direction,
Cross press, Cross release and START counts all match independently. There are
no queue overflows or connected-controller timeouts, including during overload.
Completed-report counts match poll counts to within one in-flight transaction
at measurement boundaries. Deliberate disconnection produces 16 disconnected
reports; held reconnect produces no actions before release.

The host suite also passes queue wraparound and overflow cancellation, ordered
menu gestures, new-direction response immediately after a mode change, and
repeat suppression for a direction already held. The refreshed audio fixture
passes its existing dispatch, envelope, loop-duration, overload and stop checks.
Commands and log locations are in [development.md](development.md#input-fixture).

These tests establish emulated digital and DualShock-mode communication and
input event delivery. Physical controllers/adapters, the `0x53` analog-joystick
report type, and subjective interaction latency remain unverified.
