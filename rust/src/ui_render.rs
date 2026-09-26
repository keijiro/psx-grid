//! Draws the grid and editor menus from caller-owned state.
//!
//! Layout, clipping, text, and camera state live here. The C backend owns
//! PlayStation GPU packets and submits complete frames on the main thread.

// Implementation notes:
// OT depths reverse insertion order within each bucket. Preserve the C
// renderer's draw order, including tile labels before their body sprites.

use core::ffi::{c_char, c_int, CStr};
use core::fmt::Write;

use crate::editor::{rows, Editor, EditorRow};
use crate::score::{
    at, resolve, score_channel, score_note_name, score_tile_label, Cell, Score,
};
use crate::score_edit::{score_plan_move, MovePlan};
use crate::storage::storage_message;
use crate::ui_format::{row_value, Text};
use crate::ui_metrics::ADVANCE;

const SCREEN_W: c_int = 320;
const SCREEN_H: c_int = 240;
const CELL_SIZE: c_int = 16;
const VIEW_COLS: c_int = SCREEN_W / CELL_SIZE;
const VIEW_ROWS: c_int = SCREEN_H / CELL_SIZE;
const SCORE_WIDTH: c_int = 128;
const SCORE_HEIGHT: c_int = 64;
const UI_DOT: c_int = 0x4e;
const UI_INK: c_int = 0xf2;
const UI_PANEL: c_int = 0x1e;
const UI_BORDER: c_int = 0x3a;
const UI_SHADOW: c_int = 0x0c;
const UI_RULE: c_int = 0x60;
const UI_RAIL: c_int = 0x60;
const UI_MENU_MARGIN: c_int = 12;
const UI_MENU_ROW: c_int = 17;
const UI_MENU_EDGE: c_int = 16;
const EDIT_PLANE: c_int = 0;
const EDIT_DELETE: c_int = 2;
const EDIT_PICKER: c_int = 3;
const EDIT_PATTERN: c_int = 4;
const EDIT_MOVE: c_int = 5;
const EDIT_SOUND: c_int = 6;
const EDIT_MAIN: c_int = 7;
const EDIT_REVERB: c_int = 8;
const ROW_HEADING: c_int = 0;
const TILE_NOTE: c_int = 1;
const TILE_CYCLE: c_int = 2;
const TILE_PROBABILITY: c_int = 3;
const TILE_JUMP: c_int = 4;
const TILE_RELATIVE: c_int = 5;
const TILE_KIND_COUNT: c_int = 6;
const CELL_EMPTY: c_int = 0;
const CELL_HEAD: c_int = 1;
const CELL_STEP: c_int = 2;
const CELL_TILE: c_int = 3;
const CELL_END: c_int = 4;
const LOCK_ATTACK: c_int = 1;
const LOCK_RELEASE: c_int = 2;

// The renderer is main-thread only, like the C packet backend.
static mut CAMERA_X: c_int = 0;
static mut CAMERA_Y: c_int = 0;

unsafe extern "C" {
    fn render_backend_begin();
    fn render_backend_tile(
        depth: c_int,
        x: c_int,
        y: c_int,
        w: c_int,
        h: c_int,
        gray: c_int,
    );
    fn render_backend_sprite(
        depth: c_int,
        x: c_int,
        y: c_int,
        u: c_int,
        v: c_int,
        w: c_int,
        h: c_int,
    );
    fn render_backend_present();
}

/// Reads a label owned by a static model or editor table.
///
/// # Safety
/// `ptr` must point to a static NUL-terminated string.
unsafe fn cbytes(ptr: *const c_char) -> &'static [u8] {
    // SAFETY: The caller passes a pointer to a static model/editor label.
    unsafe { CStr::from_ptr(ptr).to_bytes() }
}

fn message(editor: &Editor) -> &[u8] {
    // SAFETY: The editor owns a live message pointer throughout the frame.
    unsafe { CStr::from_ptr(editor.message).to_bytes() }
}

/// Clips panel primitives to protected display margins and grid primitives
/// to the full frame.
fn rect(
    depth: c_int,
    mut x: c_int,
    mut y: c_int,
    mut w: c_int,
    mut h: c_int,
    gray: c_int,
) {
    let top = if depth <= 2 { UI_MENU_EDGE } else { 0 };
    let bottom = if depth <= 2 {
        SCREEN_H - UI_MENU_EDGE
    } else {
        SCREEN_H
    };
    if x < 0 {
        w += x;
        x = 0;
    }
    if y < top {
        h += y - top;
        y = top;
    }
    if x + w > SCREEN_W {
        w = SCREEN_W - x;
    }
    if y + h > bottom {
        h = bottom - y;
    }
    if w <= 0 || h <= 0 {
        return;
    }
    // SAFETY: The backend has an open frame; clipping keeps the packet valid.
    unsafe { render_backend_tile(depth, x, y, w, h, gray) };
}

fn outline(depth: c_int, x: c_int, y: c_int, w: c_int, h: c_int, gray: c_int) {
    rect(depth, x, y, w, 1, gray);
    rect(depth, x, y + h - 1, w, 1, gray);
    rect(depth, x, y, 1, h, gray);
    rect(depth, x + w - 1, y, 1, h, gray);
}

/// Moves the atlas origin with the clipped destination so partial sprites
/// retain their source pixels.
fn sprite(
    depth: c_int,
    mut x: c_int,
    mut y: c_int,
    mut u: c_int,
    mut v: c_int,
    mut w: c_int,
    mut h: c_int,
) {
    let top = if depth <= 2 { UI_MENU_EDGE } else { 0 };
    let bottom = if depth <= 2 {
        SCREEN_H - UI_MENU_EDGE
    } else {
        SCREEN_H
    };
    if x < 0 {
        u -= x;
        w += x;
        x = 0;
    }
    if y < top {
        v += top - y;
        h += y - top;
        y = top;
    }
    if x + w > SCREEN_W {
        w = SCREEN_W - x;
    }
    if y + h > bottom {
        h = bottom - y;
    }
    if w <= 0 || h <= 0 {
        return;
    }
    // SAFETY: The backend has an open frame; clipped source and destination
    // match the original atlas geometry.
    unsafe { render_backend_sprite(depth, x, y, u, v, w, h) };
}

fn glyph(ch: u8) -> usize {
    usize::from(if (32..=126).contains(&ch) {
        ch - 32
    } else {
        b'?' - 32
    })
}

fn text(mut x: c_int, y: c_int, bytes: &[u8]) {
    for &ch in bytes {
        let i = glyph(ch);
        let advance = c_int::from(ADVANCE[i]);
        if i != 0 {
            sprite(
                0,
                x,
                y,
                (i as c_int % 32) * 8,
                24 + (i as c_int / 32) * 8,
                advance - 1,
                7,
            );
        }
        x += advance;
    }
}

fn text_width(bytes: &[u8]) -> c_int {
    bytes
        .iter()
        .map(|&ch| c_int::from(ADVANCE[glyph(ch)]))
        .sum()
}

/// Stops before a whole glyph would cross the panel limit.
fn clipped_text(mut x: c_int, y: c_int, bytes: &[u8], right: c_int) {
    for &ch in bytes {
        let i = glyph(ch);
        let advance = c_int::from(ADVANCE[i]);
        if x + advance > right {
            break;
        }
        if i != 0 {
            sprite(
                0,
                x,
                y,
                (i as c_int % 32) * 8,
                24 + (i as c_int / 32) * 8,
                advance - 1,
                7,
            );
        }
        x += advance;
    }
}

/// Uses scanline strips so the panel and shadow share exact diagonal edges.
fn clipped_panel(x: c_int, y: c_int, w: c_int, h: c_int, gray: c_int) {
    let top = 9;
    let bottom = 9;
    for i in 0..top {
        rect(2, x + top - i, y + i, w - top + i, 1, gray);
    }
    rect(2, x, y + top, w, h - top - bottom, gray);
    for i in 0..bottom {
        rect(2, x, y + h - bottom + i, w - i - 1, 1, gray);
    }
}

fn menu_panel(x: c_int, y: c_int, w: c_int, h: c_int) {
    clipped_panel(x, y, w, h, UI_PANEL);
    clipped_panel(x + 4, y + 4, w, h, UI_SHADOW);
}

/// Right-aligns values and leaves the underline in a shallower OT bucket.
fn menu_row(
    editor: &Editor,
    row: &EditorRow,
    index: c_int,
    x: c_int,
    y: c_int,
    width: c_int,
) {
    let mut value_bytes = [0; 48];
    let mut value = Text::new(&mut value_bytes);
    row_value(editor, row.id, &mut value);
    let right = x + width;
    let value_x = right - text_width(value.as_bytes());
    let label_right = if value.is_empty() { right } else { value_x - 6 };
    // SAFETY: Editor rows carry static labels.
    clipped_text(x, y, unsafe { cbytes(row.label) }, label_right);
    if !value.is_empty() {
        clipped_text(value_x, y, value.as_bytes(), right);
    }
    if index == editor.selected {
        rect(1, x, y + 9, width, 1, UI_INK);
    }
}

/// Keeps the labels for all menu surfaces in one place.
fn menu_title(editor: &Editor, out: &mut Text<'_>) {
    out.clear();
    match editor.mode {
        EDIT_MAIN => out.push(b"JACQUARD / MAIN"),
        EDIT_REVERB => out.push(b"REVERB / GLOBAL"),
        EDIT_SOUND => {
            write!(out, "SOUND CH {}", editor.sound_channel + 1)
                .expect("fixed text writer cannot fail");
        }
        EDIT_PATTERN => out.push(b"CYCLE PATTERN"),
        EDIT_PICKER => out.push(b"CREATE TILE"),
        EDIT_DELETE => {
            if editor.target != 0 {
                out.push(b"DELETE JUMP BRANCH?");
            } else if editor.score.lanes[editor.lane as usize].source != 0 {
                out.push(b"DELETE BRANCH LANE?");
            } else {
                out.push(b"DELETE LANE?");
            }
        }
        _ => out.push(b"MENU"),
    }
}

/// Sizes around visible text while the pattern grid retains its fixed pitch.
fn menu_width(
    editor: &Editor,
    rows: &[EditorRow; 16],
    count: usize,
    title: &[u8],
    status: &[u8],
) -> c_int {
    let mut width = if editor.mode == EDIT_PATTERN {
        232
    } else {
        text_width(title) + 2 * UI_MENU_MARGIN
    };
    if editor.mode != EDIT_PATTERN {
        let mut value_bytes = [0; 48];
        let mut value = Text::new(&mut value_bytes);
        for row in &rows[..count] {
            row_value(editor, row.id, &mut value);
            // SAFETY: Editor rows carry static labels.
            let row_width = text_width(unsafe { cbytes(row.label) })
                + text_width(value.as_bytes())
                + if value.is_empty() { 0 } else { 12 }
                + 30;
            width = width.max(row_width);
        }
        if editor.mode == EDIT_PICKER {
            for kind in 1..TILE_KIND_COUNT {
                // SAFETY: Model tile labels are static strings.
                width = width.max(
                    text_width(unsafe { cbytes(score_tile_label(kind)) }) + 32,
                );
            }
        }
        width = width.max(text_width(status) + 30);
        if !editor.message.is_null() {
            width = width.max(text_width(message(editor)) + 30);
        }
        width = width.clamp(128, 286);
    }
    width
}

/// Leaves ten pixels below the last selection underline.
fn menu_height(editor: &Editor, count: usize, alert: bool) -> c_int {
    let mut bottom = 35 + (count as c_int - 1) * UI_MENU_ROW + 10;
    if editor.mode == EDIT_PATTERN {
        bottom = 39
            + ((editor.score.tiles[usize::from(editor.target)].value.period
                - 1)
                / 8)
                * 18
            + 10;
    } else if editor.mode == EDIT_PICKER {
        bottom = 32 + (TILE_KIND_COUNT - 2) * UI_MENU_ROW + 10;
    } else if editor.mode == EDIT_DELETE {
        bottom = 11 + 7;
    }
    let mut height = bottom + 10;
    if editor.mode == EDIT_MAIN {
        height += UI_MENU_ROW;
    }
    if alert {
        height += 14;
    }
    height
}

/// Keeps one panel's origin and extent together across menu surfaces.
struct MenuLayout {
    x: c_int,
    y: c_int,
    width: c_int,
    height: c_int,
}

/// Draws the surface-specific body after the shared panel and title.
fn menu_body(
    editor: &Editor,
    rows: &[EditorRow; 16],
    count: usize,
    layout: MenuLayout,
    status: &[u8],
    alert: bool,
) {
    let MenuLayout {
        x,
        y,
        width,
        height,
    } = layout;
    if editor.mode == EDIT_PATTERN {
        let value = editor.score.tiles[usize::from(editor.target)].value;
        for i in 0..value.period {
            let gx = x + 16 + (i % 8) * 26;
            let gy = y + 39 + (i / 8) * 18;
            let digit = if value.pattern & (1_u32 << i) != 0 {
                b"1"
            } else {
                b"0"
            };
            text(gx + 8, gy, digit);
            if i == editor.pattern_cursor {
                rect(1, gx + 5, gy + 9, 13, 1, UI_INK);
            }
        }
    } else if editor.mode == EDIT_PICKER {
        for kind in 1..TILE_KIND_COUNT {
            let py = y + 32 + (kind - 1) * UI_MENU_ROW;
            clipped_text(
                x + 16,
                py,
                // SAFETY: Model tile labels are static strings.
                unsafe { cbytes(score_tile_label(kind)) },
                x + width - 16,
            );
            if kind == editor.tile_candidate {
                rect(1, x + 16, py + 9, width - 32, 1, UI_INK);
            }
        }
    } else if editor.mode != EDIT_DELETE {
        for (i, row) in rows[..count].iter().enumerate() {
            let py = y + 35 + i as c_int * UI_MENU_ROW;
            if row.kind == ROW_HEADING {
                // SAFETY: Editor rows carry static labels.
                clipped_text(
                    x + 15,
                    py,
                    unsafe { cbytes(row.label) },
                    x + width - 15,
                );
                rect(1, x + 15, py + 10, 62, 1, UI_RULE);
            } else {
                menu_row(editor, row, i as c_int, x + 15, py, width - 30);
            }
        }
        if editor.mode == EDIT_MAIN {
            clipped_text(
                x + 15,
                y + height - if alert { 31 } else { 17 },
                status,
                x + width - 15,
            );
        }
    }
    if alert {
        clipped_text(x + 15, y + height - 17, message(editor), x + width - 15);
    }
}

/// Builds all editor surfaces from the same state consumed by the grid.
fn draw_menu(editor: &Editor) {
    let empty = EditorRow {
        kind: 0,
        id: 0,
        min: 0,
        max: 0,
        fine: 0,
        coarse: 0,
        label: core::ptr::null(),
    };
    let mut menu_rows = [empty; 16];
    let count = rows(editor, &mut menu_rows);
    let mut title_bytes = [0; 40];
    let mut title = Text::new(&mut title_bytes);
    menu_title(editor, &mut title);
    let mut status_bytes = [0; 64];
    let mut status = Text::new(&mut status_bytes);
    if editor.mode == EDIT_MAIN {
        // SAFETY: Storage returns a static C string.
        unsafe { status.push_c(storage_message(editor.slot_status)) };
        if editor.card_free >= 0 {
            write!(status, "  {} BLK", editor.card_free)
                .expect("fixed text writer cannot fail");
        }
    }
    let width = menu_width(
        editor,
        &menu_rows,
        count,
        title.as_bytes(),
        status.as_bytes(),
    );
    let alert = !editor.message.is_null() && !message(editor).is_empty();
    let height = menu_height(editor, count, alert);
    let x = (SCREEN_W - width) / 2;
    let mut y = (SCREEN_H - height) / 2;
    if height > SCREEN_H {
        // Keep the selected row inside the display margins without
        // independently shifting the rest of the panel.
        let selected_bottom = 35 + editor.selected * UI_MENU_ROW + 10;
        y = UI_MENU_EDGE;
        if y + selected_bottom > SCREEN_H - UI_MENU_EDGE {
            y = SCREEN_H - UI_MENU_EDGE - selected_bottom;
        }
    }
    menu_panel(x, y, width, height);
    clipped_text(
        x + UI_MENU_MARGIN,
        y + 11,
        title.as_bytes(),
        x + width - UI_MENU_MARGIN,
    );
    menu_body(
        editor,
        &menu_rows,
        count,
        MenuLayout {
            x,
            y,
            width,
            height,
        },
        status.as_bytes(),
        alert,
    );
}

fn tile(depth: c_int, x: c_int, y: c_int, kind: c_int) {
    sprite(depth, x, y, kind * 16, 0, 16, 16);
}

/// Places brackets just outside the tile body without covering it.
fn cursor(mut x: c_int, y: c_int) {
    let width = 15;
    let height = 16;
    let arm = 4;
    x += 1;
    for j in 0..2 {
        for i in 0..2 {
            rect(
                3,
                x + i * (width - arm),
                y + j * (height - 1),
                arm,
                1,
                UI_INK,
            );
            rect(
                3,
                x + i * (width - 1),
                y + j * (height - arm),
                1,
                arm,
                UI_INK,
            );
        }
    }
}

fn clamp(value: c_int, max: c_int) -> c_int {
    value.clamp(0, max)
}

/// Keeps two cells of context around the cursor where score bounds allow.
fn follow(
    mut camera: c_int,
    cursor: c_int,
    size: c_int,
    bound: c_int,
) -> c_int {
    if cursor < camera + 2 {
        camera = cursor - 2;
    }
    if cursor >= camera + size - 2 {
        camera = cursor - size + 3;
    }
    clamp(camera, bound - size)
}

/// Centers compact atlas glyphs with the original tight spacing after `+`.
fn small(
    mut x: c_int,
    y: c_int,
    bytes: &[u8],
    dark: bool,
    shift: c_int,
    mut first_shift: c_int,
) {
    const CHARS: &[u8] = b"0123456789ABCDEFG+LR%h";
    let mut width = 0;
    let mut advance = 0;
    let mut compact = false;
    for &ch in bytes {
        advance = if ch == b'+' || (compact && ch.is_ascii_digit()) {
            3
        } else {
            4
        };
        width += advance;
        compact = ch == b'+' || (compact && ch.is_ascii_digit());
    }
    if width == 0 {
        return;
    }
    width -= advance - 3;
    let inset = (CELL_SIZE - width) / 2;
    x += inset.max(2) + shift;
    compact = false;
    for &ch in bytes {
        if let Some(i) = CHARS.iter().position(|&c| c == ch) {
            sprite(
                5,
                x + first_shift,
                y,
                i as c_int * 4,
                if dark { 56 } else { 48 },
                3,
                5,
            );
        }
        first_shift = 0;
        x += if ch == b'+' || (compact && ch.is_ascii_digit()) {
            3
        } else {
            4
        };
        compact = ch == b'+' || (compact && ch.is_ascii_digit());
    }
}

/// Maps a resolved model cell to its atlas sprite and compact label.
fn draw_cell(score: &Score, cell: Cell, x: c_int, y: c_int) {
    if cell.kind == CELL_EMPTY {
        return;
    }
    let kind = if cell.kind == CELL_HEAD {
        if score.lanes[cell.lane as usize].source != 0 {
            5
        } else {
            0
        }
    } else if cell.kind == CELL_END {
        7
    } else if cell.kind == CELL_STEP {
        8
    } else {
        let value_kind = score.tiles[usize::from(cell.tile)].value.kind;
        if value_kind == TILE_RELATIVE {
            6
        } else {
            value_kind
        }
    };
    let mut label_bytes = [0; 16];
    let mut label = Text::new(&mut label_bytes);
    let mut shift = 0;
    let mut first_shift = 0;
    if cell.kind == CELL_HEAD {
        if score.lanes[cell.lane as usize].source != 0 {
            label.push(b"B");
        } else {
            // SAFETY: A head cell always names a valid lane in this score.
            let channel = unsafe { score_channel(score, cell.lane) };
            write!(label, "Ch{}", channel + 1)
                .expect("fixed text writer cannot fail");
            shift = 1;
        }
    }
    if cell.kind == CELL_TILE {
        let value = score.tiles[usize::from(cell.tile)].value;
        match value.kind {
            TILE_NOTE => {
                // SAFETY: The model returns a static C string.
                unsafe { label.push_c(score_note_name(value.pitch)) };
                write!(label, "{}", value.pitch / 12)
                    .expect("fixed text writer cannot fail");
                // The plus and octave align; only the note letter needs a nudge.
                if label.replace(b'#', b'+') {
                    first_shift = 1;
                } else {
                    shift = 1;
                }
            }
            TILE_RELATIVE => {
                if value.lock_mask & LOCK_ATTACK != 0 {
                    label.push(b"A");
                }
                if value.lock_mask & LOCK_RELEASE != 0 {
                    label.push(b"R");
                }
            }
            TILE_CYCLE => {
                write!(label, "C{}", value.period)
                    .expect("fixed text writer cannot fail");
            }
            TILE_PROBABILITY => {
                write!(label, "{}", value.chance)
                    .expect("fixed text writer cannot fail");
            }
            _ => {}
        }
    }
    // Labels share the tile bucket so later panels cover them completely.
    small(
        x,
        y + 5,
        label.as_bytes(),
        cell.kind == CELL_HEAD,
        shift,
        first_shift,
    );
    tile(5, x, y, kind);
}

fn screen_x(x: c_int, camera_x: c_int) -> c_int {
    clamp(x - camera_x, VIEW_COLS - 1) * CELL_SIZE
}

fn screen_y(y: c_int, camera_y: c_int) -> c_int {
    clamp(y - camera_y, VIEW_ROWS - 1) * CELL_SIZE
}

fn visible(x: c_int, y: c_int, camera_x: c_int, camera_y: c_int) -> bool {
    x >= camera_x
        && x < camera_x + VIEW_COLS
        && y >= camera_y
        && y < camera_y + VIEW_ROWS
}

fn marker(x: c_int, y: c_int, gray: c_int, camera_x: c_int, camera_y: c_int) {
    outline(
        4,
        screen_x(x, camera_x) + 1,
        screen_y(y, camera_y) + 1,
        14,
        14,
        gray,
    );
}

/// Submits one complete frame without changing caller-owned editor state.
///
/// # Safety
/// `editor` must point to a valid editor that is not mutated during the call;
/// rendering and the C backend must run serially on the main thread.
#[no_mangle]
pub unsafe extern "C" fn render_frame(
    editor: *const Editor,
    _connected: c_int,
) {
    // SAFETY: The caller provides a live immutable editor for this frame.
    let editor = unsafe { &*editor };
    // SAFETY: Rendering is serialized on the main thread.
    let camera_x =
        unsafe { follow(CAMERA_X, editor.x, VIEW_COLS, SCORE_WIDTH) };
    // SAFETY: Rendering is serialized on the main thread.
    let camera_y =
        unsafe { follow(CAMERA_Y, editor.y, VIEW_ROWS, SCORE_HEIGHT) };
    // SAFETY: Rendering is serialized on the main thread.
    unsafe {
        CAMERA_X = camera_x;
        CAMERA_Y = camera_y;
        render_backend_begin();
    }
    for row in 0..VIEW_ROWS {
        for col in 0..VIEW_COLS {
            let x = col * CELL_SIZE;
            let y = row * CELL_SIZE;
            let cell = at(&editor.score, camera_x + col, camera_y + row);
            rect(7, x + 8, y + 8, 1, 1, UI_DOT);
            if cell.kind == CELL_EMPTY {
                continue;
            }
            let lane = &editor.score.lanes[cell.lane as usize];
            if cell.depth == 0 {
                for dx in (0..CELL_SIZE).step_by(4) {
                    let logical =
                        (camera_x + col - lane.x) * CELL_SIZE + dx - 8;
                    if logical >= 0 && logical <= (lane.length + 1) * CELL_SIZE
                    {
                        rect(6, x + dx, y + 8, 1, 1, UI_RAIL);
                    }
                }
            }
            if cell.depth != 0 {
                rect(6, x + 8, y, 1, 16, UI_RAIL);
            }
            draw_cell(&editor.score, cell, x, y);
        }
    }
    for lane in &editor.score.lanes {
        if lane.active == 0 {
            continue;
        }
        for step in 0..lane.length {
            let mut depth = 0;
            let mut tile_id = lane.tiles[step as usize];
            while tile_id != 0 {
                let item = &editor.score.tiles[usize::from(tile_id)];
                if item.value.kind == TILE_JUMP {
                    let branch = &editor.score.lanes[item.branch as usize];
                    let sx = lane.x + step + 1;
                    let sy = lane.y + depth;
                    if visible(sx, sy, camera_x, camera_y)
                        || visible(branch.x, branch.y, camera_x, camera_y)
                    {
                        let x1 = screen_x(sx, camera_x) + 8;
                        let y1 = screen_y(sy, camera_y) + 8;
                        let x2 = screen_x(branch.x, camera_x) + 8;
                        let y2 = screen_y(branch.y, camera_y) + 8;
                        rect(
                            6,
                            x1.min(x2),
                            y1,
                            (x1 - x2).abs() + 1,
                            1,
                            UI_BORDER,
                        );
                        rect(
                            6,
                            x2,
                            y1.min(y2),
                            1,
                            (y1 - y2).abs() + 1,
                            UI_BORDER,
                        );
                        if !visible(branch.x, branch.y, camera_x, camera_y) {
                            marker(
                                branch.x, branch.y, UI_INK, camera_x, camera_y,
                            );
                        }
                        if !visible(sx, sy, camera_x, camera_y) {
                            marker(sx, sy, UI_INK, camera_x, camera_y);
                        }
                    }
                }
                tile_id = item.next;
                depth += 1;
            }
        }
    }
    if editor.mode == EDIT_MOVE {
        let mut plan = MovePlan {
            sx: 0,
            sy: 0,
            x: 0,
            y: 0,
            result: 0,
        };
        // SAFETY: The score and output plan are distinct valid objects.
        unsafe {
            score_plan_move(
                &editor.score,
                editor.source_x,
                editor.source_y,
                editor.x,
                editor.y,
                &mut plan,
            );
        }
        let source = at(&editor.score, editor.source_x, editor.source_y);
        let gray = if plan.result == 0 { UI_INK } else { UI_RAIL };
        marker(
            editor.source_x,
            editor.source_y,
            UI_BORDER,
            camera_x,
            camera_y,
        );
        for row in 0..VIEW_ROWS {
            for col in 0..VIEW_COLS {
                let x = camera_x + col;
                let y = camera_y + row;
                let cell = at(
                    &editor.score,
                    x - editor.x + editor.source_x,
                    y - editor.y + editor.source_y,
                );
                let mut carried = if source.kind == CELL_HEAD {
                    cell.lane == source.lane
                } else {
                    cell.kind == CELL_TILE
                        && cell.lane == source.lane
                        && cell.step == source.step
                        && cell.depth >= source.depth
                };
                let dest = resolve(&editor.score, editor.x, editor.y);
                if source.kind == CELL_TILE
                    && dest.lane == source.lane
                    && dest.step == source.step
                {
                    carried = x == editor.x && y == editor.y;
                }
                if carried {
                    outline(4, col * 16 + 2, row * 16 + 2, 12, 12, gray);
                }
            }
        }
    }
    cursor(screen_x(editor.x, camera_x), screen_y(editor.y, camera_y));
    if editor.mode != EDIT_PLANE && editor.mode != EDIT_MOVE {
        draw_menu(editor);
    }
    // SAFETY: This is the same open frame begun above, and the backend
    // finalizes it before any other frame can start.
    unsafe { render_backend_present() };
}
