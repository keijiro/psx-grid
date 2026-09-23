# Validation Record

## Wavetable verification (2026-09-23)

The paired wavetable path was checked separately from the earlier sine player.
The current host log is `build/validation/wavetable-host-tests.log`; asset and
trajectory reports are generated at `build/generated/wave_samples.txt`.
The tests use production model, sequencer, synthesis, and rendering code with
AddressSanitizer and UndefinedBehaviorSanitizer.

### Assets and portable synthesis

All 50 waveform/bank combinations generate deterministically. Independent
ADPCM decoding checks the first and two repeated traversals with both rounded
prediction and Redux's separate predictor truncations. Tests also verify the
emitted bytes, loop flags, aligned addresses, and zero DMA padding. The bank
occupies 16,080 bytes, padded to 16,128, at SPU addresses `0x1000..0x4eff`.
The largest decoded peak is 14,419 across both decoders. Twelve coherent pairs
at the fixed gain imply a worst decoded-sample sum of 5,407.125 / 32,768;
this is a computed bound, not a capture of the final mixed or analog output.
The initial asset check exposed a sine loop-junction regression, which was
corrected in the encoder before rerunning the checks.
Minimum decoded SNR is 20.30 dB across all waves and 25.16 dB for sine;
shape-relative boundary error and DC measurements are included in the host log.

The production fixed-point path was compared with the analytical Snap formula
at **13,726,370 samples**: all 109 pitches, all 49 signed depths, decays of
0, 1, 7, 200, and 2,000 ms, and 514 times per combination. Maximum total error
was **1.113992 cents**, below the 2-cent target. Independently measured maxima
were 0.397963 cents for ideal register rounding and 1.019719 cents for
fixed-point approximation before register rounding. These maxima occur at
different points and are not additive measurements. Monotonicity, endpoint
clamping, exact return to the stored base register, and absolute times beyond
the 32-bit clock range also pass.

Mix tests cover zero/nonzero combinations, exact boundaries, equal-wave
complementary gains, and gates before and after control-envelope completion.
Amplitude tails, stop ramps, 12-note allocation, stealing, stale tokens, and
existing sequencer/lock/live-edit regressions remain covered. New publication
checks retain all settings on sounding notes and apply the complete new sound
to the next scheduled note while amplitude locks leave other fields unchanged.

### Console builds and audio emulator

Debug and Release build with `AUDIO_FIXTURE=ON` without warnings. Both expanded
PCSX-Redux audio fixtures pass. Logs are
`build/validation/audio-{debug,release}-emulator.log`; reproduction commands
are in [development.md](development.md#audio-fixture).

Actual SPU readback checks all five waveforms, matching paired registers and
12-note simultaneous starts, plus positive/negative 24-semitone sweeps with
200 ms decay and independent 100/100 ms mix envelopes. Samples use the last
completed audio-service timestamp, so main-thread logging does not distort
trajectory comparisons. C4 endpoint registers are independently checked as
16,329 for the downward sweep bank and 4,082 for the upward sweep bank.
The fourteen sweep readbacks per build have maximum analytical pitch errors
of 1.041 cents in Debug and 0.715 cents in Release.
Decoded voice-buffer captures are nonzero for every waveform and remain below
clipping. Their short windows do not measure the maximum final mixed output;
the complete-loop decoder bound above is the conservative headroom evidence.

| Measurement | Debug | Release |
| --- | ---: | ---: |
| C4 service cost | 0.140 ms | 0.139 ms |
| C4 service interval | 0.300 ms | 0.302 ms |
| C4 key-on lateness | 0.225 ms | 0.240 ms |
| Swept 12-note chord service cost, maximum | 0.565 ms | 0.565 ms |
| Swept 12-note chord service interval, maximum | 0.571 ms | 0.569 ms |
| Swept 12-note chord key-on lateness, maximum | 0.755 ms | 0.722 ms |
| Maximum-density service cost, including live edits | 1.344 ms | 1.346 ms |
| Ordinary revision copy | 18.905 ms | 18.705 ms |
| Dense revision copy | 25.398 ms | 25.398 ms |

Normal cases have no skipped notes or overloads and retain the 1 ms deadline.
The 4,096-tile score deliberately overloads the system, suppresses overdue
starts, and still stops safely. Ordinary revisions are adopted 25/25 times and
dense revisions 32/32 times in each configuration. Coalesced publication and
pending stop/disconnect pass. All 48 left/right output volume registers are
zero after stops, including both swept chords. The main-thread copy still
exceeds one NTSC frame, so these results do not establish smooth editing.

Editor BSS is 887,084 bytes in both configurations. Text/data/BSS totals are
982,558 bytes in Debug and 975,594 in Release; Debug ends at `0x800ffe34`.
These figures include the generated control tables and paired synthesis state.

### Input emulator regression

All four combinations of Debug/Release and digital (`0x41`)/analog (`0x73`)
controllers pass through the actual SIO path. Each configuration detects all
100 Right/Cross/START taps while stopped, all 100 during 12-note playback,
and all 100 with consumption delayed by eight rendered frames. Held reconnect
produces only the one fresh tap after release; the overloaded 4,096-tile case
preserves all ten taps. Direction, Cross press/release, and START counts match
independently. No queue overflows or connected-controller timeouts occur.
Logs are `build/validation/input-{debug,release}-{digital,analog}-emulator.log`.

### Editor and host-rendered UI

All eight sound controls pass grouped navigation, increment and range limits,
transactional validation, cancellation, reentry, revision, and START checks.
The renderer passes **237,790 frames**, with a peak of **24,788 / 32,768 bytes**
and no packet overflow or screen escape. The root menu, four submenus, and
eight property screens were visually inspected at representative limits.
Evidence includes [Waves](captures/host-sound-waves.png),
[Amplitude](captures/host-sound-amplitude.png), [Mix](captures/host-sound-mix.png),
[Pitch](captures/host-sound-sweep.png), and the
[signed sweep property](captures/host-sound-pitch-sweep.png).
These captures replay host GPU packets; they do not verify actual GPU output
or controller feel.

### Remaining acceptance checks

Computer-use access was retried on 2026-09-23 after the headless fixtures.
PCSX-Redux was absent from the app inventory, and both
`cua.getApp('/Users/keijiro/Projects/psx/psx-grid/.local/PCSX-Redux.app')` and
`cua.getApp('PCSX-Redux')` returned `Invalid app`. No application binding was
obtained. The interactive emulator walkthrough therefore remains unverified;
headless input injection and host captures do not resolve that limitation.

Listening to every waveform, A/B transitions, short percussion, long bends,
low/high notes, clicks, and distortion remains unverified. Automated register
and decoder checks do not establish subjective sound quality. Physical-console
SPU, display, clock, and controller behavior also remain unverified.

## Historical validation before wavetable synthesis

The sections below record the preceding implementation, including its 24-note
capacity. They are retained as history and do not establish the paired path's
current memory, timing, or sound quality. Current wavetable results above take
precedence.

Validation dates: 2026-09-22 (editor baseline) and 2026-09-23
(input regression, live editing, and refreshed audio measurements), macOS 27.0 / Apple Silicon. Host tests use Clang
with AddressSanitizer and UndefinedBehaviorSanitizer. Console builds use the
pinned PSn00bSDK v0.24 toolchain and the Debug and Release presets.

## Score editor verification

Both console configurations build without warnings. The host suite passes
model, input/editor, and renderer checks. Reproduce with `./scripts/test.sh`
and the build commands in [development.md](development.md). The current host
log is `build/validation/live-edit-host-tests.log`.

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

## Live editing verification (2026-09-23)

Debug and Release build without warnings, and the complete ASan/UBSan host
suite passes. The current host log is
`build/validation/live-edit-host-tests.log`. New coverage includes publication
between split slices, retained positions/laps, absolute deadlines and random
state, live gate/cycle and division changes, held-lock edits/order and ID reuse,
branch deletion, lane shortening/removal/recreation, head ordering, master
changes, pending-lane admission, and empty playback. Model tests check stable
birth generations across edits/moves, failed branch-allocation rollback,
reused tile/lane IDs, and generation exhaustion.

The actual SPU/timer fixture accepts 25/25 ordinary revisions and 32/32 dense
revisions in each build while playback stays active. Its ordinary pitch edits
change the SPU frequency register from 4,082 to 6,117. The dense case fills all
4,096 tile slots and reverses the sixteen lane heads at every revision, covering
runner reconciliation and execution-order changes during overload. A separate
case queues a second edit while the first revision is pending, then checks that
regular updates without further edits deliver the final revision. START-stop
and disconnect with a revision pending both cancel publication and leave every
voice's output volumes at zero.

The score copy runs with interrupts enabled but still occupies the main thread
for roughly 19 ms in ordinary playback and 27 ms under maximum density. This
exceeds one NTSC frame and remains an interaction-latency limitation: passing
audio deadlines does not establish smooth rendering or controller feel during
continuous editing. Timing and memory measurements below use the live-edit
builds. Longer interactive editing and listening remain unverified.

## Audio playback verification

The playback implementation now has an SDK-independent sequencer/voice test,
an independent ADPCM decoder test, and a standalone PCSX-Redux SPU/timer fixture.
The host suite uses ASan/UBSan; both console configurations build without warnings.
Logs are generated under `build/validation/live-edit-host-tests.log` and
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
engagement switches, signed limits, and the playback status display.

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
| C4 service cost | 0.151 ms | 0.151 ms |
| C4 service interval | 0.297 ms | 0.300 ms |
| C4 key-on lateness | 0.203 ms | 0.187 ms |
| 24-note chord service cost | 0.608 ms | 0.608 ms |
| 24-note chord service interval | 0.614 ms | 0.613 ms |
| 24-note chord key-on lateness | 0.665 ms | 0.749 ms |
| Maximum-density service cost | 1.129 ms | 1.129 ms |
| Live ordinary service cost | 0.153 ms | 0.151 ms |
| Live ordinary service interval | 0.315 ms | 0.303 ms |
| Live ordinary key-on lateness | 0.241 ms | 0.220 ms |
| Live dense service cost | 1.129 ms | 1.129 ms |
| Live dense service interval | 1.137 ms | 1.134 ms |
| Ordinary revision copy | 19.168 ms | 19.169 ms |
| Ordinary revision copy through adoption | 19.426 ms | 19.283 ms |
| Dense revision copy | 27.440 ms | 26.977 ms |
| Dense revision copy through adoption | 27.651 ms | 28.216 ms |

The normal fixtures remain below the 1 ms dispatch and service-interval targets
while the GPU frame loop scrolls and asynchronous pad polling is enabled. Normalized sixteen-step loop duration from
the observed C4 key-on span is 1.999991 s / 1.999996 s (Debug / Release).
This short emulator trace supplements the longer host clock tests; it does not
establish long-session wall-clock or analog output timing.

| Requested time | Debug Attack / Release | Release Attack / Release |
| --- | ---: | ---: |
| 0 ms | 0.043 / 0.030 ms | 0.075 / 0.060 ms |
| 1 ms | 1.134 / 1.125 ms | 1.115 / 1.119 ms |
| 5 ms | 5.007 / 5.000 ms | 4.980 / 4.987 ms |
| 100 ms | 99.989 / 99.978 ms | 100.081 / 100.081 ms |

All 48 left/right voice volume registers are zero after ordinary and overloaded
stops. ADSR level readback can lag the emulator's audio thread, so the stop
assertion uses the output volume registers rather than assuming that the held
hardware envelope itself has already read back as zero.

The linker reports 887,268 bytes of BSS in each editor build. Total text/data/BSS
is 951,858 bytes in Debug and 945,226 in Release. The Debug image ends at
`0x800f864c`, leaving about 1.03 MiB above it before the top of console RAM.
This includes four 199,168-byte scores (editor, model scratch, and two playback
buffers), the editor clipboard, held-lock/voice state, the fixed input history,
two 32 KiB render packet buffers, and the SDK's
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
All four configurations were rerun successfully against the live-edit builds.
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
