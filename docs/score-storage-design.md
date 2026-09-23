# Score storage design

Status: BIOS file migration implemented; host, build, and OpenBIOS emulator
checks completed on 2026-09-23, including Save/Load and persistence across
process restarts. The prior direct-SIO results remain historical evidence;
they do not validate this implementation. A simulated interruption after a
completed write exposes a repeatable next-Save cleanup failure; the
unformatted-card and hardware gates also remain open.
Evidence belongs in [validation.md](validation.md). This design follows the
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

Physical Save, Load, and explicit Slot refresh are modal operations. The busy
frame is submitted before pad polling is suspended. Transport and SPU output
stop completely, including Timer 0 service, before the SDK interrupt dispatcher
is detached. BIOS services own card I/O until the session ends. Input restarts
with a fresh all-buttons-up guard; transport remains stopped after success or
failure. The user presses START to resume. Slot selection alone performs no I/O.
Invalid requests and a score over the one-block limit are rejected before the
physical handoff.

A successful physical Load decodes into separate staging, restores the SDK, then
requests and acknowledges the existing stopped-state replacement. It updates the
editor immediately and reports LOADED. Failed Load leaves the editor score
untouched. No physical operation promises a continuous playhead, sustained voice,
or reverb tail.

The musical replacement request remains independent of the card backend. A future
in-memory source can provide a validated `Score` to
`audio_platform_replace()` while transport is playing. The main thread prepares
the spare snapshot; the sequencer adopts it at the actual outgoing master's lap
seam, including branches and split slices. The first regular Channel 1 lane in
Y/X order is master, falling back to the first regular lane. Incoming regular
lanes share that seam, and outgoing steps at or after it do not execute. Stop,
disconnect, and an outgoing score without regular lanes admit the replacement
immediately. The source must retain staging until acknowledgement; ordinary edit
publication and further physical sessions remain locked until then.

The replacement machinery retains pending lane starts, gate and voice ownership,
and effect adoption. These rules apply to the independent in-memory path; physical
Load uses its stopped-state case after BIOS ownership has ended.

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

The production backend uses the BIOS card filesystem in port 1. It initializes
card services for each modal session, enumerates files, opens or creates exact
one-block files, transfers 8192 bytes, closes handles, and erases only recognized
obsolete generations. BIOS owns allocation, directory publication and bad-sector
remapping. The app does not format cards. The custom pad driver operates only
outside the physical session; there is no direct-sector fallback.

Filenames remain `BIJACQUARD` plus two decimal slot digits and eight uppercase
hexadecimal generation digits. The CRC-covered envelope must agree with that
identity. Discovery tracks the largest recognized filename generation, even if
its data is incomplete, and chooses the highest valid supported payload. An
unsupported newer musical schema at or above that payload blocks fallback and
Save. Equal-generation ties follow BIOS enumeration order, which still needs
comparison with old directory-order behavior in verification.

Save encodes an immutable one-block image before card access, preserves the
latest valid generation, and creates a new generation in a spare block. After
write and close, it reopens the complete file, checks every byte and decodes
its CRC and identity. Only then does it retire recognized obsolete one-block
files for the same slot. A failed cleanup reports SAVED / CLEANUP PENDING; a
later Save retries safe retirement before allocating. A full card does not
sacrifice the last valid generation. Unrelated files are never deleted.

The BIOS can publish allocation before the payload is complete. A partial new
file can therefore remain visible after interruption, but it is invalid until
its complete payload passes readback and decode. This is a different metadata
failure window from the former raw directory-entry commit protocol. BIOS file
creation and deletion do not establish power-loss atomicity for other files.
Enumeration supplies free-block accounting from file sizes. BIOS errors do not
reliably distinguish damaged metadata from generic I/O failure, so the UI may
report CARD I/O ERROR where the old raw scanner reported CARD DAMAGED. The BIOS
filesystem also cannot promise the old raw scanner's recovery from individually
damaged directory entries. These limits need explicit card-image and hardware
verification.

The asynchronous card and file event waits use the free-running Timer 2 counter
while Timer 0 audio service is stopped. The platform discards elapsed Timer 2
time on restoration. BIOS initialization, enumeration, open, close, and erase
still contain synchronous calls. Their behavior on absent or removed cards must
be established in a supported BIOS environment; an external fixture timeout
is only a test bound, not application recovery from a stuck BIOS syscall.
The bundled OpenBIOS supports the tested normal and removal paths, but its
unimplemented `_card_clear()` call blocks the tested unformatted-card
scenario.

## Implementation gates

Normal BIOS file Save/Load and process-restart persistence have passed with
private OpenBIOS card images; emulated removal and recovery also pass. Use a
BIOS implementing `_card_clear()` for the unformatted-card scenario, then
check real cards and controllers.
Record host, platform, restart, removal, listening, and power-interruption
outcomes in [validation.md](validation.md) without revising historical
direct-SIO results. Builds and the adapted fixtures alone do not close these
gates.
