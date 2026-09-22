# psx-grid Implementation Plan

## 1. Objective and Completion Criteria

Based on `plan.md`, build a GUI prototype for operating Jacquard's Score Plane
on PlayStation. The prototype evaluates cursor positioning with the D-pad and
the context menu opened with the X (Cross) button.

The implementation is complete when the following workflow can be performed
entirely with a gamepad:

1. Move the cursor across an empty Score Plane.
2. Open a menu with X and create a lane.
3. Place a tile on the lane, select it, and delete it.
4. Extend or shorten a lane and delete it when needed.
5. Edit multiple lanes and positions outside the initial viewport.

Audio generation, sequence playback, tile-specific behavior and parameter
editing, saving, and loading are outside the scope. This document is an
implementation plan; at the time it was written, the environment and program
had not yet been created.

## 2. References and Adopted Approach

### PlayStation Development Environment

Use the following files from `../psx-test` as references:

- `README.md` and `toolchain.lock`: setup instructions and pinned dependency
  details.
- `CMakeLists.txt` and `CMakePresets.json`: generation of Debug and Release
  PS-X EXE and ELF files from C sources.
- `scripts/setup.sh`, `scripts/env.sh`, and `scripts/run.sh`: environment setup,
  zsh configuration, and emulator launch.
- `patches/`: SDK compatibility patches.
- `src/main.c`: 320 x 240 rendering initialization, double buffering, gamepad
  input, and held-direction repeat behavior.

Use the versions pinned by that project as the baseline: PSn00bSDK v0.24, MIPS
GCC 16.2.0, binutils 2.47, PCSX-Redux build 250, and its bundled OpenBIOS.
These versions come from the reference files; selecting the latest releases is
not part of this plan.

Adapt the configuration, scripts, and patches to this project, and use
`psx-grid` consistently for artifact names. Store the SDK, emulator, and
configuration inside this project under locations such as `.local/`. Reuse an
existing system toolchain after verifying its version. Do not bring over the
synthesizer implementation.

### Jacquard Model and UI

The plan references the following files under
`/Users/keijiro/Projects/jacquard/main`:

- `Assets/Core/Model/Lane.cs`: integer grid, horizontal lanes, head and endpoint
  cells, steps, and vertical tile stacks.
- `Assets/Core/Model/Score.cs`: cell classification, occupancy checks, and
  available-space checks when extending a lane.
- `Assets/Jacquard/App/ScoreEditor.cs`: creation, placement, deletion, length
  changes, and cursor selection.
- `Assets/Jacquard/UI/ScoreView.cs`: display of the grid, rails, tiles, and
  cursor.

Replace the grid-coordinate and cell-kind concepts with a small C model instead
of directly porting the C# or Unity UI.

## 3. Prototype Specification

The following values and simplifications are initial implementation choices
that do not appear in `plan.md`. Define them as constants so they can be tuned
after evaluating the interaction.

### Score Plane and Data

- The logical plane contains 128 columns and 64 rows. The cursor starts at
  `(1, 1)`, and the plane is empty at startup.
- Support up to 16 lanes, each containing 1-64 steps. A new lane contains 16
  steps.
- A lane stores its head position and length. For a head at `(x, y)`, steps
  begin at `(x + 1, y)` and the endpoint is `(x + length + 1, y)`.
- Treat the head, every step, and the endpoint as occupied. Reject creation or
  extension that overlaps another lane or leaves the plane.
- Provide one visual-only kind of regular tile, with at most one tile per step.
  Keep the head and endpoint distinct from regular tiles.
- Jacquard's stacks, branch lanes, lane and tile movement, copying, and
  automatic extension by placing at an endpoint are outside the scope.
- If capacity is exhausted or a collision occurs, leave the data unchanged and
  display a brief explanation.

### Input and Screen States

| State | D-pad | X (Cross) | O (Circle) |
| --- | --- | --- | --- |
| Plane editing | Move one cell in four directions | Open the menu for the current position | No action |
| Context menu | Select an item with Up/Down | Execute the item | Close |
| Length change | Adjust the candidate with Left/Right | Confirm the change | Restore the original length and close |
| Delete confirmation | Select Cancel/Delete with Left/Right | Confirm the selection | Cancel |

- Respond to X and Circle only on the initial press so holding a button cannot
  repeatedly confirm or delete.
- Respond to the D-pad immediately, begin repeating after about 300 ms, and use
  an initial repeat interval of about 50 ms.
- Prevent diagonal movement: opposite directions on either axis cancel each
  other, and horizontal input takes precedence when both axes are active.
- Reset repeat state during state transitions. The X press that opens a menu
  must not also execute its first item.
- Suspend editing while the controller is disconnected and discard stale input
  after reconnection.

### Menu by Selected Position

| Cell kind | Menu items |
| --- | --- |
| Empty space outside a lane | New lane, Close |
| Lane head | Change length, Delete lane, Close |
| Empty step within a lane | Place tile, Change length, Close |
| Regular tile | Delete tile, Change length, Close |
| Lane endpoint | Change length, Close |

- Use the cursor position as the head of a new lane. If the initial length does
  not fit there, ask the user to move; do not place it automatically on another
  row.
- Close the menu after placing or deleting a tile, and leave the cursor on its
  original cell.
- Preview the candidate endpoint while changing length, and do not modify the
  original data until confirmation.
- Do not allow a candidate length that would discard tiles. Tell the user to
  delete those tiles first.
- Because deleting a lane also deletes its tiles, show a confirmation with
  Cancel selected initially.
- Preserve the cursor coordinates after a length change or lane deletion, then
  classify the cell again.

### Rendering and Scrolling

- Target 320 x 240. Place position and state information at the top, the Score
  Plane in the center, and control guidance at the bottom.
- Start with 16-pixel cell spacing. Distinguish the grid, horizontal rail, head,
  endpoint, regular tile, and cursor by shape and color.
- Scroll by whole cells when the cursor approaches a viewport edge. Keep
  logical and screen coordinates separate so the same editing rules apply
  outside the viewport.
- Constrain menus to the screen and stop the cursor behind a menu from moving.
- Use short English labels and simple shapes in the initial version. Japanese
  fonts and reproducing every Jacquard icon are outside the scope.
- Draw in this order: background, grid/rails, tiles, cursor, menus/guidance.
  Draw only the visible area.

## 4. Implementation Structure

| File | Responsibility |
| --- | --- |
| `src/main.c` | Initialization, frame progression, and input/edit/render calls |
| `src/input.c`, `src/input.h` | Controller state, press detection, and direction repeat |
| `src/score.c`, `src/score.h` | Lane and tile management, cell classification, and boundary/collision/capacity checks |
| `src/editor.c`, `src/editor.h` | Cursor, menus, candidate lengths, and confirmation state |
| `src/render.c`, `src/render.h` | GPU initialization, coordinate conversion, scrolling, and UI rendering |
| `CMakeLists.txt`, `CMakePresets.json` | PS-X EXE and ELF builds |
| `scripts/`, `patches/`, `toolchain.lock` | Reproducible development environment |
| `tests/score_test.c` | Host-side boundary and consistency tests for the model |
| `README.md`, `docs/validation.md` | Execution and control instructions, validation results, and known limitations |

Store lanes and steps in bounded fixed arrays to avoid per-frame dynamic
allocation. Keep the model independent of the SDK. Centralize edit validation
in the model and share it between menu visibility and execution-time checks.
Estimate maximum render-packet usage and check the buffer boundary.

## 5. Implementation Steps and Completion Criteria

### Step 1: Development Environment and Minimal Executable

1. Adapt the build files, scripts, lock data, and patches from `psx-test`.
2. Change the target name, EXE path, and description to `psx-grid`.
3. Exclude `.local/`, `third_party/`, and `build/` from version control.
4. Implement only display initialization and simple text, then build Debug and
   Release configurations.

Completion criterion: the project scripts can launch the emulator and display
the EXE from both configurations. Running setup again preserves the pinned
configuration.

### Step 2: Score Model

1. Define grid coordinates, lanes, tiles, and cell kinds.
2. Implement cell lookup, lane creation and deletion, tile placement and
   deletion, and length changes.
3. Enforce boundaries, capacity, occupancy, and tile protection when shortening.
4. Use host tests to verify that rejected operations leave the data unchanged.

Completion criterion: overlaps, including heads and endpoints, are prevented,
and only valid edits are applied.

### Step 3: Score Plane and Cursor

1. Implement a double-buffered render loop and controller input.
2. Draw the grid, lanes, tiles, and cursor.
3. Implement initial and repeated D-pad movement, boundary constraints, and
   cursor-following scroll.

Completion criterion: development fixtures with multiple lanes are visible,
and the cursor can reach a target cell by scrolling in every direction. Restore
the empty startup plane in the final version.

### Step 4: Context Menus and Editing

1. Route input according to screen state.
2. Build menus from the selected cell kind and connect lane creation and tile
   placement/deletion.
3. Implement lane-length preview, confirmation, and cancellation, plus lane
   deletion confirmation.
4. Display reasons for rejected operations and state-specific control guidance.

Completion criterion: using only the gamepad, a user can start from an empty
plane and create, place, delete, resize, and delete a lane. Cancellation leaves
the data unchanged.

### Step 5: Interaction and Load Validation

1. Run the following acceptance scenarios in Debug and Release configurations.
2. Tune cursor speed, repeat intervals, cell dimensions, colors, and menu
   placement through direct interaction.
3. Measure render-packet usage and frame processing with the maximum lane count
   and length.
4. Document execution steps, controls, limitations, validation environment,
   and results.

Completion criterion: continuous editing in the emulator produces no visual
corruption, unintended input, or data inconsistency and continues to update on
each VSync. Record whether the result was also tested on physical hardware.

The dependency order is Step 1 -> Step 2 -> Step 3 -> Step 4 -> Step 5.

## 6. Acceptance Scenarios

| Area | Check |
| --- | --- |
| Initial workflow | Create a lane on an empty plane, then place and delete a tile on any step. |
| Cell classification | Empty space, head, empty step, regular tile, and endpoint each produce the appropriate menu. |
| Input | Initial press, hold, direction change, simultaneous directions, holding X, and cancellation with Circle behave as specified. |
| State transitions | The press that opens a menu does not execute an item, and the cursor behind an open menu does not move. |
| Extension | Changes that collide, including at the endpoint, or leave the plane are rejected without altering the original data. |
| Shortening | Lengths below the minimum and changes that discard tiles are rejected; valid shortening and cancellation work. |
| Deletion | Deleting a regular tile preserves its lane; confirmed lane deletion also removes its tiles. |
| Capacity | Failed creation or extension at maximum lane count or length does not damage existing data. |
| Viewport | Moving off-screen and back preserves the selected position, and menus remain readable in all four corners. |
| Connection | Editing stops while the controller is disconnected, and reconnection does not trigger unintended operations. |
| Repeated editing | Display and model remain consistent through repeated creation, deletion, extension, and shortening. |

Verify model boundaries, collisions, shortening, and capacity limits with host
tests. Verify input and screen-state transitions, visibility, and interaction
in the emulator. Do not record unverified items as passed.

## 7. Deliverables

- A reproducible development environment and C sources within this project.
- `build/debug/psx-grid.elf`, `build/debug/psx-grid.exe`, and their Release
  counterparts.
- A `README.md` covering setup, build, launch, and controls.
- A `docs/validation.md` recording validation results, differences from
  Jacquard, and known limitations.
