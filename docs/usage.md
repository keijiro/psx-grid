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
| Regular head (`L`) | Length, step division, global Sound settings, delete lane |
| Relative Lock | Separate Attack/Release enable switches and signed millisecond offsets; both disabled initially |
| Branch head (`B`) | Length, delete lane; division inherited from its source |
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
4,096 tiles. Regular lanes start at 16 steps; all lanes allow 1–64 steps.
Stack depth is limited by the bottom of the plane. A head at `(x,y)` has steps
at `(x+1,y)` through `(x+length,y)` and an endpoint at `(x+length+1,y)`.
Collision, boundary, and capacity failures leave the score unchanged. Shortening
a lane cannot discard occupied trailing steps.

Division denominators are `1, 2, 3, 4, 6, 8, 12, 16, 24, 32, 48, 64`, initially
16. Auditioning, effects, waveform/channel/tempo selection, absolute locks,
saving, loading, and undo are outside this milestone.

## Playback and sound

START toggles playback in every editor mode without applying an unfinished
property edit or move. It never repeats while held. Playback starts at step
zero with lap zero on every regular lane; branches have no independent runner.
Stopping silences voices with a short ramp. Starting again restarts the score.

Committed score and global sound edits are published during playback, after
any time slice already being processed has finished. Notes, gates, jumps, and
new locks use the updated contents when their steps are next read; a held
lock uses its updated value on subsequent slices until its step ends. Deleted
locks stop contributing after publication. Unconfirmed property candidates
and moves are not published.

Existing regular lanes retain their next deadline and lap count. A division
change applies to the next step's duration; it does not move the deadline of
the current step. Moving a head changes runner order. If the visited branch
is deleted, its runner returns to step zero of its origin; if its lane is
shortened past the next step, it returns to step zero of that lane. Neither
repair increments the lap count. Removing an origin removes its runner.
Already sounding notes keep their scheduled gates and envelopes.

The first regular head in top-to-bottom, then left-to-right order is the
master. New regular lanes join at a master lap boundary. That boundary is
assigned when the master reads its final step, so a lane published while that
step is already sounding waits one additional lap. A new lane that becomes
the master starts at publication instead, so it can provide that boundary. Playback stays enabled with no regular lanes; the first lane drawn
then starts at publication. Deleting and recreating a lane creates a new
runner, even if the same pool slot is reused.

The status area shows `PLAYING` or `STOPPED`, with `APPLYING EDITS` while a
committed revision is waiting for publication. There is no Transport Row or
playhead.

Tempo is fixed at 120 BPM. A step lasts 2000 divided by the lane division in
milliseconds: sixteen steps at division 16 make a two-second loop. Notes keep
their written gate lengths and overlap across steps. Release begins at gate-off,
even if Attack is still in progress. Up to 24 SPU voices share one logical
sound channel. Extreme density may steal voices or skip overdue notes.

A reached Jump selects the next step from its branch, after finishing the
current stack. The last reached Jump wins. Any terminator returns the runner
to its original regular lane and increments its lap, with no extra empty step.
Cycle gates select bit zero on the first lap. Probability gates use a repeatable
random sequence reset at each start.

Open `SOUND` on any regular head to edit **global** Attack and Release. Both
start at 5 ms and range from 0 to 16,000 ms. These are software volume ramps
on compressed sine samples; see the measured limitations in [validation.md](validation.md).

Relative Locks add signed offsets from -16,000 to +16,000 ms. Enable a target
before changing its offset. Enabled zero is distinct from disabled; disabling
clears the offset. Both targets may be enabled in one tile (`A` and `R` labels).

Runners execute by their original heads, top to bottom, then left to right.
Within each stack, locks affect notes below them and subsequent lower runners.
Each addition clamps immediately to 0–16,000 ms. The reached locks remain held
until that runner's next step; an empty step clears them. Faster lower lanes
hear a slower upper lane's held locks. Notes already sounding retain their
original Attack and Release, and the global base settings never accumulate
lock changes.

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
2. Select its head, open `SOUND`, and try a long Release. Stop during a tail.
3. Place a Relative Lock above a Note. Enable Attack and set +100 ms. Place
   another Note above that lock to compare the two envelopes in one chord.
4. Put a lock in the first step of an upper lane at division 8. Put Notes on
   each step of a lower lane at division 16. Leave the upper lane's second
   step empty to hear the lock expire back to the base settings.
5. Try Cycle and Probability Gates above the lock, then a Jump to a branch.
   Stop/start to compare the same seeded traversal.
