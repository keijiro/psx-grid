# Validation Record

## Score storage (2026-09-23)

### Scope and result

Verification ran on Apple Silicon, macOS 27.0 (26A428), Apple Clang 21.0.0,
PSn00bSDK v0.24, MIPS GCC 16.2.0, and PCSX-Redux build 250 with its bundled
OpenBIOS. Compiler/SDK/emulator versions follow `toolchain.lock`; the host OS
has advanced since that lock was recorded. Environment output and BIOS hash
are retained in `build/validation/storage-environment.log`.
Automated host checks and direct-SIO emulator persistence checks pass. **The
platform acceptance gate is incomplete:** the default BIOS prototype fails
present-card discovery, and physical-card/controller/listening checks have no
new evidence. No shipping backend was selected by emulator success alone.

| Plan phase | Result |
| --- | --- |
| 1. Exclusive card access | Direct SIO passes emulator persistence/error/resume checks; BIOS fails discovery; hardware pending |
| 2. Portable format | Host sanitizer/golden/compatibility/invalid-input/boundary checks pass |
| 3. Capacity and readout | Host transactions and render bounds pass; host captures inspected |
| 4. Numbered storage | Host interruption/recovery and isolated emulator generations pass; physical power-loss behavior unverified |
| 5. Musical replacement | Host event traces pass; platform results and limitations below |
| 6. End-to-end acceptance | Actual menu request/coordinator and restart persistence exercised; manual controller/card-manager/listening and hardware incomplete |

### Host evidence

`./scripts/test.sh` passes with ASan/UBSan, including four new suites:
`score_format_test`, `storage_test`, `storage_editor_test`, and
`replacement_test`. The fixed v1 binary is `tests/fixtures/score-v1.bin`;
its SHA-256 is
`f253351a65240836e4e7678f87c2e72c9bba5d49d182449cce139cf38eb325b9`.
Tests cover deterministic re-encoding, semantic content, measured/emitted size,
optional extensions, required future features, CRC/truncation/invalid graph
rejection, and caller-owned staging without publication on decode failure.
There are no released older schemas requiring migration in v1.

Capacity checks reach exactly 8,192 bytes, preserve the entire score/revision
on rejection, retain same-size edits, and check atomic paste, lane growth,
end-marker moves, and Jump/owned-branch cost and reclamation. The host renderer
checks 295,284 frames with 25,028 / 32,768 bytes peak packet usage. The
[storage contact sheet](captures/host-storage.png) shows capacity, slot/card
space, long error statuses, and playback messages without clipping or overlap.
These are GPU-stub raster captures, not a console/card-manager inspection.

The in-memory card suite interrupts every one of the 64 payload writes and
64 verification reads, plus directory publication/readback and cleanup/retry.
It sweeps all 37 initial metadata reads with I/O and card-change failures,
checks ownership balancing and cleanup-retry discovery failure, and checks
restart recovery, corrupt-latest fallback, newer-format blocking,
full cards, missing/replaced cards, unrelated data preservation, and immutable
save capture. Valid payload/directory remap redirection and malformed duplicate
or reserved-range remap rejection are also covered. These simulations do not establish physical sector-write atomicity.
Replacement traces check Channel 1 master choice, mixed divisions, taken and
skipped Cycle-gated branch laps,
incoming starts at the seam, absence of outgoing seam steps, outgoing gate-token
release time, held-lock reset, empty/Stop adoption, repeated-request rejection,
and split slices.

Full evidence: `build/validation/storage-host-final.log`; focused results use
`storage-codec-*`, `storage-coordinator-*`, `storage-editor.log`, and
`storage-seams-*` in the same directory. The verification found and corrected
encoder validation before traversal, BIOS event-handle rejection, and an
unbounded BIOS filesystem initialization call in the prototype. The remaining
BIOS failure is recorded below.

### Emulator persistence and regressions

Debug and Release application/fixture builds pass for BIOS and direct-SIO
configurations. Existing audio fixtures pass in both configurations, and input
fixtures pass for digital/analog pads in Debug/Release, retaining the deliberate
4,096-tile overload tests. Their outputs are `audio-*-emulator.log` and
`input-*-*-emulator.log` under `build/validation`.

`storage_fixture.c` uses the application's actual `storage_action` and editor
menu requests. On disposable images it saves 12-note content while playing,
loads it, adopts it, restarts the executable, compares re-encoded content, and
replaces generation 1 with generation 2. The runner checks one allocated
8,192-byte file, filename generation, wrapper/title/icon bytes, and continued
pad polling. These checks do not drive every action through physical buttons
or inspect the console's card manager.

| Direct-SIO configuration | Service peak | Interval peak | Dispatch peak |
| --- | ---: | ---: | ---: |
| Debug | 2,377 | 2,403 | 3,069 |
| Release | 2,377 | 2,397 | 3,068 |

Values are 4,233,600-Hz ticks; all remain below the 4,233-tick deadline.
Per-operation `io_services`/`io_ticks` are measured between backend begin/end,
excluding busy-frame rendering and warm-up. The longest measured normal
operation is about 3.64 seconds (Debug replacement Save); pad input is paused
for this interval. Logs: `storage-storage-direct-{debug,release}-normal-*.log`.

Additional Debug scenarios pass: absent card, unformatted card unchanged,
removal during a replacement Save with the previous generation unchanged and
recoverable after restart, and Cross/START held across I/O followed by release
and a fresh accepted press. Error waits finish and polling resumes with audio
still playing. Logs use the `missing`, `unformatted`, `remove`, and `held`
scenario names; the removal run includes seed and recovery logs.

The BIOS prototype initially rejected valid high-bit event handles. After that
fix and removing `_bu_init`, it still reports `STORAGE_NO_CARD` with a valid
inserted private image. Its input-resume/audio check succeeds, but no BIOS
write lifecycle is verified (`storage-debug-normal-0.log`). The default remains
provisional; use the direct-SIO commands in [development.md](development.md)
for the passing emulator persistence configuration.

### Platform replacement and memory

`replacement_fixture.c` and its runner pass in Debug/Release. They hold an
ordinary publication pending, supersede it with Load, reject a repeated Load,
and adopt ready replacements through both Stop and disconnect. A probe in SPU
reverb RAM remains unchanged for same-Size adoption and is cleared for a Size
change; the wet volume is restored to 16,383. This validates memory/register
behavior, not audible continuity or the exact delay from note seam to effect
acknowledgement.

| Loaded replacement configuration | Service peak | Interval peak | Dispatch peak |
| --- | ---: | ---: | ---: |
| Debug | 4,151 | 4,176 | 4,217 |
| Release | 4,135 | 4,155 | 4,216 |

All measured loaded transitions remain within 4,233 ticks, with little margin.
The pending-publication ownership probe includes an artificial timer pause;
its long interval is excluded from timing acceptance. The rows above report
the separate loaded adoption/reverb cases. Evidence:
`storage-replacement-replacement-{debug,release}-emulator.log`.

The application maps include live editor/staging data, both playback snapshots,
encoded blocks, and prepared sequencers. BIOS Debug `_end` is `0x8013fa7c` and
Release `_end` is `0x8013d1fc`; BSS is 1,114,648 bytes in both. Direct-SIO Debug
and Release `_end` are `0x8013f9dc` and `0x8013cf6c`. These leave roughly 770–780
KiB below the top of 2 MiB RAM, shared by heap/stack and runtime needs.
Compiler stack reports show `score_validate_import` at 4,184/4,160 bytes
(Debug/Release) per call, `score_copy` at 2,352 bytes, and the audio service
frame at 112 bytes. Per-function stack sizes are not peak call-chain or IRQ
watermarks. Detailed maps and `.su` reports are retained in
`storage-replacement-memory-stack.txt`.

The isolated storage fixture also measures runtime watermarks with incoming
state and I/O buffers live. Across normal first-save and restart/replacement
runs, Debug reaches 4,944 bytes on the main stack and 320 / 4,096 bytes on the
SDK ISR stack; Release reaches 4,824 and 328 / 4,096 bytes respectively. The
fixture has 767,196 / 778,508 bytes between its recorded main SP and `_end`.
The main watermark covers a 64 KiB window with a 2 KiB active-frame exclusion;
shallow paths therefore report a 2 KiB floor. The runner rejects exhaustion of
that window. Painting occurs before playback, and final stack scanning is
outside the measured I/O intervals. These are observed fixture paths, not
hardware or all-path worst-case proofs. `STACK` rows are retained in the
normal, missing, unformatted, removal/recovery, and held-input logs.

### Remaining acceptance work

Physical card read/write/removal, pad handoff and measured interrupt/dispatch
bounds on hardware, actual controller interaction and card-manager title/icon
inspection, and audible reverb continuity still require manual equipment-based
validation. Exhaustive IRQ interleavings and physical power-loss guarantees are
not supplied by the host or emulator tests. Full card/pad replacement across
all metadata and transfer boundaries must not be inferred from the bounded
injection cases. The final backend-selection and end-to-end acceptance gates
therefore remain open.

## Eight shared channels (2026-09-23)

### Host model, playback and UI

`./scripts/test.sh` passes with ASan and UBSan. Channel acceptance cases cover
all eight defaults and assignment endpoints, invalid edits without mutation,
shared and unused sound retention, nested branch inheritance, moving a Jump
subtree, selector bounds/cancel, shared Sound and Reverb targets, and START /
SELECT candidate handling. Sequencer checks exercise isolated sounds/locks,
shared held locks across divisions, held-lock reassignment without runner or
gate changes, nested branches, publication during a split slice, and 16 lanes
using all eight channels. The synthesis driver retains a wet note's complete
captured settings while future notes adopt a different dry channel.

The renderer checks **295,187 frames**, with **24,848 / 32,768 bytes** peak
packet usage and no overflow or screen escape. The channel-8 head menu,
selector, Sound/submenu/property headings, Reverb editor, and inherited branch
status were visually inspected in a [host raster contact sheet](captures/host-channels.png).
These captures replay GPU packets; they do not establish actual GPU behavior.
The full host log is `build/tests/channel-host.log`.

### SPU and publication

Both Debug and Release build with `AUDIO_FIXTURE=ON`. Expanded PCSX-Redux
checks inspect the actual hardware pair-send masks for contrasting sounds on
wet/dry channels, publication while notes are held, release retention,
retirement, dry-to-wet and wet-to-dry slot reuse, stop, disconnect, and restart.
A new dry note leaves nonzero Hall feedback memory and the wet return enabled;
this establishes register/memory behavior, not the audible tail's quality.
Channel assignment followed by a destination sound edit also passes the
pending-publication coalescing case. Dense traversal uses all eight channels.

Digital and analog input fixtures pass in both Debug and Release, including
START/SELECT, delayed consumption, reconnection, and overloaded playback.
Logs are `build/validation/channel-final-{debug,release}.log` and
`build/validation/channel-input-{debug,release}-{digital,analog}.log`;
the runners also retain their full raw logs under the usual audio/input names.

### Capacity and timing

The capacity fixture runs 16 regular lanes across eight channels with 12
simultaneous notes, then checks the next lap after those notes retire. Both
builds dispatch 12 notes per lap with zero steals, skips, or overloads and
read back the expected alternating wet/dry pair mask `0xCCCCCC`. The separate
4,096-tile case deliberately overloads the shared 12-note pool; it rejects
late starts, remains bounded, accepts revisions, and clears sends/volumes on
stop. Capacity limits do not imply that arbitrary dense scores meet 1 ms.

| Measurement | Debug | Release |
| --- | ---: | ---: |
| C4 service cost | 0.167 ms | 0.167 ms |
| C4 service interval | 0.299 ms | 0.298 ms |
| Swept 12-note chord service cost, maximum | 0.599 ms | 0.599 ms |
| Swept 12-note chord dispatch, maximum | 0.672 ms | 0.737 ms |
| 16-lane / eight-channel service cost | 0.723 ms | 0.723 ms |
| 16-lane / eight-channel service interval | 0.729 ms | 0.728 ms |
| 16-lane / eight-channel dispatch | 0.744 ms | 0.803 ms |
| Dense service cost, including live edits | 1.785 ms | 1.419 ms |
| Dense service interval, including live edits | 1.791 ms | 1.425 ms |
| Ordinary revision copy, maximum | 19.042 ms | 19.009 ms |
| Dense revision copy, maximum | 25.453 ms | 25.385 ms |

An earlier capacity run exposed phase-sensitive first-chord rejection in
Debug. After the deadline-path fixes, capacity service cost decreased from
3,632 to 3,060 ticks in Debug and from 3,415 to 3,060 in Release. The table
records the final implementation; sampled ordinary dispatches meet the
4,233-tick (1 ms) limit. Dense work remains below the 65,536-tick clock-wrap
bound. Ordinary and dense publications are adopted 25/25 and 32/32 times.

### Memory

MIPS target-compiler sizes, compared with the pre-channel `HEAD` headers:

| Allocation | Before | After | Increase |
| --- | ---: | ---: | ---: |
| Score | 199,208 | 199,524 | 316 bytes |
| Sequencer | 7,288 | 7,536 | 248 bytes |
| Editor, including its Score | 201,696 | 202,016 | 320 bytes |

The editor score, model scratch score, and two publication snapshots total
four Score copies. Their increase plus the Sequencer is **1,512 bytes**;
including the editor's channel-target field makes **1,516 bytes**. Target
measurements are in `build/validation/channel-ram/{before,after}.txt`.
Host pointer/alignment sizes differ and are not the console RAM estimate.

### Remaining manual checks

The [channel walkthrough](usage.md#channel-walkthrough) remains unverified by
listening or an interactive controller pass. A fresh computer-use attempt
could bind and capture the idle Redux application, so the historical
`Invalid app` error did not recur. However, menu interactions failed with
`windowNotFoundAtPosition`; rebinding and launching the editor directly also
produced `procNotFound`, without a controllable editor window. The launch log
is `build/validation/channel-ui.log`. Host raster inspection and headless SIO
injection do not establish actual display interaction or controller feel.

Contrasting sounds, shared editing, branch inheritance, and mixed dry/wet
audio have automated coverage above; subjective listening and physical-console
SPU/display/controller checks remain unperformed. Earlier global-bypass
results below describe the previous implementation and do not apply to the
new per-note channel sends.

## Main menu, tempo and reverb (2026-09-23)

Host ASan/UBSan checks pass, including menu apply/discard, Select during a
move, settings bounds, all integer tempos from 30 to 300 BPM, and a live tempo
change preserving the current gate. The renderer checks 286,984 frames with
no screen escape or packet overflow (24,788 / 32,768 bytes peak). The Jacquard
wordmark and main menu were also inspected in a host raster capture.

Debug and Release audio fixtures verify BPM 60/240 timing, all three reverb
networks writing nonzero SPU work memory, and live bypass/amount changes.
Changing Size takes about 19 ms on the main thread while audio interrupt
intervals remain below 0.30 ms. Digital/Debug and analog/Release input fixtures
verify Select through SIO, including delayed consumption and reconnection.
Logs are `build/tests/menu-test.log`,
`build/validation/audio-{debug,release}-emulator.log`, and
`build/validation/input-{debug-digital,release-analog}-emulator.log`.
Final listening and physical-console reverb checks remain unperformed.

## Transient regression (2026-09-23)

After the onset synchronization change, `scripts/test.sh` passes with ASan and
UBSan. New host cases cover delayed playback, independent pairs, stealing,
stale gate-offs, zero release, and stopping while playback is still pending.
Both Debug and Release builds and `scripts/test-audio-emulator.py` pass.
Each emulator run checks 16 starts with Mix Attack 0 ms and Mix Release 1 ms:
all retain the initial Wave B gain and complete the release, with zero register
trajectory errors. The fixture observes pending SPU key-ons in both builds.
Sweep comparisons now use elapsed time from the acknowledged playback onset
and verify matching pitch within each pair rather than across independent pairs.
Logs are `build/validation/audio-{debug,release}-emulator.log`.
These automated checks inspect registers and control timing, not final output.
The user also confirmed the fix on physical hardware on 2026-09-23.

## Initial wavetable verification (2026-09-23)

The paired wavetable path was checked separately from the earlier sine player.
The baseline host log is `build/validation/wavetable-host-tests.log`; asset and
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
