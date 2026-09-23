# Using the Score Editor

See [development.md](development.md) for setup, builds, and emulator launch.
The executable starts with an empty score and the cursor at `(1,1)`.

## Controls

| State | D-pad | X / Cross | O / Circle |
| --- | --- | --- | --- |
| Plane | Move and scroll | Tap to open the cursor menu; hold with a direction to move an object | Cancel a move |
| Moving | Select a destination, including invalid ground | Release to drop | Return to source |
| Menu / picker | Up/Down selects | Execute | Return |
| Pitch | Left/Right changes semitone; Up/Down changes octave | Apply | Discard |
| Note length | Left/Right changes 0.05 steps; Up/Down changes one step | Apply | Discard |
| Wave A / B | Right/Up or Left/Down changes one waveform | Apply | Discard |
| Pitch sweep | Left/Right changes 1 semitone; Up/Down changes 12 | Apply | Discard |
| Sound time / lock offset | Left/Right changes 1 ms; Up/Down changes 100 ms | Apply | Discard |
| Lock engagement | Right/Up enables; Left/Down disables and clears the offset | Apply | Discard |
| Other numeric property | Right/Up increases; Left/Down decreases | Apply | Discard |
| Cycle pattern | Navigate eight columns and the Apply item | Toggle a lap, or apply at Apply | Discard |
| Deletion confirmation | Down/Right selects Delete; Up/Left selects Cancel | Execute selection | Return |

Menus open on X release, with no long-press timer. An X press and direction in
one frame grabs the cell at the cursor's original position. Directional input
on an empty cell suppresses the tap menu. Menu confirmation never begins a
move. Invalid drops and Circle return to the source without modifying the score.

The controller must release all buttons after reconnecting. Disconnecting
stops playback and cancels a move; other editors retain their candidates. Directions repeat after
18 frames, then every three frames. Opposite directions cancel; horizontal
movement takes precedence over vertical movement. Mode changes reset repeat.

## Tiles and lanes

Ground offers `NEW LANE`. Empty steps, terminators, and cells immediately below
stacks offer `CREATE TILE` and `PASTE STACK`. Placement on a terminator extends
the lane by one step. Occupied cells are never overwritten.

| Object | Menu properties and initial values |
| --- | --- |
| Note | Pitch C0–C9, initially C4; length 0.25–64 steps, initially 1 |
| Cycle gate | Period 2–32, initially 4; pattern with only lap 1 enabled |
| Probability gate | Chance 0–100%, initially 50% |
| Regular head (`L`) | Length, step division, channel, shared Sound settings, delete lane |
| Relative Lock | Separate Attack/Release enable switches and signed millisecond offsets; both disabled initially |
| Branch head (`B`) | Length, delete lane; division and channel inherited from its source |
| Jump | Copy stack, delete tile; destination is its own branch |

New notes remember the pitch and length of the last confirmed note placement
or edit. Shortening a cycle period preserves switches outside the active
period. Pattern changes are committed only with the explicit `APPLY` item.
Cycle tiles display `C` and their period; probability tiles display their
percentage as a number. Full editable values appear in the cursor menus.

Notes in a vertical stack describe a chord. Gates govern tiles below them;
a failed gate preserves notes and locks already reached above it.
Deleting a tile closes the gap. Moving to another step carries the selected
tile and all tiles below it; a drop on a stack inserts at that depth. Moving
within the same stack reorders just the selected tile. Dragging either kind of
head moves the entire lane; connected lanes keep their positions.

`COPY STACK` includes the selected tile and everything below it except jumps.
A selection containing only jumps leaves the previous clipboard intact.
Pastes create independent tiles, including independent property values.

A jump creates a four-step branch below the score. Its connection and any
visible off-screen endpoint markers are drawn on the plane. Deleting a jump
removes its branch and every descendant branch. Deleting a branch head also
removes its source jump. Lane and jump deletion require confirmation, initially
set to Cancel. Moves that would create a branch cycle are rejected.

The plane contains 128 by 64 cells, up to 16 lanes (including branches), and
4,096 runtime tile slots, additionally limited by the one-block serialized
score budget. Regular lanes start at 16 steps; all lanes allow 1–64 steps.
Stack depth is limited by the bottom of the plane. A head at `(x,y)` has steps
at `(x+1,y)` through `(x+length,y)` and an endpoint at `(x+length+1,y)`.
Collision, boundary, and capacity failures leave the score unchanged. Shortening
a lane cannot discard occupied trailing steps.

Division denominators are `1, 2, 3, 4, 6, 8, 12, 16, 24, 32, 48, 64`, initially
16. Auditioning, the Channel Panel (including Solo, Mute, and Swap),
absolute locks and undo are outside this milestone.

## Playback and sound

Except during exclusive card access, START toggles playback in every editor
mode without applying an unfinished
property edit or move. It never repeats while held. Playback starts at step
zero with lap zero on every regular lane; branches have no independent runner.
Stopping silences voices with a short ramp. Starting again restarts the score.

Committed score, channel assignment, and channel sound edits are published
during playback, after any time slice already being processed has finished. Notes, gates, jumps, and
new locks use the updated contents when their steps are next read; a held
lock uses its updated value on subsequent slices until its step ends. Deleted
locks stop contributing after publication. Unconfirmed property candidates
and moves are not published.

Existing regular lanes retain their next deadline and lap count. A tempo or division
change applies to the next step's duration; it does not move the deadline of
the current step. Moving a head changes runner order. If the visited branch
is deleted, its runner returns to step zero of its origin; if its lane is
shortened past the next step, it returns to step zero of that lane. Neither
repair increments the lap count. Removing an origin removes its runner.
Already sounding notes keep their scheduled gates and envelopes.

The first regular Channel 1 head in top-to-bottom, then left-to-right order
is the master, falling back to the first regular head if Channel 1 is absent. New regular lanes join at a master lap boundary. That boundary is
assigned when the master reads its final step, so a lane published while that
step is already sounding waits one additional lap. A new lane that becomes
the master starts at publication instead, so it can provide that boundary. Playback stays enabled with no regular lanes; the first lane drawn
then starts at publication. Deleting and recreating a lane creates a new
runner, even if the same pool slot is reused.

The status area shows `PLAYING` or `STOPPED`, with `APPLYING EDITS` while a
committed revision is waiting for publication. There is no Transport Row or
playhead.

Tempo defaults to 120 BPM and can be set from 30 to 300 BPM in the main menu.
A step lasts 240,000 / BPM / division milliseconds: at 120 BPM, sixteen steps
at division 16 make a two-second loop. Notes keep
their written gate lengths and overlap across steps. Release begins at gate-off,
even if Attack is still in progress. All eight channels share a pool of up to
12 notes, using a fixed pair of SPU voices per note. Extreme density may steal
voices or skip overdue notes.

A reached Jump selects the next step from its branch, after finishing the
current stack. The last reached Jump wins. Any terminator returns the runner
to its original regular lane and increments its lap, with no extra empty step.
Cycle gates select bit zero on the first lap. Probability gates use a repeatable
random sequence reset at each start.

SELECT opens the main menu with the Jacquard wordmark, `BPM`, and `REVERB`.
SELECT again or Circle closes it. Opening it discards an unfinished property
candidate and cancels a move. Playback continues while menus are open.
BPM uses Left/Right for 1 BPM and Up/Down for 10 BPM. Cross applies; Circle
returns without applying.

`REVERB` provides `SIZE` (Small, Medium, Large; initially Medium) and `AMOUNT`
(0–100%; initially 30%). Amount uses Left/Right for 1% and Up/Down for 10%.
Cross applies each setting and returns to the Reverb menu; Circle discards it.
Each channel remains dry until its `SOUND > REVERB` is enabled. This setting
is captured by future notes after publication; held notes and release tails
keep their captured send choice. Dry notes do not mute wet notes or the shared
effect tail. Global Amount controls the wet return while preserving dry sound.
Changing Size clears the previous reverb tail;
ordinary stopping lets the tail decay. Reverb settings also apply when stopped.

Open `CHANNEL` on a regular head to select channel 1–8 (initially 1).
Left/Down decrements and Right/Up increments, stopping at either endpoint.
Cross applies; Circle discards. Selection does not copy or reset sounds.
All eight configurations persist even when no lane uses them. Branches,
including nested branches, inherit their origin regular lane's channel;
moving a Jump subtree to another lane changes that inheritance. The plane
status shows the effective channel on heads, steps, tiles, and branches.

Open `SOUND` on a regular head to edit its channel's shared sound through four
submenus and a `REVERB` on/off field (initially Off). Cross applies an individual
candidate; Circle discards it and returns to its submenu. `BACK` or Circle returns from a submenu to `SOUND`.
Headings retain the channel number throughout editing. Heads assigned to the
same channel edit the same settings; changing assignment and reopening Sound
shows the destination channel's settings. Lane divisions, lengths, positions,
and lap counts remain independent.

| Submenu | Controls | Range | Initial value |
| --- | --- | --- | --- |
| Waves | Wave A, Wave B | Sine, Triangle, Saw, Square, Noise | Both Sine |
| Amplitude | Amp Attack, Amp Release | 0–16,000 ms | Both 5 ms |
| Mix | Mix Attack, Mix Release | 0–500 ms | 120 ms, 280 ms |
| Pitch | Pitch Sweep | -24–+24 semitones | 0 |
| Pitch | Pitch Decay | 0–2,000 ms | 200 ms |

Amplitude rises at note-on, sustains until gate-off, and releases from its
current level. Mix independently travels from A to B during its attack and
back to A during its release, once per note. A zero attack starts at B if
release is nonzero; a zero release returns immediately to A at the attack
boundary. Both zero select A throughout. Equal wave choices are valid and
retain the same total gain. Noise is a repeating wavetable.

Positive sweep begins above the written pitch and falls; negative sweep begins
below it and rises. The interval follows Jacquard's normalized exponential
Snap curve and reaches the written pitch exactly at Pitch Decay. Zero sweep
or zero decay disables it. The instantaneous frequency is limited to C0–C9,
so extreme notes can initially plateau at an endpoint. Mix and sweep continue
through gate-off while the amplitude tail remains audible. Their time starts
when the SPU pair begins playback; amplitude and gate timing follow the score.

All settings are captured at note-on. Published edits affect future notes;
existing notes keep their complete sound. Automated synthesis checks and
remaining listening/controller checks are recorded in [validation.md](validation.md).

Relative Locks affect only Amp Attack and Amp Release, adding signed offsets
from -16,000 to +16,000 ms. Enable a target before changing its offset. Enabled zero is distinct from disabled; disabling
clears the offset. Both targets may be enabled in one tile (`A` and `R` labels).

Runners execute by their original heads, top to bottom, then left to right.
Within each stack, locks affect notes below them and subsequent lower runners
on the same channel. Other channels are unaffected.
Each addition clamps immediately to 0–16,000 ms. The reached locks remain held
until that runner's next step; an empty step clears them. Faster lower lanes
on the same channel hear a slower upper lane's held locks. Notes already
sounding retain their original Attack and Release, and base channel settings never accumulate
lock changes. After a published channel reassignment, still-held locks apply
to the new channel on the next slice and stop contributing to the old one.
Assignment and sound edits preserve runner positions, laps, and scheduled gates.

## Saving and loading

Storage has passed host and direct-SIO emulator checks. The default BIOS
backend still fails card discovery in the pinned emulator. Use the direct-SIO
verification configuration in [development.md](development.md) for emulator
testing; physical-card and listening checks remain incomplete.

Open the main menu with SELECT. Select SLOT and use Left/Right to choose 01–15;
this only changes the target. Press Cross on SLOT to check the card in port 1.
Choose SAVE to capture the committed score, or LOAD to replace it. The slot
number remains independent of the currently playing score. EMPTY cannot load;
CORRUPT and NEWER VERSION identify unreadable content. The separate `BLK`
readout reports available card blocks after an operation.

`FREE n B` in the plane header and beside Save is the score's remaining encoded
content budget, available even without a card. An empty score has 7464 bytes
free. A transaction that exceeds the budget reports SCORE FULL and changes
nothing. Deletions reclaim bytes, while fixed-size property edits remain
available at zero free bytes.

Each slot uses one block. Replacing it needs a spare block until the new save
is verified, so a card containing 15 saved slots must have a block freed before
a replacement. The application never formats cards or deletes other slots to
make space. SAVED / CLEANUP PENDING means the new generation is saved, with an
obsolete generation still occupying a block; another Save retries cleanup.

All controller input, including START, pauses while CHECKING CARD, SAVING, or
READING. Release every button when access finishes. Playback continues during
I/O. A ready Load waits for the outgoing master lap while playing; the master
is the first regular Channel 1 lane in Y/X order, falling back to the first
regular lane. During WAITING FOR LAP, score edits and further Save/Load actions
are locked, but START can stop and admit the new score immediately. Once
adopted, all incoming regular lanes start together and outgoing note tails
retain their gate deadlines. A changed reverb Size clears its old tail after
adoption; identical Size retains its delay memory.

## Walkthrough

1. Tap X at `(1,1)` and execute `NEW LANE`.
2. At `(2,1)`, create a Cycle Gate. At `(2,2)` and `(2,3)`, create Notes.
3. Open each note's menu and edit pitch and length. Open the gate's menu,
   change its period, toggle laps in its pattern, and select Apply.
4. Copy from the gate. Move to `(3,1)` and paste the complete stack.
5. Create another lane on clear ground. Hold X on the pasted gate, navigate
   to an empty step in the new lane, and release X. The whole stack moves.
6. Create a Jump on an empty step. Follow its connection to the branch head;
   edit its length, then delete the branch with explicit confirmation.
   Verify that its source jump disappears as well.

During a move the original score stays visible. Outlines show the carried
shape at the candidate position: bright means valid, muted means invalid.
The source marker is clamped to a viewport edge when it scrolls out of view.

## Playback walkthrough

1. Create a lane and place Notes at its first few steps. Press START and edit
   a pitch. On the next visit to that step after publication, the new pitch
   should sound without restarting playback.
2. Select its head, open `SOUND > AMPLITUDE`, and try a long Amp Release. Stop during a tail.
3. Place a Relative Lock above a Note. Enable Attack and set +100 ms. Place
   another Note above that lock to compare the two envelopes in one chord.
4. Put a lock in the first step of an upper lane at division 8. Put Notes on
   each step of a lower lane at division 16. Leave the upper lane's second
   step empty to hear the lock expire back to the base settings.
5. Try Cycle and Probability Gates above the lock, then a Jump to a branch.
   Stop/start to compare the same seeded traversal.

## Channel walkthrough

1. Create two regular lanes and put notes in both. Leave the first on channel
   1 and use the second head's `CHANNEL` selector to assign channel 8.
2. Set channel 1's Wave A/B to Sine and channel 8's Wave A/B to Square. Start
   playback and compare the two sounds. While playing, change the second
   lane's assignment and confirm that its next notes change without restarting.
3. Assign both heads to channel 8. Edit Amp Release through one head and
   reopen Sound through the other; both should show the same value. Cancel
   a channel candidate and verify that its assignment remains unchanged.
4. Add a Jump and a nested Jump. Inspect the branch status for `CH 8`, then
   move the first Jump to a lane on channel 1 and check both descendants.
5. On channel 8, put an enabled +100 ms Attack lock above a note in a slow
   lane. Compare a faster lane on channel 8 with one on channel 1. Reassign
   the slow lane while its lock is held; the lock should follow its channel
   on the next slice, without changing notes already sounding.
6. Enable Reverb only on channel 8, set global Amount to an audible level,
   and play both channels. Toggle that channel's Reverb during a long note:
   the held note keeps its send, future notes use the new setting, and dry
   notes should not cut off the existing wet tail. Stop and listen to the
   shared tail decay.

This listening/controller walkthrough complements the automated checks;
completion and any remaining gaps are recorded in [validation.md](validation.md).
