# Using the Score Plane

See [development.md](development.md) for setup, build, and emulator launch
instructions.

## Controls

| State | D-pad | X / Cross | O / Circle |
| --- | --- | --- | --- |
| Plane | Move the cursor; scroll at edges | Open menu | No action |
| Menu | Select an item with Up/Down | Execute | Close |
| Tile picker | Select a kind with Up/Down | Place | Return to menu |
| Change length | Change the candidate with Left/Right | Confirm | Cancel |
| Delete lane confirmation | Select Cancel/Delete with Left/Right | Confirm | Cancel |

1. At the initial position `(1,1)`, press X and confirm `NEW LANE` with X.
2. Move right, then select `PLACE TILE` with X. Choose a kind with Up/Down
   and press X again to place it. Circle returns to the menu without placing.
3. At the same position, select `DELETE TILE` with X to remove only the tile.
4. Select `CHANGE LENGTH` to resize the lane. A dashed outline marks the
   candidate endpoint. An off-screen endpoint appears as an edge arrow with
   its `END` coordinate. Press Circle to return without changing the data.
5. Return to the bright `CH` lane head, then select `DELETE LANE`, Right, and X to
   delete the lane and its tiles.

The grayscale plane uses center dots, dotted lane rails, and triangles on
empty steps. Bright `CH` heads and U-turn endpoints bound each lane. The cursor
uses bright corner brackets outside the tile body.

The picker offers Note (`C4`), Absolute Parameter, Relative Parameter, Cycle
Gate, Probability Gate, and Jump. Notes have outlined bodies, parameters and
gates use gray fields, and jumps use bright fields. These are visual kinds
only: they do not implement music, parameters, probabilities, or jump behavior.
The picker starts with Note and remembers the last successfully placed kind;
browsing and cancellation leave both that choice and the score unchanged.
To replace an occupied tile, delete it and place another.

Editing pauses while the controller is disconnected and until held buttons
are released after reconnection. Cross and Circle act only on the initial
press; changing modes resets direction repeat.

A held direction starts repeating after 18 frames and then repeats every
3 frames (about 300 ms and 50 ms under NTSC). Opposite directions cancel each other;
horizontal movement takes precedence when both axes are active.

## Plane and Editing Limits

The plane starts empty, with 128 columns and 64 rows. Coordinates are
zero-based; the cursor starts at `(1,1)`. The model supports up to 16 lanes
of 1-64 steps each; new lanes contain 16 steps. A lane occupies its head, every step, and its endpoint. The editor
rejects collisions, positions outside the plane, and capacity overflows. If a
shorter length would discard tiles, it asks the user to delete those tiles
first. Menus and canceled operations preserve the cursor position.

A lane with head `(x,y)` has steps from `(x+1,y)` through `(x+length,y)`
and an endpoint at `(x+length+1,y)`. Each step holds at most one tile.
New lanes use the cursor position as their head; creation fails if the initial
length does not fit. Lane deletion starts with Cancel selected.

Audio generation, playback, musical tile behavior, parameter editing, jump
destinations/connections, stacks, branch lanes, lane and tile movement,
copying, automatic extension at endpoints, saving, and loading are outside
the prototype's scope.
