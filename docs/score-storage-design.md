# Score storage design

Status: implemented; host verification and direct-SIO emulator persistence
checks passed on 2026-09-23. The BIOS prototype fails card discovery in the
pinned emulator. Backend selection remains provisional; hardware and listening
gates are incomplete. Evidence and remaining cases are in [validation.md](validation.md). This design follows the
local Jacquard reference at `~/Projects/jacquard/main` as inspected on
2026-09-23. Its relevant sources are `ProjectStore.cs`, `JacquardApp.cs`,
`Core/Model/Score.cs`, and `Core/Sequencer/Sequencer.cs`.

## Interaction and storage identity

Put a numbered chooser, Save, and Load in the main menu alongside BPM and
Reverb. Selecting a number changes the target only. Save captures the current
committed editor score; Load explicitly requests replacement. The chooser's
target and the score currently playing are separate identities.

Use application slot numbers, 01–15, rather than physical card
block numbers. Initially use the card in port 1; adding a card selector later
does not need a score-format change. Show EMPTY, SAVED, BUSY, CORRUPT, or NEWER
VERSION for the selected target and report missing cards and insufficient space.
An empty slot cannot be loaded. Unformatted cards must not be formatted as a
side effect of Save. The main menu has six selectable rows: BPM, Reverb, Slot, Save, Load, and
Close. Left/Right on Slot changes the target without I/O; Cross refreshes its
status. Unqueried slots display CHECK. Card free blocks appear separately.
Statuses are cached for presentation only; Save and Load rediscover the card.

Remove user-supplied names, not filesystem identity. A stable application
prefix plus slot number identifies each file. A generated BIOS title such as
`JACQUARD 01` and an icon let the console's card manager identify it. Slot
numbers must not be compacted when other files are deleted.

A score is limited to one 8,192-byte card block, including its title/icon
wrapper and application headers. Every saved score occupies exactly one block.
A card has 15 usable blocks, shared with other applications and any temporary
save generation. Logical slots 01–15 therefore do not guarantee that all 15
can be saved on a card already containing other files. Report card capacity
separately from the current score's remaining byte budget. Card geometry comes from
the [PSX card format](https://psx-spx.consoledev.net/controllersandmemorycards/#memory-card-data-format).

## Load timing and ownership

Jacquard reads and parses before requesting a switch. While playing, it waits
for the outgoing master runner to complete a lap and uses that runner's next
deadline as the seam. Branches and gates make this different from calculating
`length * step duration` at press time. Every incoming regular lane starts at
that seam; outgoing lanes must not execute any step at or after it.

There is a reference mismatch to resolve deliberately: Jacquard chooses the
first regular Channel 1 lane in Y/X order, falling back to the first regular
lane. This project's sequencer currently treats the first Y/X runner as master.
Adopt Jacquard's rule for both score switching and pending lane starts while
preserving Y/X execution order for locks. Channel 1 is index zero here.
Pending lane starts must observe this selected runner's wrap, not the current
`!runner_index` test, since the master may run after an upper accent lane.

Replacement lifecycle:

1. Capture the selected slot and begin reading. Lock score-changing controls
   during reading and while waiting, so edits cannot race replacement.
2. Decode into separate storage, validate the entire score, and prepare the
   incoming playback state on the main thread. On failure, keep the current
   score and playback untouched and unlock the editor.
3. Once ready, arm one pending replacement. Stop or an outgoing score without
   regular lanes admits it immediately; otherwise wait for the next observed
   master lap. A slow read simply misses an earlier lap.
4. At the seam, swap prepared state before evaluating any incoming events.
   Acknowledge adoption to the main thread, which updates the editor and
   releases the lock. Keep replacement ownership through this acknowledgement
   so the normal revision publisher cannot republish the outgoing editor score.

Finish the split slice that discovers the lap before takeover. Once its seam
is known, process outgoing deadlines strictly below it; if `now` reaches or
passes it, replacement preempts ordinary slice selection. Merely swapping on
a later timer callback would let the current `sequencer_service` execute old
events at the seam before the incoming ones.

Reject repeated Load while busy; changing the chooser must not retarget an
existing request. All pad controls, including START, are unavailable during
card access. Once access ends, resume pad input even if the incoming score is
still waiting for its musical seam. START can then stop playback and admit a
ready score immediately, as in Jacquard. Actual pad disconnection, detected
after polling resumes, uses the same stopped-state rule. For an initial
implementation, disable Save while Load is busy to avoid saving an ambiguous
outgoing score. Score-changing controls remain locked until adoption.
The current audio stop/disconnect path clears `pending_snapshot`; a ready
replacement must have separate ownership and be adopted on that path, not
discarded with an ordinary pending edit publication.

This needs a distinct replacement operation, not `sequencer_resync`, which
preserves the identities and positions of edited lanes. Regenerate runtime
revision/birth bookkeeping on import and reset incoming runners, laps, and
held locks. Preserve the outgoing voices and their scheduled gate-offs across
the seam; the existing stop/start path would discard them. New notes naturally
compete for the existing voice pool. Prepare runner state outside interrupts,
and retain the bounded slice/tile budgets during takeover.
Gate-offs live in `Sequencer.offs`, so resetting the whole sequencer is also
incorrect: retain or transfer that array and the sink/audio allocator state
while resetting runners, PRNG, and slice traversal state.

The incoming global effect is applied by the first main-thread adoption
acknowledgement after the note seam. Until that acknowledgement, the outgoing
network and wet return remain active. Identical Size retains the delay memory;
a Size change wet-mutes, disables the network, clears its RAM through DMA, and
restores the incoming network/Amount while incoming notes continue. No reverb
DMA runs in an interrupt. The post-seam delay includes the remaining main-loop
work; its duration and audible continuity have not been measured. This is the
provisional transition policy for the separate listening/platform session.

## Portable binary payload

Use a PSX title/icon wrapper followed by a platform-independent binary payload.
Do not dump `Score`: it includes unused pool entries, native `int`/enum layout,
links and editor/publication generations that are not musical content.

The v1 envelope starts at block offset 512. All multibyte integers are
little-endian; signed values use two's complement. CRC-32/ISO-HDLC uses reflected
polynomial `0xedb88320`, initial value `0xffffffff`, and final XOR `0xffffffff`.
It covers the envelope and declared payload with bytes 20–23 treated as zero;
it excludes the PSX wrapper and block padding.

| Envelope offset | Width | Meaning |
| --- | --- | --- |
| 0 | 4 | ASCII `JQSC` |
| 4 | 2 | Envelope version, 1 |
| 6 | 2 | Minimum reader version, 1 |
| 8 | 2 | Envelope length, 32 |
| 10 | 2 | Reserved, zero |
| 12 | 4 | Payload length, excluding envelope |
| 16 | 4 | Required feature bits; none defined in v1, zero |
| 20 | 4 | CRC-32 |
| 24 | 4 | Save generation, 1–`0xffffffff`, no wrap |
| 28 | 1 | Logical slot, 1–15 |
| 29 | 3 | Reserved, zero |

The 512-byte wrapper starts with `SC`, icon flag `0x11`, and block count 1.
The title at offset 4 is full-width Shift-JIS `JACQUARD NN`, zero-padded to
64 bytes. The 16-color palette starts at 96 and the single 16x16, 4-bit icon
at 128; unused wrapper bytes are zero. Every physical file is padded to 8192
bytes, independently of its measured content size.

Each 12-byte chunk header contains tag u16, schema u16, flags u32, and payload
length u32. Flag bit 0 means required; other bits are unsupported. Canonical
output has Global (tag 1), Sounds (2), Lanes (3), and Steps (4), each exactly
once with schema 1 and required set. Readers accept these chunks in any order;
duplicate/missing known chunks fail. Unknown optional chunks are disposable
metadata and are skipped; unknown required chunks or schemas report NEWER
VERSION. There are no earlier released schemas to migrate in v1.

Persistent chunk content:

| Chunk | Persistent content |
| --- | --- |
| Global | BPM and reverb Size/Amount |
| Sounds | All eight channels, including currently unused ones |
| Lanes | Active lanes, coordinates, length, division, channel/branch role |
| Steps | Ordered tile stacks for every step in each active lane |

Global uses BPM u16, Size u8, Amount u8, and
four reserved bytes. Each 16-byte sound uses attack/release u16, two waveform
u8 tags, mix attack/release u16, sweep i8, decay u16, reverb u8, and two reserved
bytes. Each 12-byte lane uses x, y, length, division, channel, and role as u8
fields, followed by six reserved bytes; branches use a channel sentinel and
inherit division. Reserved bytes are zero in v1. Active lanes appear in Y/X
order; their record positions define file-local indices. Steps follows that
same lane order and ascending step order, with one count and its tile records
per step. Lane lengths determine the number of counts, avoiding runtime IDs
or a separate offset table. These are the v1 layouts. Waveform tags are explicitly 0=Sine, 1=Triangle,
2=Saw, 3=Square, and 4=Noise. Branch records use role 1, division 0, and channel
255; regular records use role 0 and a channel in 0–7. The decoder reconstructs
branch inheritance rather than treating its zero division as a playback rate.

Assign compact file-local lane indices. A Jump stores its destination lane
index; reconstruct the destination's source link on import. Derive tile pool
links from stack order rather than serializing runtime IDs. Keep empty steps
and lanes, which affect duration and layout. Persist each tile kind's actual
properties, including every Cycle pattern bit and enabled zero-offset locks.
Do not store cursor, clipboard, selection, playheads, PRNG state, or generations.
Use stable wire tags independent of C enum ordinals. Store divisions as their
numeric values, not indices into the current division table.

Each tile has a one-byte wire kind (Note=1, Cycle=2, Probability=3, Jump=4,
Relative Lock=5) and one-byte payload length, with these payloads:

| Kind | Payload | Total bytes |
| --- | --- | ---: |
| Note | pitch u8, duration u16 in twentieths of a step | 5 |
| Cycle | period u8, pattern u32 | 7 |
| Probability | chance u8 | 3 |
| Jump | destination lane u8 | 3 |
| Relative lock | mask u8, attack i16, release i16 in milliseconds | 7 |

One u8 stack count per step accommodates the 64-cell height limit. The fixed
v1 cost is 512 bytes reserved for the PSX wrapper, 32 for the application
header, four 12-byte chunk headers, 8 global bytes, and eight 16-byte sounds:
728 bytes. Every active lane adds 12 bytes plus one count byte per step, and
its tiles add the sizes above. The full model capacity would exceed a block;
the one-block rule therefore adds an editing constraint alongside geometry,
lane count, and tile pool capacity. Keep the encoding uncompressed so its cost
is exact and predictable, including near the limit.

## Remaining capacity and edit admission

Display the current score's remaining serialized bytes continuously in the
plane header, for example `FREE 2048 B`, and repeat the value beside Save in
the main menu. The existing top line has room to place a compact readout on
the right after the title/coordinates; verify exact spacing at render time.
Keep action/error messages and transport status available in their existing
bottom rows. This readout works without a card inserted and describes room
for score content, not unused card blocks or bytes of runtime RAM.

With the v1 encoding, the exact calculation is:

```
used = 728 + 12 * active_lanes + sum(active_lane_lengths)
     + 5 * notes + 7 * cycles + 3 * probabilities + 3 * jumps
     + 7 * relative_locks
free = 8192 - used
```

An empty score has 7,464 bytes free. A new empty 16-step lane consumes 28 bytes.
A Note on an existing step consumes 5 bytes; a Cycle or Relative Lock consumes
7. A Jump also creates its branch: the current four-step branch costs another
16 bytes, making 19 bytes in total. Insertion at an end marker adds another
step-count byte. Extending an empty lane costs one byte per added step.
Pitch, duration, sound, tempo, and lock-value edits do not change encoded size.
Moves normally have no size cost, but moving a stack to an end marker can grow
the target lane. Deletions return their complete serialized cost, including
owned branches. Geometry and existing model limits still apply independently.

Admit each proposed edit only if the resulting complete score fits. Reject
an over-budget transaction with `SCORE FULL`, leaving both the score and the
readout unchanged. Apply this to creation, placement, resizing, moves, and
whole-stack paste; do not leave a partial paste or orphan branch. At zero free
bytes, same-size edits and deletions remain available. The aim is that every
committed score is saveable within one block, rather than discovering an
oversize score only when Save is pressed.

The codec exposes a measurement operation sharing the writer's field
emission and sizing rules, rather than maintaining a second UI-only estimator.
Measure candidate edits before commit, and cache the current count on revision
changes. Do not serialize every video frame, access the card, or calculate
capacity in the audio interrupt. Save independently checks the actual encoded
length as a final invariant; Load validates both the file and the reconstructed
score's encodability under the current one-block policy. Padding the physical
file to 8,192 bytes does not consume the displayed content budget.

These byte counts and layouts define v1; the golden-fixture and compatibility
checks are part of the host verification suite. Future
versions should use reserved space or omit default-valued extension chunks
where appropriate, so new settings do not automatically make a formerly full
score unsaveable. Any format migration must explicitly test full old scores;
older-file readability alone does not guarantee that re-encoding fits. Never
silently discard musical content or grow a save beyond one block.

Compatibility policy:

- New readers accept older supported schemas and supply version-specific
  defaults for newly introduced settings. Preserve old musical meaning when
  defaults or units change; migrate explicitly where necessary.
- Old readers may skip explicitly optional metadata chunks. Unknown required
  chunks, tile kinds, sound algorithms, or playback-affecting fields must fail
  as NEWER VERSION. Silently dropping them would play a different score.
- Length fields enable extension but do not alone promise compatibility.
  Require a newer reader whenever a change affects musical interpretation.
- Do not promise arbitrary future-file round trips. Skipped metadata is either
  preserved as opaque data or declared disposable; it must not conceal a
  required musical extension. Jacquard text-file interchange is a separate
  conversion feature, since the synthesis models already differ.

The decoder must bound lengths/counts before allocating or following links,
then check values, geometry, unique tile ownership, Jump/source consistency,
and an acyclic branch graph. The current model's internal `validate` assumes
trusted pool links and is not a safe parser validation entry point. Reject a
bad file atomically instead of clamping or partially loading it.
Every Jump must target an active branch, every branch must have exactly one
owning Jump, regular lanes must have no source, and branches must have no
independent channel assignment.

## Card I/O and save integrity

The current pad driver owns SIO0 and resets it on every VBlank. A memory-card
driver cannot independently use that peripheral. The agreed interaction allows
all pad input to pause during card access. Give the card exclusive ownership
for the whole operation, including directory access and save readback, rather
than interleaving pad packets. Audio playback must continue. Show `READING` or
`SAVING` before handing over the bus; START and cancellation by pad are also
unavailable while it belongs to the card. Avoid continuous background card
scans that would repeatedly suspend input; refresh on explicit storage actions.

The handoff must cover every pad entry point, not just editor input:

1. Stop accepting pad actions after the initiating Save/Load press. Prevent new
   VBlank polls, finish the current packet with a bounded timeout, then drain
   pending receive/ACK state and deselect the device before changing owners.
2. Gate the pad's timer service and replace or dispatch its SIO0 IRQ handler so
   neither can access card registers. Discard queued input reports and reset
   input repeat/gesture state. Do not publish a disconnected sample to express
   this intentional pause: `audio_platform_update` would stop playback.
3. Run card work while audio timer interrupts remain live. Keep directory
   scans, parsing, score copies, and CRC work out of interrupt callbacks.
   Every transfer/wait needs a timeout; a missing or removed card must not
   leave input disabled indefinitely.
4. On both success and error, quiesce card callbacks/transfers, clear residual
   SIO state, restore pad ownership/configuration, and resume fresh polling.
   Reset the input interpreter and require all buttons to be released before
   accepting another action. The existing reconnection guard provides this
   behavior without sending a synthetic disconnection to the audio layer.

Input pressed during the pause is discarded, not replayed after access ends.
Keep the last observed connection state for audio during the pause; physical
pad removal cannot be detected until polling resumes. A Load waiting for the
master lap no longer needs the card bus and must not prolong this input pause.

The pinned SDK's `examples/io/pads/spi.h` explicitly excludes coexistence
between its custom bus driver and BIOS pad/card APIs. Its example also uses
Timer 2, already owned by this project's audio clock, so it cannot be copied
unchanged. A custom card backend also needs directory/allocation handling;
BIOS filesystem calls are not an independent layer that can simply be added
above an active custom driver. Exclusive ownership makes a temporary handoff
to BIOS card services an option worth testing first, potentially reusing their
filesystem handling. It does not establish that disabling polls alone makes
those services safe: callback/IRQ ownership, SDK/BIOS interrupt dispatch, and
audio timing must all be verified. If that handoff fails validation, a custom
card backend can retain the same exclusive-access UI without needing pad/card
packet interleaving. Transfer card sectors in 128-byte units. Preserve the
audio service cadence and dispatch deadline, not merely its clock's 15.48 ms
wrap limit; stopping pad polling does not authorize long interrupt masking.

The memory budget includes the editor score, model scratch, two playback
snapshots, decoded incoming score, two encoded blocks, directory/remap state,
and a second prepared sequencer. One `Score` occupies 199,524 bytes; a raw
score would not fit on an entire card. Final linker-map and stack evidence
belongs in [validation.md](validation.md#score-storage-2026-09-23), including
the remaining hardware measurement limits.

Capture a committed score into immutable save data before writing, and hold
it through verification and cleanup. CRC detects damage but does not
make an in-place overwrite safe. Prefer writing a new generation to a second
file, reading it back and validating it, then retiring the old generation.
Use deterministic slot/generation recovery so an interruption with two files
does not depend on an assumed atomic rename. Card metadata itself still needs
failure testing; this is not a claim of hardware-level power-loss atomicity.

Allocation does not proactively reserve a spare block. A blank card may hold
15 slots, but replacing a saved slot then requires the user to free a block.
Never delete the latest valid generation to create this space or touch another
slot. The score's FREE byte count does not describe card allocation.

Filenames are exactly `BIJACQUARD` + two decimal slot digits + eight uppercase
hexadecimal generation digits (20 characters). The filename identity must match
the CRC-covered envelope identity. Save uses one more than the largest observed
recognized generation, including damaged payloads, and refuses counter wrap.
Recovery selects the highest valid generation, using the lower directory block
for duplicate generation numbers. A newer unsupported musical format at or
above the best readable generation prevents fallback and overwrite. A corrupt
latest payload may fall back to an older valid generation.

The coordinator checks directory checksums, allocated file chains, and sector
remappings before allocating. Damage to an individual directory entry prevents
all writes, but read-only recovery can still select an intact older generation
from an individually valid entry if the card header/remapping table is sound. It writes into a free block, reads and validates
the complete block, and only then publishes and reads back its one-block
directory entry. This directory entry plus a matching valid envelope is commit
evidence; there is no rename dependency. A partial write before publication
leaves a free block and the old save. After publication, obsolete recognized
files for that slot are retired. SAVED / CLEANUP PENDING means the new save was
verified but retirement failed. A subsequent Save first retries retirement
while retaining the newest validated generation, then allocates its spare.
An I/O failure during that retry does not report a new successful save.

The platform interface is sector-based so the same allocation/recovery logic
can use either handoff candidate. `STORAGE_BIOS_BACKEND=ON` currently selects
the **provisional BIOS prototype**, with bounded event waits and no blocking
BIOS filesystem calls. `OFF` selects the provisional direct-SIO implementation.
The direct path has passed emulator persistence checks, but neither has passed
the complete backend-selection gate. The direct path detects card
insertion after clearing the flag through the reserved write-test sector; the
BIOS path checks card presence before sector requests. Neither path formats
an unformatted card. Main-thread staging and directory handling remain common.

## Implementation gates

1. Validate exclusive SIO handoff with card reads/writes, held buttons, missing
   cards, and removal while dense audio runs. Check recovery after every error,
   discarded queued presses, the all-buttons-up guard, START after I/O while
   waiting for a seam, and delayed pad-disconnect detection. Measure service/
   dispatch latency and timer wrap safety in the emulator and on hardware.
2. Add the SDK-independent codec with semantic round trips, fixed golden bytes,
   older-version migrations, rejected future musical features, truncation,
   corrupt references/cycles, maximum geometry, and block-size bounds. Check
   measured versus actual byte counts, exact fits, over-budget transactions,
   Jump/branch costs, end-marker moves, atomic paste, and reclaimed capacity.
3. Test replacement seams with mixed divisions, conditional branches, long
   outgoing notes, held locks, empty scores, Stop, repeated Load, split slices,
   reverb changes, and pending normal edit publications.
4. Test write interruption/recovery at each persistence stage, card replacement,
   full directories, insufficient blocks, and coexistence with unrelated saves.
   Measure the final RAM/stack budget with incoming state and I/O buffers live.

The verification session added codec/storage/seam fixtures, failure injection,
builds, and runtime checks. Gate completion and remaining physical-platform
requirements are recorded in [validation.md](validation.md); emulator success
does not establish hardware power-loss behavior or audible effect continuity.
