# Jacquard UI Appearance Plan

## Objective and Scope

Bring the Score Plane's appearance closer to Jacquard while keeping the
320 x 240 PlayStation interface readable and practical to implement. Prioritize
the dotted background, grayscale hierarchy, tile silhouettes, recognizable
icons, and typography. Add gamepad placement of different visual tile kinds
so the result can be evaluated through normal editing.

This is a plan, not a record of implemented or visually verified changes.
It extends `../implementation-plan.md`: multiple visual tile kinds and icon
reproduction replace that plan's single-kind and placeholder-shape choices.
The remaining model limits and editing rules stay in effect.

Audio, playback, parameter editing, stacks, jump connections, saving, and a
complete reproduction of Jacquard's panels are outside this iteration. Tile
kinds carry appearance only; a gate or jump icon does not imply its behavior
has been implemented. Keep the ordinary startup plane empty.

## Reference and Current Baseline

The reference is the local Jacquard checkout at
`/Users/keijiro/Projects/jacquard/main`, inspected on 2026-09-22 at commit
`5f02d3d` with a clean working tree. Paths below are relative to that checkout.

| Reference | Information to carry over |
| --- | --- |
| `Assets/Jacquard/UI/Style.cs` | Neutral gray palette, 30 x 32 tile bodies, 34 x 36 cell pitch, radius 5, and text sizes. |
| `Assets/Jacquard/UI/ScoreView.cs` | Centered square lattice dots in empty cells, dotted horizontal rails, and triangular pass-through markers on empty lane steps. |
| `Assets/Jacquard/UI/TileElement.cs` | Outlined notes, gray parameter/gate tiles, bright flow tiles, note labels, and inverted channel labels. |
| `Assets/Jacquard/UI/TileIcons.cs` | Faders, gate patterns, probability wedges, U-turns, jump arrows, and entry arrows; most icons use a 15 x 15 drawing box. |
| `Assets/UI/Fonts/Jura-Regular.ttf` | Jura's thin, monoline letterforms and spacing. |
| `Assets/UI/Fonts/OFL.txt` | Attribution and license accompanying the font asset. |

The current prototype uses 16-pixel square cells in a 19 x 10 viewport,
continuous grid lines, colored rectangular tiles, and the SDK's 8 x 8 debug
font. `Lane.tiles` holds boolean occupancy, and `PLACE TILE` commits a single
generic tile immediately. These are the main integration points.

Reference findings above come from source inspection. Capture a representative
Jacquard screen before visual tuning; this plan does not claim a rendered
comparison has already been performed.

## Visual Specification

### Geometry and Background

Start with the existing 16 x 16 pitch and viewport. Prototype a 13 x 14 tile
body with a pixel-stepped, approximately 2-pixel corner radius and a 1-pixel
note outline. This introduces the original's slightly tall rounded silhouette
and visible gutters without first changing navigation density. These dimensions
are candidates to evaluate, not measured final values.

Replace the grid with a 1 x 1 square at each empty cell's center; compare a
2 x 2 variant if single pixels are too faint in emulator output. A lattice dot
marks a logical cell center, not the former intersection of grid lines. Omit
it for heads, endpoints, tiles, and empty lane steps.

Draw a subdued dotted rail along each lane and a small right-pointing triangle
on each empty step. Anchor the rail's dot phase to the lane's logical origin
so scrolling does not change the pattern. Clip it to the plane viewport and
draw tile bodies over it.

Compare one fallback layout with an 18 x 18 pitch and 15 x 16 bodies if the
compact icons or labels cannot be distinguished. Derive visible rows/columns
and scrolling bounds from the selected geometry. Settle on one layout before
producing all final assets; runtime zoom and a size selector are unnecessary.

### Grayscale and Selection

Use a single intensity for every UI color (`r = g = b`), including messages,
menus, cursor, resize preview, and disconnected-controller guidance.

| Role | Jacquard source intensity | Initial use |
| --- | --- | --- |
| Plane background | `0x16` | Screen and note interiors |
| Lattice dots | `0x4e` | Empty-cell positions |
| Parameter/gate field | `0x34` | Medium-dark tile bodies |
| Note outline / flow field | `0xe8` | Thin note borders and solid flow tiles |
| Text / cursor | `0xf2` | Light ink and selection |
| Empty-step marker | `0x9a` | Pass-through triangles |
| Panel / panel border | `0x1e` / `0x3a` | Context menus and picker |

These are starting RGB inputs. Check the resulting contrast after the PS1's
color quantization and emulator presentation. Approximate the original rail's
35% opacity with a precomputed gray over the plane background; an alpha effect
is unnecessary for this static surface.

Use dark ink on bright flow tiles and light ink on note and gray tiles. Keep
the cursor outside the tile body, initially as four bright corner brackets.
Use a different outline pattern for the candidate endpoint, retaining the
off-screen arrow and `END` coordinate. Confirm that selection is visible over
every tile style without obscuring its icon. Color must carry no information.

### Tile Families and Icons

Implement the following small visual vocabulary. Use fixed representative
values so the work remains a GUI prototype.

| Element / picker item | Body | Interior |
| --- | --- | --- |
| Lane head (automatic) | Bright filled | Compact `CH` label; full `CH1` if it fits legibly |
| Note | Outline on background | Fixed `C4` label |
| Absolute parameter | Gray filled | Vertical fader |
| Relative parameter | Gray filled | Fader with up/down modifier |
| Cycle gate | Gray filled | Four boxes, with alternating filled and hollow boxes |
| Probability gate | Gray filled | Circle with a fixed half-filled wedge |
| Jump | Bright filled | Bent, Z-like rightward arrow |
| Lane endpoint (automatic) | Bright filled | U-turn arrow |

The head and endpoint retain their existing structural roles. Do not expose
them as ordinary placeable tiles. Jump destinations and other icon variants
can follow after this set is readable; they are not required for completion.

Redraw the reference paths on an integer pixel grid, initially within a
9 x 10 interior box. Preserve distinctive features rather than scaling all
15 x 15 paths mechanically. In particular, separate the relative fader's
modifier from its shaft and keep the cycle gate's hollow centers open. If
these fail at native size, use the geometry fallback above before sacrificing
their identity. Inspect dark-on-light and light-on-dark symbols separately.

Use a small indexed bitmap atlas for tile shells and icons, with transparent
surroundings and opaque tile interiors. Prepare pixel masks as explicit,
editable data and generate the atlas deterministically; avoid emitting a GPU
primitive for each lit pixel. Share masks through palette variants when useful.
Vector tessellation at runtime and AI-generated icon images are unnecessary
for these small, precisely defined shapes.

### Typography

Compare two candidates using the same native-resolution UI sample:

1. Jura rasterized offline at the actual target size, with integer placement
   and no runtime rescaling.
2. A custom bitmap face guided by Jura's letterforms, with one-pixel strokes
   and manually adjusted counters and spacing.

Start with a 5 x 7 ink area and approximately 6-pixel advance for compact UI
text. Allow glyph-specific widths where the forms need them. Test a slightly
larger face if the smaller candidate loses its character or readability. Draw
tile labels with the same face if possible; only add a compact label variant
if the selected tile geometry requires it.

Compare `CH1`, `C4`, `C#4`, `0/O`, `1/I`, coordinates, menu labels, errors, and
controller guidance on dark and bright backgrounds. The final subset must
cover all actual UI strings: uppercase letters, digits, spaces, and their
punctuation. Provide a visible fallback glyph. Lowercase, Japanese text, and
a general Unicode renderer are outside scope.

Prefer the rasterized original if it remains clear; otherwise author the
custom bitmap face. Do not assume automatic downsampling is sufficient.
Store glyph metrics alongside the atlas and measure menu widths with those
metrics instead of `strlen * 8`. Replace the shipping UI's `FntSort` path;
the SDK font can remain a development fallback. Keep font attribution and
the accompanying license with any imported or derived font assets, and record
asset provenance alongside their editable sources.

## Making Icon Tiles Placeable

Keep `Lane.tiles` as a fixed byte array, but give its values explicit visual
kind IDs: zero for empty, followed by the six picker kinds above. Retain
`CellKind` for structural classification. The renderer obtains a tile's
visual kind through the classified lane and step.

Replace boolean placement with an explicit kind-aware placement operation;
keep deletion explicit. Validate kind IDs and require an empty lane step.
Invalid placement must leave the model unchanged. Existing resize protection
must treat every nonzero kind as occupied. Update all callers and fixtures
that currently pass `1` to `score_tile` so the change is not hidden behind
boolean-compatible integer arguments.

Add a tile-picker editor state with this interaction:

1. On an empty step, open the context menu and select `PLACE TILE`.
2. Show a bounded list of the six kinds with icon samples and readable labels.
   Up/Down changes the candidate; show its full tile appearance in a preview.
3. Cross places the candidate and returns to the plane. The opening press
   must not also commit the tile.
4. Circle returns to the context menu with `PLACE TILE` selected and writes
   nothing to the score. Circle from that menu returns to the plane.

Default to Note and remember the last successfully placed kind during the
session. Browsing and cancellation must not update that remembered kind.
Keep cursor coordinates and the placement target fixed while the picker is
open. Show preview content as an overlay without writing into `Score`.

Retain deletion and length editing on occupied steps; replacing a tile can
remain delete-then-place. The picker is separate from the existing three-item
context menu, so it does not require increasing that menu's action capacity.
Give the picker its own panel height, clipping, and screen-edge placement.
Handle Circle's picker-specific transition before the existing global cancel
branch. Extend mode-dependent guidance and repeat-reset behavior for the new
state, including controller disconnect/reconnect handling.

## Implementation Sequence

| Stage | Work and main files | Exit condition |
| --- | --- | --- |
| 1. Visual study | Capture Jacquard; make a 320 x 240 comparison sheet containing dots, all bodies/icons, cursor, menu, and font candidates. | Select pitch, body size, icon detail, and font direction at native size. |
| 2. Shared drawing assets | Add a compact style definition and bitmap asset/text helpers beside `src/render.*`; add deterministic asset generation and build integration in `CMakeLists.txt`. | The same samples render on the actual PS1 GPU path with correct palette, transparency, and text metrics. |
| 3. Plane appearance | Update `src/render.c` for grayscale, dots, dotted rails, markers, rounded bodies, structural icons, and selection. | Empty and occupied planes scroll consistently and selection remains readable. |
| 4. Visual kinds and picker | Update `src/score.*`, `src/editor.*`, and picker rendering together. | Every ordinary kind can be selected, placed, canceled, and deleted with the gamepad. |
| 5. Validation and handoff | Extend relevant tests; build Debug/Release; inspect emulator output; update README and validation record. | Acceptance cases below pass, with unverified hardware results explicitly identified. |

Keep additions small: the renderer should not know musical behavior, and the
model should not know atlas coordinates or grayscale values. Store final pixel
decisions and implementation-specific constraints beside the asset data or
code they govern. Use this document for the cross-file scope and workflow,
without repeating those future comments.

## Rendering Constraints and Validation

Retain fixed storage, double buffering, and allocation-free frames. Reserve an
explicit VRAM area for the atlas and CLUTs outside both 320 x 240 framebuffers
and any retained SDK font area. Verify texture-page boundaries, UV bounds,
transparent palette entries, and ordering-table traversal with the pinned SDK.
Layer backgrounds, rails, bodies, symbols, cursor, and panels deliberately;
submission order alone is not a reliable description of their GPU draw order.

The current packet budget is 32,768 bytes per buffer. The earlier measured
12,752-byte peak does not predict the new renderer's peak: dots, dotted rails,
sprites, text, and the picker change the workload. Extend the GPU stub for
the actual textured primitives and texture setup packets, then measure again.
Keep overflow rejection and debugger counters. Do not raise the budget before
checking avoidable packet duplication.

Required acceptance checks:

- Start from an empty plane, create a lane, and place each of the six kinds
  without a debug fixture. Scroll away and back; each kind remains intact.
- Cancel the picker and confirm the score is byte-for-byte unchanged. Check
  held Cross, direction repeat, cursor retention, and reconnect behavior.
- Reject invalid kinds, placement on occupied/structural cells, and shortening
  across any tile kind. Verify deletion and lane-slot reuse clear kind data.
- Run render bounds and packet checks across plane coordinates and every UI
  mode, including all picker selections, longest labels, all corners, and
  maximum occupancy. Include sparse long lanes: their exposed dotted rails
  may stress a different path from fully populated lanes.
- Confirm grayscale throughout the rendered UI, recognizable tile families,
  distinct absolute/relative parameter icons, and readable gate patterns.
- Check the resize endpoint, off-screen arrows, selection on bright tiles,
  background-dot alignment, and rail continuity during scrolling.
- Compare captures with Jacquard by visual role and shape, using native
  320 x 240 output and integer enlargement without smoothing. Also inspect
  the emulator's normal presentation, where pixel aspect and scaling can
  affect the apparent proportions and small text.
- Build Debug and Release and run the ordinary editing walkthrough in the
  emulator. Record packet peak, overflow count, and frame behavior under load.
  Host tests alone cannot establish appearance, texture correctness, or timing.

Provide one reproducible comparison arrangement containing empty space, empty
steps, each tile kind, head/end, and selection. A development-only fixture may
speed up repeated captures, but user placement remains the acceptance path
and the normal startup stays empty. Save comparison captures and record the
selected geometry and font outcome in `docs/validation.md` after verification.
Update README controls, tile descriptions, and the former color-based guidance
in the same implementation change. Physical-console testing is useful follow-up
and must not be reported as completed unless actually performed.
