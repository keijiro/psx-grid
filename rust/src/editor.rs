//! Converts normalized controller frames into score-editor actions.
//!
//! The caller owns fixed editor state. Updates run on the main thread and
//! retain menu, gesture, clipboard, and storage-request state without allocation.

// Implementation notes:
// The Rust renderer reads the shared Editor and EditorRow layouts directly;
// interaction rules stay here.

use core::ffi::{c_char, c_int};
use core::ptr;

use crate::input::InputFrame;
use crate::score::{
    at, default_value, resolve, score_channel, score_division, score_divisions,
    score_edit, score_message, score_set_bpm, score_set_channel,
    score_set_division, score_set_reverb, score_set_sound, Score, TileValue,
};
use crate::score_edit::{
    score_apply_move, score_copy, score_create, score_delete, score_paste,
    score_place_value, score_plan_move, score_remove, score_resize, Clipboard,
    MovePlan,
};
use crate::score_format::measure;

const SCORE_FILE_BYTES: c_int = 8192;
const SCORE_WIDTH: c_int = 128;
const SCORE_HEIGHT: c_int = 64;
const EDITOR_MENU_ITEMS: usize = 8;
const EDITOR_ROWS: usize = 16;

const EDIT_PLANE: c_int = 0;
const EDIT_MENU: c_int = 1;
const EDIT_DELETE: c_int = 2;
const EDIT_PICKER: c_int = 3;
const EDIT_PATTERN: c_int = 4;
const EDIT_MOVE: c_int = 5;
const EDIT_SOUND: c_int = 6;
const EDIT_MAIN: c_int = 7;
const EDIT_REVERB: c_int = 8;

const ACTION_CREATE: c_int = 0;
const ACTION_PLACE: c_int = 1;
const ACTION_REMOVE: c_int = 2;
const ACTION_LENGTH: c_int = 3;
const ACTION_DELETE: c_int = 4;
const ACTION_COPY: c_int = 5;
const ACTION_PASTE: c_int = 6;
const ACTION_PITCH: c_int = 7;
const ACTION_DURATION: c_int = 8;
const ACTION_PERIOD: c_int = 9;
const ACTION_PATTERN: c_int = 10;
const ACTION_CHANCE: c_int = 11;
const ACTION_DIVISION: c_int = 12;
const ACTION_SOUND: c_int = 13;
const ACTION_CHANNEL: c_int = 14;
const ACTION_LOCK_ATTACK_ENABLE: c_int = 15;
const ACTION_LOCK_RELEASE_ENABLE: c_int = 16;
const ACTION_LOCK_ATTACK: c_int = 17;
const ACTION_LOCK_RELEASE: c_int = 18;

const ROW_HEADING: c_int = 0;
const ROW_VALUE: c_int = 1;
const ROW_SUBMENU: c_int = 2;
const ROW_ACTION: c_int = 3;

const ROW_BPM: c_int = 100;
const ROW_REVERB: c_int = 101;
const ROW_SLOT: c_int = 102;
const ROW_CHECK: c_int = 103;
const ROW_SAVE: c_int = 104;
const ROW_LOAD: c_int = 105;
const ROW_SIZE: c_int = 106;
const ROW_AMOUNT: c_int = 107;
const ROW_WAVE1: c_int = 200;
const ROW_WAVE2: c_int = 201;
const ROW_AMP_ATTACK: c_int = 202;
const ROW_AMP_RELEASE: c_int = 203;
const ROW_MIX_ATTACK: c_int = 204;
const ROW_MIX_RELEASE: c_int = 205;
const ROW_SWEEP: c_int = 206;
const ROW_DECAY: c_int = 207;
const ROW_SEND: c_int = 208;

const STORAGE_ACTION_CHECK: c_int = 1;
const STORAGE_ACTION_SAVE: c_int = 2;
const STORAGE_ACTION_LOAD: c_int = 3;

const TILE_NOTE: c_int = 1;
const TILE_CYCLE: c_int = 2;
const TILE_PROBABILITY: c_int = 3;
const TILE_JUMP: c_int = 4;
const TILE_RELATIVE: c_int = 5;

#[repr(C)]
#[derive(Clone, Copy)]
/// Presents one fixed menu row through the shared C interface.
pub struct EditorRow {
    pub(crate) kind: c_int,
    pub(crate) id: c_int,
    pub(crate) min: c_int,
    pub(crate) max: c_int,
    pub(crate) fine: c_int,
    pub(crate) coarse: c_int,
    pub(crate) label: *const c_char,
}

#[repr(C)]
/// Holds the caller-owned score and persistent interaction state.
pub struct Editor {
    pub(crate) score: Score,
    pub(crate) playing: c_int,
    pub(crate) snapshot_dirty: c_int,
    pub(crate) clipboard: Clipboard,
    pub(crate) x: c_int,
    pub(crate) y: c_int,
    pub(crate) selected: c_int,
    pub(crate) lane: c_int,
    pub(crate) parent_selected: c_int,
    pub(crate) sound_channel: c_int,
    pub(crate) gesture: c_int,
    pub(crate) directed: c_int,
    pub(crate) source_x: c_int,
    pub(crate) source_y: c_int,
    pub(crate) pattern_cursor: c_int,
    pub(crate) target: u16,
    pub(crate) value: TileValue,
    pub(crate) last_note: TileValue,
    pub(crate) tile_candidate: c_int,
    pub(crate) last_tile: c_int,
    pub(crate) mode: c_int,
    pub(crate) message: *const c_char,
    pub(crate) storage_slot: c_int,
    pub(crate) storage_request: c_int,
    pub(crate) load_busy: c_int,
    pub(crate) load_slot: c_int,
    pub(crate) card_free: c_int,
    pub(crate) slot_status: c_int,
    pub(crate) capacity_revision: u32,
    pub(crate) free_bytes: c_int,
}

#[cfg(target_pointer_width = "64")]
const _: () = {
    assert!(core::mem::size_of::<EditorRow>() == 32);
    assert!(core::mem::size_of::<Editor>() == 202016);
    assert!(core::mem::offset_of!(Editor, target) == 201884);
    assert!(core::mem::offset_of!(Editor, message) == 201976);
};

#[cfg(target_pointer_width = "32")]
const _: () = {
    assert!(core::mem::size_of::<EditorRow>() == 28);
    assert!(core::mem::size_of::<Editor>() == 202008);
    assert!(core::mem::offset_of!(Editor, target) == 201884);
    assert!(core::mem::offset_of!(Editor, message) == 201972);
};

fn text(value: &'static [u8]) -> *const c_char {
    value.as_ptr().cast()
}

fn clamp(value: c_int, min: c_int, max: c_int) -> c_int {
    value.max(min).min(max)
}

fn refresh_capacity(editor: &mut Editor) {
    if editor.capacity_revision == editor.score.revision {
        return;
    }
    editor.capacity_revision = editor.score.revision;
    editor.free_bytes = SCORE_FILE_BYTES - measure(&editor.score) as c_int;
}

/// Initializes editor state and its embedded empty score.
///
/// # Safety
/// `editor` must point to a valid exclusively writable C `Editor`.
#[no_mangle]
pub unsafe extern "C" fn editor_init(editor: *mut Editor) {
    // SAFETY: The C caller owns the entire writable editor allocation.
    unsafe { ptr::write_bytes(editor, 0, 1) };
    // SAFETY: Zeroed storage is a valid C Editor before initialization.
    let editor = unsafe { &mut *editor };
    // SAFETY: The embedded score is exclusively writable.
    unsafe { crate::score::score_init(&mut editor.score) };
    editor.x = 1;
    editor.y = 1;
    editor.message = text(b"\0");
    editor.storage_slot = 1;
    editor.card_free = -1;
    editor.free_bytes = SCORE_FILE_BYTES - measure(&editor.score) as c_int;
    editor.last_tile = TILE_NOTE;
    editor.tile_candidate = TILE_NOTE;
    editor.last_note = default_value(TILE_NOTE);
}

/// Recomputes save capacity after a committed score revision.
///
/// # Safety
/// `editor` must point to a valid exclusively writable C `Editor`.
#[no_mangle]
pub unsafe extern "C" fn editor_refresh_capacity(editor: *mut Editor) {
    // SAFETY: The C caller supplies exclusive access to valid editor state.
    refresh_capacity(unsafe { &mut *editor });
}

fn add_action(
    actions: &mut [c_int; EDITOR_MENU_ITEMS],
    n: &mut usize,
    action: c_int,
) {
    actions[*n] = action;
    *n += 1;
}

fn menu(editor: &Editor, actions: &mut [c_int; EDITOR_MENU_ITEMS]) -> usize {
    let cell = resolve(&editor.score, editor.x, editor.y);
    let mut n = 0;
    if cell.kind == 0 {
        add_action(actions, &mut n, ACTION_CREATE);
    }
    if cell.kind == 2 || cell.kind == 4 {
        add_action(actions, &mut n, ACTION_PLACE);
        add_action(actions, &mut n, ACTION_PASTE);
    }
    if cell.kind == 3 {
        let kind = editor.score.tiles[usize::from(cell.tile)].value.kind;
        if kind == TILE_NOTE {
            add_action(actions, &mut n, ACTION_PITCH);
            add_action(actions, &mut n, ACTION_DURATION);
        }
        if kind == TILE_CYCLE {
            add_action(actions, &mut n, ACTION_PERIOD);
            add_action(actions, &mut n, ACTION_PATTERN);
        }
        if kind == TILE_RELATIVE {
            add_action(actions, &mut n, ACTION_LOCK_ATTACK_ENABLE);
            add_action(actions, &mut n, ACTION_LOCK_ATTACK);
            add_action(actions, &mut n, ACTION_LOCK_RELEASE_ENABLE);
            add_action(actions, &mut n, ACTION_LOCK_RELEASE);
        }
        if kind == TILE_PROBABILITY {
            add_action(actions, &mut n, ACTION_CHANCE);
        }
        add_action(actions, &mut n, ACTION_COPY);
        add_action(actions, &mut n, ACTION_REMOVE);
    }
    if cell.kind == 1 {
        add_action(actions, &mut n, ACTION_LENGTH);
        if editor.score.lanes[cell.lane as usize].source == 0 {
            add_action(actions, &mut n, ACTION_DIVISION);
            add_action(actions, &mut n, ACTION_CHANNEL);
            add_action(actions, &mut n, ACTION_SOUND);
        }
        add_action(actions, &mut n, ACTION_DELETE);
    }
    n
}

/// Writes context actions without changing unused array entries.
///
/// # Safety
/// `editor` and `items` must point to distinct valid C objects; `items`
/// must hold at least `EDITOR_MENU_ITEMS` writable integers.
#[no_mangle]
pub unsafe extern "C" fn editor_menu(
    editor: *const Editor,
    items: *mut c_int,
) -> c_int {
    // SAFETY: The C caller supplies a valid immutable editor.
    let editor = unsafe { &*editor };
    // SAFETY: The C caller supplies eight distinct writable action slots.
    let items = unsafe { &mut *(items as *mut [c_int; EDITOR_MENU_ITEMS]) };
    menu(editor, items) as c_int
}

const ACTION_LABELS: [&[u8]; 19] = [
    b"NEW LANE\0",
    b"CREATE TILE\0",
    b"DELETE TILE\0",
    b"LENGTH\0",
    b"DELETE LANE\0",
    b"COPY STACK\0",
    b"PASTE STACK\0",
    b"PITCH\0",
    b"NOTE LENGTH\0",
    b"PERIOD\0",
    b"PATTERN\0",
    b"CHANCE\0",
    b"DIVISION\0",
    b"SOUND\0",
    b"CHANNEL\0",
    b"ATTACK ENABLE\0",
    b"RELEASE ENABLE\0",
    b"ATTACK OFFSET\0",
    b"RELEASE OFFSET\0",
];

/// Returns the fixed display label for a valid context action.
#[no_mangle]
pub extern "C" fn editor_action_label(action: c_int) -> *const c_char {
    ACTION_LABELS
        .get(action as usize)
        .copied()
        .unwrap_or(b"?\0")
        .as_ptr()
        .cast()
}

fn row(
    kind: c_int,
    id: c_int,
    label: &'static [u8],
    min: c_int,
    max: c_int,
    fine: c_int,
    coarse: c_int,
) -> EditorRow {
    EditorRow {
        kind,
        id,
        min,
        max,
        fine,
        coarse,
        label: text(label),
    }
}

fn push(rows: &mut [EditorRow; EDITOR_ROWS], n: &mut usize, value: EditorRow) {
    rows[*n] = value;
    *n += 1;
}

pub(crate) fn rows(
    editor: &Editor,
    output: &mut [EditorRow; EDITOR_ROWS],
) -> usize {
    let mut n = 0;
    if editor.mode == EDIT_MAIN {
        push(
            output,
            &mut n,
            row(ROW_VALUE, ROW_BPM, b"BPM\0", 30, 300, 1, 10),
        );
        push(
            output,
            &mut n,
            row(ROW_SUBMENU, ROW_REVERB, b"REVERB\0", 0, 0, 0, 0),
        );
        push(
            output,
            &mut n,
            row(ROW_VALUE, ROW_SLOT, b"SLOT\0", 1, 15, 1, 1),
        );
        push(
            output,
            &mut n,
            row(ROW_ACTION, ROW_CHECK, b"CHECK CARD\0", 0, 0, 0, 0),
        );
        push(
            output,
            &mut n,
            row(ROW_ACTION, ROW_SAVE, b"SAVE\0", 0, 0, 0, 0),
        );
        push(
            output,
            &mut n,
            row(ROW_ACTION, ROW_LOAD, b"LOAD\0", 0, 0, 0, 0),
        );
    } else if editor.mode == EDIT_REVERB {
        push(
            output,
            &mut n,
            row(ROW_VALUE, ROW_SIZE, b"SIZE\0", 0, 2, 1, 1),
        );
        push(
            output,
            &mut n,
            row(ROW_VALUE, ROW_AMOUNT, b"AMOUNT\0", 0, 100, 1, 10),
        );
    } else if editor.mode == EDIT_SOUND {
        push(
            output,
            &mut n,
            row(ROW_HEADING, 0, b"WAVEFORM\0", 0, 0, 0, 0),
        );
        push(
            output,
            &mut n,
            row(ROW_VALUE, ROW_WAVE1, b"WAVE 1\0", 0, 4, 1, 1),
        );
        push(
            output,
            &mut n,
            row(ROW_VALUE, ROW_WAVE2, b"WAVE 2\0", 0, 4, 1, 1),
        );
        push(output, &mut n, row(ROW_HEADING, 0, b"AMP\0", 0, 0, 0, 0));
        push(
            output,
            &mut n,
            row(ROW_VALUE, ROW_AMP_ATTACK, b"ATTACK\0", 0, 16000, 1, 100),
        );
        push(
            output,
            &mut n,
            row(ROW_VALUE, ROW_AMP_RELEASE, b"RELEASE\0", 0, 16000, 1, 100),
        );
        push(output, &mut n, row(ROW_HEADING, 0, b"MIX\0", 0, 0, 0, 0));
        push(
            output,
            &mut n,
            row(ROW_VALUE, ROW_MIX_ATTACK, b"ATTACK\0", 0, 500, 1, 100),
        );
        push(
            output,
            &mut n,
            row(ROW_VALUE, ROW_MIX_RELEASE, b"RELEASE\0", 0, 500, 1, 100),
        );
        push(output, &mut n, row(ROW_HEADING, 0, b"PITCH\0", 0, 0, 0, 0));
        push(
            output,
            &mut n,
            row(ROW_VALUE, ROW_SWEEP, b"SWEEP\0", -24, 24, 1, 12),
        );
        push(
            output,
            &mut n,
            row(ROW_VALUE, ROW_DECAY, b"DECAY\0", 0, 2000, 1, 100),
        );
        push(
            output,
            &mut n,
            row(ROW_VALUE, ROW_SEND, b"REVERB\0", 0, 1, 1, 1),
        );
    } else if editor.mode == EDIT_MENU {
        let mut actions = [0; EDITOR_MENU_ITEMS];
        let count = menu(editor, &mut actions);
        for action in actions[..count].iter().copied() {
            let mut kind = ROW_ACTION;
            let mut min = 0;
            let mut max = 0;
            let mut fine = 0;
            let mut coarse = 0;
            if action == ACTION_SOUND
                || action == ACTION_PLACE
                || action == ACTION_PATTERN
            {
                kind = ROW_SUBMENU;
            } else if matches!(
                action,
                ACTION_LENGTH
                    | ACTION_DIVISION
                    | ACTION_CHANNEL
                    | ACTION_PITCH
                    | ACTION_DURATION
                    | ACTION_PERIOD
                    | ACTION_CHANCE
                    | ACTION_LOCK_ATTACK_ENABLE
                    | ACTION_LOCK_RELEASE_ENABLE
                    | ACTION_LOCK_ATTACK
                    | ACTION_LOCK_RELEASE
            ) {
                kind = ROW_VALUE;
            }
            match action {
                ACTION_LENGTH => {
                    min = 1;
                    max = 64;
                    fine = 1;
                    coarse = 1;
                }
                ACTION_DIVISION => {
                    max = 11;
                    fine = 1;
                    coarse = 1;
                }
                ACTION_CHANNEL => {
                    max = 7;
                    fine = 1;
                    coarse = 1;
                }
                ACTION_PITCH => {
                    max = 108;
                    fine = 1;
                    coarse = 12;
                }
                ACTION_DURATION => {
                    min = 5;
                    max = 1280;
                    fine = 1;
                    coarse = 20;
                }
                ACTION_PERIOD => {
                    min = 2;
                    max = 32;
                    fine = 1;
                    coarse = 1;
                }
                ACTION_CHANCE => {
                    max = 100;
                    fine = 1;
                    coarse = 10;
                }
                ACTION_LOCK_ATTACK_ENABLE | ACTION_LOCK_RELEASE_ENABLE => {
                    max = 1;
                    fine = 1;
                    coarse = 1;
                }
                ACTION_LOCK_ATTACK | ACTION_LOCK_RELEASE => {
                    min = -16000;
                    max = 16000;
                    fine = 1;
                    coarse = 100;
                }
                _ => {}
            }
            push(
                output,
                &mut n,
                EditorRow {
                    kind,
                    id: action,
                    min,
                    max,
                    fine,
                    coarse,
                    label: editor_action_label(action),
                },
            );
        }
    }
    n
}

/// Writes menu rows without changing entries beyond the returned count.
///
/// # Safety
/// `editor` and `output` must point to distinct valid C objects; `output`
/// must hold at least `EDITOR_ROWS` writable rows.
#[no_mangle]
pub unsafe extern "C" fn editor_rows(
    editor: *const Editor,
    output: *mut EditorRow,
) -> c_int {
    // SAFETY: The C caller supplies a valid immutable editor.
    let editor = unsafe { &*editor };
    // SAFETY: The C caller supplies 16 distinct writable rows.
    let output = unsafe { &mut *(output as *mut [EditorRow; EDITOR_ROWS]) };
    rows(editor, output) as c_int
}

fn cancel_move(editor: &mut Editor) {
    editor.x = editor.source_x;
    editor.y = editor.source_y;
    editor.mode = EDIT_PLANE;
    editor.gesture = 0;
    editor.message = text(b"CANCELLED\0");
}

fn finish(editor: &mut Editor, result: c_int) {
    editor.message = score_message(result);
    if result == 0 {
        editor.mode = EDIT_PLANE;
    }
}

fn enter_context(editor: &mut Editor) {
    let cell = resolve(&editor.score, editor.x, editor.y);
    editor.lane = cell.lane;
    editor.target = cell.tile;
    if cell.tile != 0 {
        editor.value = editor.score.tiles[usize::from(cell.tile)].value;
    }
    editor.mode = EDIT_MENU;
    editor.selected = 0;
    editor.message = text(b"\0");
}

/// Skips headings without letting a selection leave its menu.
fn selected_row(
    rows: &[EditorRow; EDITOR_ROWS],
    count: usize,
    selected: c_int,
    direction: c_int,
) -> c_int {
    let mut next = selected;
    loop {
        let candidate = clamp(next + direction, 0, count as c_int - 1);
        if candidate == next {
            return selected;
        }
        next = candidate;
        if rows[next as usize].kind != ROW_HEADING {
            return next;
        }
    }
}

fn tile_value_row(id: c_int) -> bool {
    matches!(
        id,
        ACTION_PITCH
            | ACTION_DURATION
            | ACTION_PERIOD
            | ACTION_CHANCE
            | ACTION_LOCK_ATTACK_ENABLE
            | ACTION_LOCK_RELEASE_ENABLE
            | ACTION_LOCK_ATTACK
            | ACTION_LOCK_RELEASE
    )
}

fn current_value(editor: &Editor, id: c_int) -> Option<c_int> {
    let sound = if id >= ROW_WAVE1 {
        Some(editor.score.sounds[editor.sound_channel as usize])
    } else {
        None
    };
    let value = if tile_value_row(id) {
        Some(editor.score.tiles[usize::from(editor.target)].value)
    } else {
        None
    };
    let result = match id {
        ROW_BPM => editor.score.bpm,
        ROW_SLOT => editor.storage_slot,
        ROW_SIZE => editor.score.reverb.size,
        ROW_AMOUNT => editor.score.reverb.amount,
        ROW_WAVE1 => sound?.wave_a,
        ROW_WAVE2 => sound?.wave_b,
        ROW_AMP_ATTACK => sound?.attack,
        ROW_AMP_RELEASE => sound?.release,
        ROW_MIX_ATTACK => sound?.mix_attack,
        ROW_MIX_RELEASE => sound?.mix_release,
        ROW_SWEEP => sound?.sweep,
        ROW_DECAY => sound?.decay,
        ROW_SEND => sound?.reverb,
        ACTION_LENGTH => editor.score.lanes[editor.lane as usize].length,
        ACTION_DIVISION => {
            // SAFETY: The editor keeps a valid selected lane.
            let division =
                unsafe { score_division(&editor.score, editor.lane) };
            score_divisions
                .iter()
                .position(|&value| value == division)
                .unwrap_or(score_divisions.len() - 1) as c_int
        }
        ACTION_CHANNEL => {
            // SAFETY: The editor keeps a valid selected lane.
            unsafe { score_channel(&editor.score, editor.lane) }
        }
        ACTION_PITCH => value?.pitch,
        ACTION_DURATION => value?.length,
        ACTION_PERIOD => value?.period,
        ACTION_CHANCE => value?.chance,
        ACTION_LOCK_ATTACK_ENABLE => c_int::from(value?.lock_mask & 1 != 0),
        ACTION_LOCK_RELEASE_ENABLE => c_int::from(value?.lock_mask & 2 != 0),
        ACTION_LOCK_ATTACK => value?.attack,
        ACTION_LOCK_RELEASE => value?.release,
        _ => return None,
    };
    Some(result)
}

/// Routes each setting through the validator that owns its score field.
fn apply_value(editor: &mut Editor, id: c_int, candidate: c_int) {
    let mut sound = if id >= ROW_WAVE1 {
        Some(editor.score.sounds[editor.sound_channel as usize])
    } else {
        None
    };
    let mut reverb = editor.score.reverb;
    let mut value = if tile_value_row(id) {
        Some(editor.score.tiles[usize::from(editor.target)].value)
    } else {
        None
    };
    let mut result = 0;
    match id {
        ROW_BPM => {
            // SAFETY: The editor exclusively owns its embedded score.
            result = unsafe { score_set_bpm(&mut editor.score, candidate) };
        }
        ROW_SLOT => {
            editor.storage_slot = candidate;
            if editor.load_busy == 0 {
                editor.message = text(b"\0");
            }
            return;
        }
        ROW_SIZE => reverb.size = candidate,
        ROW_AMOUNT => reverb.amount = candidate,
        ROW_WAVE1 => {
            sound
                .as_mut()
                .expect("sound row requires a selected channel")
                .wave_a = candidate
        }
        ROW_WAVE2 => {
            sound
                .as_mut()
                .expect("sound row requires a selected channel")
                .wave_b = candidate
        }
        ROW_AMP_ATTACK => {
            sound
                .as_mut()
                .expect("sound row requires a selected channel")
                .attack = candidate
        }
        ROW_AMP_RELEASE => {
            sound
                .as_mut()
                .expect("sound row requires a selected channel")
                .release = candidate
        }
        ROW_MIX_ATTACK => {
            sound
                .as_mut()
                .expect("sound row requires a selected channel")
                .mix_attack = candidate
        }
        ROW_MIX_RELEASE => {
            sound
                .as_mut()
                .expect("sound row requires a selected channel")
                .mix_release = candidate
        }
        ROW_SWEEP => {
            sound
                .as_mut()
                .expect("sound row requires a selected channel")
                .sweep = candidate
        }
        ROW_DECAY => {
            sound
                .as_mut()
                .expect("sound row requires a selected channel")
                .decay = candidate
        }
        ROW_SEND => {
            sound
                .as_mut()
                .expect("sound row requires a selected channel")
                .reverb = candidate
        }
        ACTION_LENGTH => {
            // SAFETY: The editor exclusively owns its embedded score.
            result = unsafe {
                score_resize(&mut editor.score, editor.lane, candidate)
            };
        }
        ACTION_DIVISION => {
            // SAFETY: The editor exclusively owns its embedded score.
            result = unsafe {
                score_set_division(
                    &mut editor.score,
                    editor.lane,
                    score_divisions[candidate as usize],
                )
            };
        }
        ACTION_CHANNEL => {
            // SAFETY: The editor exclusively owns its embedded score.
            result = unsafe {
                score_set_channel(&mut editor.score, editor.lane, candidate)
            };
        }
        ACTION_PITCH => {
            value
                .as_mut()
                .expect("tile row requires a selected tile")
                .pitch = candidate
        }
        ACTION_DURATION => {
            value
                .as_mut()
                .expect("tile row requires a selected tile")
                .length = candidate
        }
        ACTION_PERIOD => {
            value
                .as_mut()
                .expect("tile row requires a selected tile")
                .period = candidate
        }
        ACTION_CHANCE => {
            value
                .as_mut()
                .expect("tile row requires a selected tile")
                .chance = candidate
        }
        ACTION_LOCK_ATTACK_ENABLE => {
            let value =
                value.as_mut().expect("tile row requires a selected tile");
            if candidate != 0 {
                value.lock_mask |= 1;
            } else {
                value.lock_mask &= !1;
                value.attack = 0;
            }
        }
        ACTION_LOCK_RELEASE_ENABLE => {
            let value =
                value.as_mut().expect("tile row requires a selected tile");
            if candidate != 0 {
                value.lock_mask |= 2;
            } else {
                value.lock_mask &= !2;
                value.release = 0;
            }
        }
        ACTION_LOCK_ATTACK => {
            let value =
                value.as_mut().expect("tile row requires a selected tile");
            if value.lock_mask & 1 == 0 {
                return;
            }
            value.attack = candidate;
        }
        ACTION_LOCK_RELEASE => {
            let value =
                value.as_mut().expect("tile row requires a selected tile");
            if value.lock_mask & 2 == 0 {
                return;
            }
            value.release = candidate;
        }
        _ => {}
    }
    if id == ROW_SIZE || id == ROW_AMOUNT {
        // SAFETY: The editor exclusively owns its embedded score.
        result = unsafe { score_set_reverb(&mut editor.score, &reverb) };
    }
    if let Some(sound) = sound {
        // SAFETY: The editor exclusively owns its embedded score.
        result = unsafe {
            score_set_sound(&mut editor.score, editor.sound_channel, &sound)
        };
    }
    if let Some(value) = value {
        // SAFETY: The editor exclusively owns its embedded score.
        result =
            unsafe { score_edit(&mut editor.score, editor.target, &value) };
        if result == 0 && value.kind == TILE_NOTE {
            editor.last_note = value;
        }
    }
    editor.message = score_message(result);
}

// A pending load permits slot selection but blocks score edits.
fn adjust(
    editor: &mut Editor,
    row: EditorRow,
    direction: c_int,
    coarse: c_int,
) {
    if row.kind != ROW_VALUE || direction == 0 {
        return;
    }
    let Some(current) = current_value(editor, row.id) else {
        return;
    };
    let step = if coarse != 0 { row.coarse } else { row.fine };
    let candidate = clamp(current + direction * step, row.min, row.max);
    if candidate == current || editor.load_busy != 0 && row.id != ROW_SLOT {
        return;
    }
    apply_value(editor, row.id, candidate);
}

/// Defers card I/O to the main loop and retains nested-menu selection.
fn activate(editor: &mut Editor, row: EditorRow) {
    if editor.load_busy != 0 && row.id != ROW_REVERB && row.id != ROW_SLOT {
        return;
    }
    match row.id {
        ROW_REVERB => {
            editor.mode = EDIT_REVERB;
            editor.parent_selected = editor.selected;
            editor.selected = 0;
        }
        ROW_CHECK => editor.storage_request = STORAGE_ACTION_CHECK,
        ROW_SAVE => editor.storage_request = STORAGE_ACTION_SAVE,
        ROW_LOAD => editor.storage_request = STORAGE_ACTION_LOAD,
        ACTION_CREATE => {
            // SAFETY: The editor exclusively owns its embedded score.
            let result = unsafe {
                score_create(&mut editor.score, editor.x, editor.y, 16)
            };
            finish(editor, result);
        }
        ACTION_PLACE => {
            editor.tile_candidate = editor.last_tile;
            editor.parent_selected = editor.selected;
            editor.mode = EDIT_PICKER;
        }
        ACTION_PASTE => {
            // SAFETY: The score and clipboard are distinct editor fields.
            let result = unsafe {
                score_paste(
                    &mut editor.score,
                    editor.x,
                    editor.y,
                    &editor.clipboard,
                )
            };
            finish(editor, result);
        }
        ACTION_COPY => {
            // SAFETY: The score and clipboard are distinct editor fields.
            unsafe {
                score_copy(
                    &editor.score,
                    editor.x,
                    editor.y,
                    &mut editor.clipboard,
                )
            };
            editor.mode = EDIT_PLANE;
        }
        ACTION_REMOVE => {
            if editor.score.tiles[usize::from(editor.target)].value.kind
                == TILE_JUMP
            {
                editor.parent_selected = editor.selected;
                editor.mode = EDIT_DELETE;
            } else {
                // SAFETY: The editor exclusively owns its embedded score.
                let result = unsafe {
                    score_remove(&mut editor.score, editor.x, editor.y)
                };
                finish(editor, result);
            }
        }
        ACTION_DELETE => {
            editor.parent_selected = editor.selected;
            editor.mode = EDIT_DELETE;
        }
        ACTION_SOUND => {
            // SAFETY: The editor owns a valid selected lane.
            editor.sound_channel =
                unsafe { score_channel(&editor.score, editor.lane) };
            editor.parent_selected = editor.selected;
            editor.selected = 1;
            editor.mode = EDIT_SOUND;
        }
        ACTION_PATTERN => {
            editor.value = editor.score.tiles[usize::from(editor.target)].value;
            editor.pattern_cursor = 0;
            editor.parent_selected = editor.selected;
            editor.mode = EDIT_PATTERN;
        }
        _ => {}
    }
}

/// Keeps a drag gesture tied to its source until release or cancellation.
fn update_plane(editor: &mut Editor, frame: &InputFrame) {
    if frame.cross != 0 && editor.mode == EDIT_PLANE && editor.load_busy == 0 {
        editor.gesture = 1;
        editor.directed = 0;
        editor.source_x = editor.x;
        editor.source_y = editor.y;
    }
    if frame.circle != 0 && editor.gesture != 0 {
        cancel_move(editor);
        return;
    }
    if editor.gesture != 0 && (frame.dx != 0 || frame.dy != 0) {
        editor.directed = 1;
        let cell = at(&editor.score, editor.source_x, editor.source_y);
        if cell.kind == 3 || cell.kind == 1 {
            editor.mode = EDIT_MOVE;
        }
    }
    editor.x = clamp(editor.x + frame.dx, 0, SCORE_WIDTH - 1);
    editor.y = clamp(editor.y + frame.dy, 0, SCORE_HEIGHT - 1);
    if frame.dx != 0 || frame.dy != 0 {
        editor.message = text(b"\0");
    }
    if frame.cross_released != 0 && editor.gesture != 0 {
        editor.gesture = 0;
        if editor.mode == EDIT_MOVE {
            let mut plan = MovePlan {
                sx: 0,
                sy: 0,
                x: 0,
                y: 0,
                result: 0,
            };
            // SAFETY: The editor owns the score, and the local plan is distinct.
            unsafe {
                score_plan_move(
                    &editor.score,
                    editor.source_x,
                    editor.source_y,
                    editor.x,
                    editor.y,
                    &mut plan,
                )
            };
            // SAFETY: The editor owns the score, and the local plan is distinct.
            let result = unsafe { score_apply_move(&mut editor.score, &plan) };
            if result != 0 {
                editor.x = editor.source_x;
                editor.y = editor.source_y;
            }
            editor.mode = EDIT_PLANE;
            editor.message = score_message(result);
        } else if editor.directed == 0 {
            enter_context(editor);
        }
    }
}

/// Places a chosen tile and remembers successful note settings.
fn update_picker(editor: &mut Editor, frame: &InputFrame) {
    editor.tile_candidate = clamp(editor.tile_candidate + frame.row_dy, 1, 5);
    if frame.cross != 0 && editor.load_busy == 0 {
        let value = if editor.tile_candidate == TILE_NOTE {
            editor.last_note
        } else {
            default_value(editor.tile_candidate)
        };
        // SAFETY: The editor owns its score, and `value` is a local copy.
        let result = unsafe {
            score_place_value(&mut editor.score, editor.x, editor.y, &value)
        };
        if result == 0 {
            editor.last_tile = editor.tile_candidate;
            if value.kind == TILE_NOTE {
                editor.last_note = value;
            }
        }
        finish(editor, result);
    }
}

/// Keeps pattern bit edits behind model validation.
fn update_pattern(editor: &mut Editor, frame: &InputFrame) {
    let period = editor.score.tiles[usize::from(editor.target)].value.period;
    editor.pattern_cursor = clamp(
        editor.pattern_cursor + frame.dx + frame.dy * 8,
        0,
        period - 1,
    );
    if frame.cross != 0 && editor.load_busy == 0 {
        let mut value = editor.score.tiles[usize::from(editor.target)].value;
        value.pattern ^= 1u32 << editor.pattern_cursor;
        // SAFETY: The editor owns its score, and `value` is a local copy.
        let result =
            unsafe { score_edit(&mut editor.score, editor.target, &value) };
        if result == 0 {
            editor.value = value;
        }
        editor.message = score_message(result);
    }
}

/// A row move never also edits a value in the same frame.
fn update_rows(editor: &mut Editor, frame: &InputFrame) {
    let empty = EditorRow {
        kind: 0,
        id: 0,
        min: 0,
        max: 0,
        fine: 0,
        coarse: 0,
        label: ptr::null(),
    };
    let mut list = [empty; EDITOR_ROWS];
    let count = rows(editor, &mut list);
    if count == 0 {
        return;
    }
    if editor.selected < 0
        || editor.selected as usize >= count
        || list[editor.selected as usize].kind == ROW_HEADING
    {
        editor.selected = selected_row(&list, count, 0, 1);
    }
    if frame.row_dy != 0 {
        editor.selected =
            selected_row(&list, count, editor.selected, frame.row_dy);
        return;
    }
    let row = list[editor.selected as usize];
    if frame.value_dir != 0 {
        adjust(editor, row, frame.value_dir, frame.value_coarse);
    }
    if frame.cross != 0 && row.kind != ROW_VALUE {
        activate(editor, row);
    }
}

/// Prioritizes disconnection and exits so one frame has one effect.
fn update(editor: &mut Editor, frame: &InputFrame) {
    if frame.connected == 0 {
        if editor.gesture != 0 || editor.mode == EDIT_MOVE {
            cancel_move(editor);
        }
        return;
    }
    if frame.select != 0 {
        let close = editor.mode == EDIT_MAIN;
        if editor.gesture != 0 || editor.mode == EDIT_MOVE {
            cancel_move(editor);
        }
        editor.gesture = 0;
        editor.mode = if close { EDIT_PLANE } else { EDIT_MAIN };
        editor.selected = if editor.load_busy != 0 { 2 } else { 0 };
        editor.message = text(b"\0");
        return;
    }
    if editor.mode == EDIT_PLANE || editor.mode == EDIT_MOVE {
        update_plane(editor, frame);
        return;
    }
    editor.gesture = 0;
    if frame.circle != 0 {
        if editor.mode == EDIT_MAIN || editor.mode == EDIT_MENU {
            editor.mode = EDIT_PLANE;
        } else if editor.mode == EDIT_REVERB {
            editor.mode = EDIT_MAIN;
            editor.selected = editor.parent_selected;
        } else {
            editor.mode = EDIT_MENU;
            editor.selected = editor.parent_selected;
        }
        editor.message = text(b"\0");
        return;
    }
    if editor.mode == EDIT_PICKER {
        update_picker(editor, frame);
        return;
    }
    if editor.mode == EDIT_DELETE {
        if frame.cross != 0 && editor.load_busy == 0 {
            // SAFETY: The editor exclusively owns its embedded score.
            let result = if editor.target != 0 {
                unsafe { score_remove(&mut editor.score, editor.x, editor.y) }
            } else {
                unsafe { score_delete(&mut editor.score, editor.lane) }
            };
            finish(editor, result);
        }
        return;
    }
    if editor.mode == EDIT_PATTERN {
        update_pattern(editor, frame);
        return;
    }
    update_rows(editor, frame);
}

/// Applies one normalized input frame and refreshes save capacity.
///
/// # Safety
/// `editor` and `frame` must point to distinct valid C objects, with the
/// editor exclusively writable.
#[no_mangle]
pub unsafe extern "C" fn editor_update(
    editor: *mut Editor,
    frame: *const InputFrame,
) {
    // SAFETY: The C caller supplies exclusive editor access and a distinct frame.
    let editor = unsafe { &mut *editor };
    // SAFETY: The C caller supplies a valid immutable input frame.
    update(editor, unsafe { &*frame });
    refresh_capacity(editor);
}
