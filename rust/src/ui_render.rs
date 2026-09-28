//! Draws the grid and editor menus from caller-owned state.
//!
//! Layout, clipping, text, and camera state live here. The C backend owns
//! PlayStation GPU packets and submits complete frames on the main thread.

// Implementation notes:
// OT depths reverse insertion order within each bucket. Preserve the C
// renderer's draw order, including tile labels before their body sprites.

use core::ffi::{c_char, c_int, CStr};
use core::fmt::Write;

use crate::audio_transport::AudioPlayheads;
use crate::editor::{rows, Editor, EditorRow, TILE_PICKER_ORDER};
use crate::score::{
    at, resolve, score_channel, score_note_name, score_tile_label, Cell, Score,
    TILE_CAPACITY,
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
const LOGO_W: c_int = 72;
const LOGO_H: c_int = 15;
const EDIT_PLANE: c_int = 0;
const EDIT_MENU: c_int = 1;
const EDIT_DELETE: c_int = 2;
const EDIT_PICKER: c_int = 3;
const EDIT_PATTERN: c_int = 4;
const EDIT_MOVE: c_int = 5;
const EDIT_SOUND: c_int = 6;
const EDIT_MAIN: c_int = 7;
const EDIT_REVERB: c_int = 8;
const EDIT_CARD: c_int = 9;
const EDIT_LOCK: c_int = 10;
const ROW_HEADING: c_int = 0;
const TILE_NOTE: c_int = 1;
const TILE_CYCLE: c_int = 2;
const TILE_PROBABILITY: c_int = 3;
const TILE_JUMP: c_int = 4;
const TILE_RELATIVE: c_int = 5;
const CELL_EMPTY: c_int = 0;
const CELL_HEAD: c_int = 1;
const CELL_STEP: c_int = 2;
const CELL_TILE: c_int = 3;
const CELL_END: c_int = 4;
const LOCK_ATTACK: c_int = 1;
const LOCK_RELEASE: c_int = 2;

#[cfg(all(target_arch = "mips", debug_assertions))]
const MONITOR_HISTORY: usize = 48;
#[cfg(all(target_arch = "mips", debug_assertions))]
const MONITOR_BOTTOM: c_int = SCREEN_H;
#[cfg(all(target_arch = "mips", debug_assertions))]
const MONITOR_TOP: c_int = MONITOR_BOTTOM - UI_MENU_EDGE;

/// Mirrors the C backend's completed-frame diagnostics.
#[cfg(all(target_arch = "mips", debug_assertions))]
#[repr(C)]
struct RenderMonitor {
    history: [u32; MONITOR_HISTORY],
    history_next: u32,
    history_count: u32,
    frame_ticks: u32,
    work_ticks: u32,
    work_peak_percent: u32,
    audio_dispatch_peak: u32,
    audio_skipped_notes: u32,
    audio_queue_underruns: u32,
    audio_overloads: u32,
}

// The renderer is main-thread only, like the C packet backend.
static mut CAMERA_X: c_int = 0;
static mut CAMERA_Y: c_int = 0;
static mut MENU_Y: c_int = UI_MENU_EDGE;
static mut MENU_MODE: c_int = EDIT_PLANE;
const EMPTY_CELL: Cell = Cell {
    kind: CELL_EMPTY,
    lane: -1,
    step: -1,
    depth: 0,
    tile: 0,
};
/// Main-thread scratch storage avoids allocating 300 cells on the stack.
static mut VISIBLE_CELLS: [Cell; (VIEW_COLS * VIEW_ROWS) as usize] =
    [EMPTY_CELL; (VIEW_COLS * VIEW_ROWS) as usize];

/// Admitted score coordinates and branch indices fit in one byte each.
#[derive(Clone, Copy)]
struct JumpVisual {
    x: u8,
    y: u8,
    branch: u8,
}

/// Invalidates cached jump origins when the edited score revision changes.
struct JumpCache {
    score: *const Score,
    revision: u32,
    count: usize,
    items: [JumpVisual; TILE_CAPACITY],
}

/// The renderer alone accesses this cache on the main thread.
static mut JUMP_CACHE: JumpCache = JumpCache {
    score: core::ptr::null(),
    revision: 0,
    count: 0,
    items: [JumpVisual {
        x: 0,
        y: 0,
        branch: 0,
    }; TILE_CAPACITY],
};

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
    #[cfg(all(target_arch = "mips", debug_assertions))]
    fn render_backend_monitor() -> *const RenderMonitor;
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

#[cfg(all(target_arch = "mips", debug_assertions))]
fn percent(ticks: u32, budget: u32) -> u32 {
    ((u64::from(ticks) * 100) / u64::from(budget.max(1))) as u32
}

/// Draws a monitor primitive in the reserved bottom band.
#[cfg(all(target_arch = "mips", debug_assertions))]
fn monitor_tile(
    depth: c_int,
    x: c_int,
    y: c_int,
    w: c_int,
    h: c_int,
    gray: c_int,
) {
    // SAFETY: Every monitor primitive has fixed geometry inside the frame.
    unsafe { render_backend_tile(depth, x, y, w, h, gray) };
}

/// Draws glyphs without the menu renderer's protected-margin clipping.
#[cfg(all(target_arch = "mips", debug_assertions))]
fn monitor_text(mut x: c_int, y: c_int, bytes: &[u8], right: c_int) {
    for &ch in bytes {
        let i = glyph(ch);
        let advance = c_int::from(ADVANCE[i]);
        if x + advance > right {
            break;
        }
        if i != 0 {
            // SAFETY: Fixed band positions and the atlas glyph table keep
            // both destination and source inside their respective bounds.
            unsafe {
                render_backend_sprite(
                    0,
                    x,
                    y,
                    (i as c_int % 32) * 8,
                    24 + (i as c_int / 32) * 8,
                    advance - 1,
                    7,
                )
            };
        }
        x += advance;
    }
}

/// Draws the last frame's diagnostics in the bottom 16-pixel band.
#[cfg(all(target_arch = "mips", debug_assertions))]
fn draw_monitor() {
    // SAFETY: The C backend owns static storage and frame rendering is serial.
    let monitor = unsafe { &*render_backend_monitor() };
    let budget = monitor.frame_ticks.max(1);
    let cpu = percent(monitor.work_ticks, budget);
    let peak = monitor.work_peak_percent;
    let audio = percent(monitor.audio_dispatch_peak, 4233);

    // Draw first: ordering-table buckets reverse insertion order, so the
    // monitor stays above the editor's panels and labels.
    monitor_tile(1, 0, MONITOR_TOP, SCREEN_W, UI_MENU_EDGE, UI_PANEL);
    monitor_tile(0, 172, MONITOR_TOP + 6, 144, 1, UI_RULE);
    for i in 0..MONITOR_HISTORY {
        let index = (monitor.history_next as usize + i) % MONITOR_HISTORY;
        if i + (monitor.history_count as usize) < MONITOR_HISTORY {
            continue;
        }
        let ticks = monitor.history[index];
        let height = ((u64::from(ticks) * 14 * 100) / (u64::from(budget) * 150))
            .min(14) as c_int;
        if height > 0 {
            monitor_tile(
                0,
                172 + i as c_int * 3,
                MONITOR_BOTTOM - 1 - height,
                2,
                height,
                UI_INK,
            );
        }
    }

    let mut bytes = [0; 48];
    let mut label = Text::new(&mut bytes);
    let _ = write!(label, "CPU {}% P {}%", cpu, peak);
    // Leave the last scanline beneath the glyphs inside the panel.
    monitor_text(4, MONITOR_TOP, label.as_bytes(), 168);

    label.clear();
    let _ = write!(
        label,
        "AUD {}% SK {} U {} O {}",
        audio,
        monitor.audio_skipped_notes,
        monitor.audio_queue_underruns,
        monitor.audio_overloads
    );
    monitor_text(4, MONITOR_TOP + 8, label.as_bytes(), 168);
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
        EDIT_MAIN => {}
        EDIT_REVERB => out.push(b"REVERB / GLOBAL"),
        EDIT_CARD => out.push(b"MEMORY CARD"),
        EDIT_SOUND => {
            write!(out, "SOUND CH {}", editor.sound_channel + 1)
                .expect("fixed text writer cannot fail");
        }
        EDIT_LOCK => out.push(b"RELATIVE LOCK / X TOGGLE"),
        EDIT_PATTERN => out.push(b"CYCLE PATTERN"),
        EDIT_PICKER => out.push(b"CREATE TILE"),
        EDIT_MENU => {
            let cell = resolve(&editor.score, editor.x, editor.y);
            match cell.kind {
                CELL_EMPTY => out.push(b"GROUND"),
                CELL_HEAD => {
                    if editor.score.lanes[cell.lane as usize].source == 0 {
                        out.push(b"LANE");
                    } else {
                        out.push(b"BRANCH LANE");
                    }
                }
                CELL_STEP | CELL_END => out.push(b"EMPTY STEP"),
                CELL_TILE => {
                    let kind =
                        editor.score.tiles[usize::from(cell.tile)].value.kind;
                    // SAFETY: Model tile labels are static strings.
                    unsafe { out.push_c(score_tile_label(kind)) };
                }
                _ => out.push(b"CELL"),
            }
        }
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
    rows: &[EditorRow; 20],
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
            for &kind in &TILE_PICKER_ORDER {
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
        bottom = 32 + (TILE_PICKER_ORDER.len() as c_int - 1) * UI_MENU_ROW + 10;
    } else if editor.mode == EDIT_DELETE {
        bottom = 11 + 7;
    }
    let mut height = bottom + 10;
    if editor.mode == EDIT_CARD {
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
    rows: &[EditorRow; 20],
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
        for (index, &kind) in TILE_PICKER_ORDER.iter().enumerate() {
            let py = y + 32 + index as c_int * UI_MENU_ROW;
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
        if editor.mode == EDIT_CARD {
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
    let mut menu_rows = [empty; 20];
    let count = rows(editor, &mut menu_rows);
    let mut title_bytes = [0; 40];
    let mut title = Text::new(&mut title_bytes);
    menu_title(editor, &mut title);
    let mut status_bytes = [0; 64];
    let mut status = Text::new(&mut status_bytes);
    if editor.mode == EDIT_CARD {
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
        // The panel follows the selection near the bottom, but holds its
        // position on the way back until the selection reaches the top rows.
        let first_row = menu_rows[..count]
            .iter()
            .position(|row| row.kind != ROW_HEADING)
            .unwrap_or(0) as c_int;
        let top = UI_MENU_EDGE + 35 + first_row * UI_MENU_ROW;
        let bottom = SCREEN_H - UI_MENU_EDGE - 2 * UI_MENU_ROW;
        let selected_top = 35 + editor.selected * UI_MENU_ROW;
        let selected_bottom = selected_top + 10;
        let last_bottom = 35 + (count as c_int - 1) * UI_MENU_ROW + 10;
        let min_y = SCREEN_H - UI_MENU_EDGE - last_bottom;
        // SAFETY: Rendering is serialized on the main thread.
        unsafe {
            if MENU_MODE != editor.mode {
                MENU_Y = UI_MENU_EDGE;
            }
            MENU_MODE = editor.mode;
            y = MENU_Y;
            if y + selected_bottom > bottom {
                y = bottom - selected_bottom;
            }
            if y + selected_top < top {
                y = top - selected_top;
            }
            y = y.clamp(min_y, UI_MENU_EDGE);
            MENU_Y = y;
        }
    } else {
        // SAFETY: Rendering is serialized on the main thread.
        unsafe { MENU_MODE = editor.mode };
    }
    menu_panel(x, y, width, height);
    if editor.mode == EDIT_MAIN {
        // The atlas keeps the wordmark's 60-unit cells as single pixels.
        sprite(0, x + UI_MENU_MARGIN, y + 11, 0, 64, LOGO_W, LOGO_H);
    } else {
        clipped_text(
            x + UI_MENU_MARGIN,
            y + 11,
            title.as_bytes(),
            x + width - UI_MENU_MARGIN,
        );
    }
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

/// Avoids the general formatter for the short integer labels redrawn in
/// every visible cell. The scratch digits cover the full `c_int` range.
fn push_decimal(label: &mut Text<'_>, value: c_int) {
    if value < 0 {
        label.push(b"-");
    }
    let mut number = value.unsigned_abs();
    let mut digits = [0; 10];
    let mut start = digits.len();
    loop {
        start -= 1;
        digits[start] = b'0' + (number % 10) as u8;
        number /= 10;
        if number == 0 {
            break;
        }
    }
    label.push(&digits[start..]);
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
            label.push(b"Ch");
            push_decimal(&mut label, channel + 1);
            shift = 1;
        }
    }
    if cell.kind == CELL_TILE {
        let value = score.tiles[usize::from(cell.tile)].value;
        match value.kind {
            TILE_NOTE => {
                // SAFETY: The model returns a static C string.
                unsafe { label.push_c(score_note_name(value.pitch)) };
                push_decimal(&mut label, value.pitch / 12);
                // The plus and octave align; only the note letter needs a nudge.
                if label.replace(b'#', b'+') {
                    first_shift = 1;
                } else {
                    shift = 1;
                }
            }
            TILE_RELATIVE => {
                if value.lock_mask & !3 == 0 {
                    if value.lock_mask & LOCK_ATTACK != 0 {
                        label.push(b"A");
                    }
                    if value.lock_mask & LOCK_RELEASE != 0 {
                        label.push(b"R");
                    }
                } else {
                    push_decimal(
                        &mut label,
                        value.lock_mask.count_ones() as c_int,
                    );
                }
            }
            TILE_CYCLE => {
                label.push(b"C");
                push_decimal(&mut label, value.period);
            }
            TILE_PROBABILITY => {
                push_decimal(&mut label, value.chance);
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

/// Draws each audible step in the two-pixel gutter beside its tile stack.
fn draw_playheads(
    editor: &Editor,
    playheads: &AudioPlayheads,
    camera_x: c_int,
    camera_y: c_int,
) {
    // During publication the editable geometry may lead the audio snapshot.
    // Waiting for matching revisions keeps a moved lane from showing a bar
    // at a position that is not yet sounding.
    if playheads.revision != editor.score.revision {
        return;
    }
    for head in &playheads.items[..playheads.count as usize] {
        if head.lane < 0 || head.lane as usize >= editor.score.lanes.len() {
            continue;
        }
        let lane = &editor.score.lanes[head.lane as usize];
        if lane.active == 0 || head.step < 0 || head.step >= lane.length {
            continue;
        }
        let mut depth = 0;
        let mut tile_id = lane.tiles[head.step as usize];
        while tile_id != 0 {
            depth += 1;
            tile_id = editor.score.tiles[usize::from(tile_id)].next;
        }
        let x = (lane.x + head.step + 1 - camera_x) * CELL_SIZE;
        let y = (lane.y - camera_y) * CELL_SIZE;
        rect(3, x, y + 1, 2, depth.max(1) * CELL_SIZE - 2, UI_INK);
    }
}

/// Keeps the first model cell at a visible coordinate, matching `at()`'s
/// lane-order precedence without querying the same stack at every depth.
fn place_visible(
    cells: &mut [Cell; (VIEW_COLS * VIEW_ROWS) as usize],
    camera_x: c_int,
    camera_y: c_int,
    x: c_int,
    y: c_int,
    cell: Cell,
) {
    let col = x - camera_x;
    let row = y - camera_y;
    if !(0..VIEW_COLS).contains(&col) || !(0..VIEW_ROWS).contains(&row) {
        return;
    }
    let slot = &mut cells[(row * VIEW_COLS + col) as usize];
    if slot.kind == CELL_EMPTY {
        *slot = cell;
    }
}

/// Resolves the visible plane in lane order while walking each tile link at
/// most once per visible step. Static storage avoids a large main-stack frame.
fn fill_visible(
    score: &Score,
    camera_x: c_int,
    camera_y: c_int,
    cells: &mut [Cell; (VIEW_COLS * VIEW_ROWS) as usize],
) {
    cells.fill(EMPTY_CELL);
    for (index, lane) in score.lanes.iter().enumerate() {
        if lane.active == 0
            || lane.y >= camera_y + VIEW_ROWS
            || lane.x >= camera_x + VIEW_COLS
            || lane.x + lane.length + 1 < camera_x
        {
            continue;
        }
        let lane_id = index as c_int;
        place_visible(
            cells,
            camera_x,
            camera_y,
            lane.x,
            lane.y,
            Cell {
                kind: CELL_HEAD,
                lane: lane_id,
                step: -1,
                depth: 0,
                tile: 0,
            },
        );
        place_visible(
            cells,
            camera_x,
            camera_y,
            lane.x + lane.length + 1,
            lane.y,
            Cell {
                kind: CELL_END,
                lane: lane_id,
                step: lane.length,
                depth: 0,
                tile: 0,
            },
        );
        for step in 0..lane.length {
            let x = lane.x + step + 1;
            if x < camera_x || x >= camera_x + VIEW_COLS {
                continue;
            }
            let mut tile_id = lane.tiles[step as usize];
            if tile_id == 0 {
                place_visible(
                    cells,
                    camera_x,
                    camera_y,
                    x,
                    lane.y,
                    Cell {
                        kind: CELL_STEP,
                        lane: lane_id,
                        step,
                        depth: 0,
                        tile: 0,
                    },
                );
            }
            let mut depth = 0;
            while tile_id != 0 && lane.y + depth < camera_y + VIEW_ROWS {
                place_visible(
                    cells,
                    camera_x,
                    camera_y,
                    x,
                    lane.y + depth,
                    Cell {
                        kind: CELL_TILE,
                        lane: lane_id,
                        step,
                        depth,
                        tile: tile_id,
                    },
                );
                tile_id = score.tiles[usize::from(tile_id)].next;
                depth += 1;
            }
        }
    }
}

/// Retains jump origins across frames so a static score does not require a
/// second walk through every tile. Admitted edits change the score revision.
fn refresh_jumps(score: &Score, cache: &mut JumpCache) {
    if cache.score == core::ptr::from_ref(score)
        && cache.revision == score.revision
    {
        return;
    }
    cache.count = 0;
    for lane in &score.lanes {
        if lane.active == 0 {
            continue;
        }
        for step in 0..lane.length {
            let mut depth = 0;
            let mut tile_id = lane.tiles[step as usize];
            while tile_id != 0 {
                let item = &score.tiles[usize::from(tile_id)];
                if item.value.kind == TILE_JUMP {
                    cache.items[cache.count] = JumpVisual {
                        x: (lane.x + step + 1) as u8,
                        y: (lane.y + depth) as u8,
                        branch: item.branch as u8,
                    };
                    cache.count += 1;
                }
                tile_id = item.next;
                depth += 1;
            }
        }
    }
    cache.score = core::ptr::from_ref(score);
    cache.revision = score.revision;
}

/// Submits one complete frame without changing caller-owned editor state.
///
/// # Safety
/// `editor` must point to a valid editor that is not mutated during the call;
/// `playheads` must be null or point to a stable snapshot. Rendering and the
/// C backend must run serially on the main thread.
#[no_mangle]
pub unsafe extern "C" fn render_frame(
    editor: *const Editor,
    _connected: c_int,
    playheads: *const AudioPlayheads,
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
    #[cfg(all(target_arch = "mips", debug_assertions))]
    draw_monitor();
    // SAFETY: Rendering is serialized on the main thread, and no callback
    // reads or writes the scratch plane during this frame.
    let cells = unsafe { &mut *core::ptr::addr_of_mut!(VISIBLE_CELLS) };
    fill_visible(&editor.score, camera_x, camera_y, cells);
    for row in 0..VIEW_ROWS {
        for col in 0..VIEW_COLS {
            let x = col * CELL_SIZE;
            let y = row * CELL_SIZE;
            let cell = cells[(row * VIEW_COLS + col) as usize];
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
    // SAFETY: The main thread alone renders and mutates this scratch cache.
    let jumps = unsafe { &mut *core::ptr::addr_of_mut!(JUMP_CACHE) };
    refresh_jumps(&editor.score, jumps);
    for jump in &jumps.items[..jumps.count] {
        let branch = &editor.score.lanes[usize::from(jump.branch)];
        let sx = c_int::from(jump.x);
        let sy = c_int::from(jump.y);
        if visible(sx, sy, camera_x, camera_y)
            || visible(branch.x, branch.y, camera_x, camera_y)
        {
            let x1 = screen_x(sx, camera_x) + 8;
            let y1 = screen_y(sy, camera_y) + 8;
            let x2 = screen_x(branch.x, camera_x) + 8;
            let y2 = screen_y(branch.y, camera_y) + 8;
            rect(6, x1.min(x2), y1, (x1 - x2).abs() + 1, 1, UI_BORDER);
            rect(6, x2, y1.min(y2), 1, (y1 - y2).abs() + 1, UI_BORDER);
            if !visible(branch.x, branch.y, camera_x, camera_y) {
                marker(branch.x, branch.y, UI_INK, camera_x, camera_y);
            }
            if !visible(sx, sy, camera_x, camera_y) {
                marker(sx, sy, UI_INK, camera_x, camera_y);
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
    if !playheads.is_null() {
        // SAFETY: The caller keeps this frame's snapshot stable until return.
        let playheads = unsafe { &*playheads };
        draw_playheads(editor, playheads, camera_x, camera_y);
    }
    cursor(screen_x(editor.x, camera_x), screen_y(editor.y, camera_y));
    if editor.mode != EDIT_PLANE && editor.mode != EDIT_MOVE {
        draw_menu(editor);
    } else {
        // SAFETY: Rendering is serialized on the main thread.
        unsafe { MENU_MODE = EDIT_PLANE };
    }
    // SAFETY: This is the same open frame begun above, and the backend
    // finalizes it before any other frame can start.
    unsafe { render_backend_present() };
}
