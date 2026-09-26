//! Applies transactional edits to fixed score storage.
//!
//! Mutations stage in one shared score and publish only after geometry and
//! card capacity pass. Main-thread callers serialize access to the scratch.

// Implementation notes:
// A single static scratch score preserves the C model's atomic edits without
// adding a 200 KiB console-stack temporary or a second model copy.

use core::ffi::c_int;
use core::mem::MaybeUninit;
use core::ptr;

use crate::score::{
    at, default_value, geometry, owner, resolve, valid, value_valid, Cell,
    Score, Tile, TileValue, LANES, STEPS, TILE_CAPACITY,
};
use crate::score_format::measure;

const SCORE_OK: c_int = 0;
const SCORE_BOUNDS: c_int = 1;
const SCORE_FULL: c_int = 3;
const SCORE_TILES: c_int = 4;
const SCORE_INVALID: c_int = 5;
const TILE_JUMP: c_int = 4;
const SCORE_HEIGHT: usize = 64;
const SCORE_WIDTH: usize = 128;
const SCORE_FILE_BYTES: usize = 8192;

#[repr(C)]
pub struct Clipboard {
    pub(crate) count: c_int,
    pub(crate) values: [TileValue; SCORE_HEIGHT],
}

#[repr(C)]
pub struct MovePlan {
    pub(crate) sx: c_int,
    pub(crate) sy: c_int,
    pub(crate) x: c_int,
    pub(crate) y: c_int,
    pub(crate) result: c_int,
}

const _: () = assert!(core::mem::size_of::<Clipboard>() == 2308);
const _: () = assert!(core::mem::size_of::<MovePlan>() == 20);

static mut SCRATCH: MaybeUninit<Score> = MaybeUninit::uninit();

/// Borrows the one staged score for a serialized model operation.
///
/// # Safety
/// The caller must serialize all score edit calls and keep `score` distinct
/// from the scratch allocation.
unsafe fn stage(score: &Score) -> &'static mut Score {
    let scratch = ptr::addr_of_mut!(SCRATCH).cast::<Score>();
    // SAFETY: The main-thread model call owns scratch exclusively.
    unsafe { ptr::copy_nonoverlapping(score, scratch, 1) };
    // SAFETY: The copy initialized the complete scratch score.
    unsafe { &mut *scratch }
}

fn admission(score: &Score) -> c_int {
    let result = geometry(score);
    if result != SCORE_OK {
        return result;
    }
    if measure(score) > SCORE_FILE_BYTES {
        SCORE_FULL
    } else {
        SCORE_OK
    }
}

/// Copies an admitted revision to its caller only after all checks pass.
unsafe fn commit(score: &mut Score) -> c_int {
    let scratch = ptr::addr_of_mut!(SCRATCH).cast::<Score>();
    // SAFETY: This serialized edit owns the initialized scratch score.
    let staged = unsafe { &mut *scratch };
    let result = admission(staged);
    if result == SCORE_OK {
        staged.revision = score.revision.wrapping_add(1);
        // SAFETY: Caller storage and shared scratch are distinct.
        unsafe { ptr::copy_nonoverlapping(staged, score, 1) };
    }
    result
}

fn new_lane(
    score: &mut Score,
    x: c_int,
    y: c_int,
    length: c_int,
    source: u16,
) -> Option<usize> {
    if score.generation == u32::MAX {
        return None;
    }
    for i in 0..LANES {
        if score.lanes[i].active == 0 {
            // SAFETY: The selected lane is wholly caller-owned and writable.
            unsafe { ptr::write_bytes(&mut score.lanes[i], 0, 1) };
            score.generation += 1;
            score.lane_generation[i] = score.generation;
            let lane = &mut score.lanes[i];
            lane.active = 1;
            lane.x = x;
            lane.y = y;
            lane.length = length;
            lane.division = 16;
            lane.channel = if source == 0 { 0 } else { -1 };
            lane.source = source;
            return Some(i);
        }
    }
    None
}

fn can_create(score: &Score, x: c_int, y: c_int, length: c_int) -> c_int {
    // SAFETY: Public model calls are serialized; source and scratch differ.
    let staged = unsafe { stage(score) };
    if new_lane(staged, x, y, length, 0).is_none() {
        return SCORE_FULL;
    }
    admission(staged)
}

/// Previews a root lane without publishing the staged score.
///
/// # Safety
/// `score` must point to a valid immutable C `Score`. Model calls must be
/// serialized because staging uses one shared scratch score.
#[no_mangle]
pub unsafe extern "C" fn score_can_create(
    score: *const Score,
    x: c_int,
    y: c_int,
    length: c_int,
) -> c_int {
    // SAFETY: The C caller provides an immutable valid score.
    can_create(unsafe { &*score }, x, y, length)
}

/// Creates an admitted root lane and increments the revision.
///
/// # Safety
/// `score` must point to a valid exclusively writable C `Score`.
#[no_mangle]
pub unsafe extern "C" fn score_create(
    score: *mut Score,
    x: c_int,
    y: c_int,
    length: c_int,
) -> c_int {
    // SAFETY: The C caller provides exclusive access to a valid score.
    let score = unsafe { &mut *score };
    let result = can_create(score, x, y, length);
    if result != SCORE_OK {
        return result;
    }
    // SAFETY: Successful admission left a complete staged score.
    unsafe { commit(score) }
}

fn can_resize(score: &Score, lane: c_int, length: c_int) -> c_int {
    if !valid(score, lane) {
        return SCORE_INVALID;
    }
    if !(1..=STEPS as c_int).contains(&length) {
        return SCORE_BOUNDS;
    }
    let current = &score.lanes[lane as usize];
    if length < current.length {
        for &head in &current.tiles[length as usize..current.length as usize] {
            if head != 0 {
                return SCORE_TILES;
            }
        }
    }
    // SAFETY: Public model calls are serialized; source and scratch differ.
    let staged = unsafe { stage(score) };
    staged.lanes[lane as usize].length = length;
    admission(staged)
}

/// Previews a lane resize without publishing the staged score.
///
/// # Safety
/// `score` must point to a valid immutable C `Score`.
#[no_mangle]
pub unsafe extern "C" fn score_can_resize(
    score: *const Score,
    lane: c_int,
    length: c_int,
) -> c_int {
    // SAFETY: The C caller provides an immutable valid score.
    can_resize(unsafe { &*score }, lane, length)
}

/// Resizes a lane after validating trailing tiles and geometry.
///
/// # Safety
/// `score` must point to a valid exclusively writable C `Score`.
#[no_mangle]
pub unsafe extern "C" fn score_resize(
    score: *mut Score,
    lane: c_int,
    length: c_int,
) -> c_int {
    // SAFETY: The C caller provides exclusive access to a valid score.
    let score = unsafe { &mut *score };
    let result = can_resize(score, lane, length);
    if result != SCORE_OK {
        return result;
    }
    // SAFETY: Successful admission left a complete staged score.
    unsafe { commit(score) }
}

/// Addresses a link by its owner so edits can change it without aliasing.
#[derive(Clone, Copy)]
enum Link {
    Head { lane: usize, step: usize },
    Next { tile: usize },
}

fn link_value(score: &Score, link: Link) -> u16 {
    match link {
        Link::Head { lane, step } => score.lanes[lane].tiles[step],
        Link::Next { tile } => score.tiles[tile].next,
    }
}

fn set_link(score: &mut Score, link: Link, value: u16) {
    match link {
        Link::Head { lane, step } => score.lanes[lane].tiles[step] = value,
        Link::Next { tile } => score.tiles[tile].next = value,
    }
}

fn link_at(score: &Score, cell: Cell) -> Link {
    let mut link = Link::Head {
        lane: cell.lane as usize,
        step: cell.step as usize,
    };
    for _ in 0..cell.depth {
        let tile = link_value(score, link);
        if tile == 0 {
            break;
        }
        link = Link::Next {
            tile: usize::from(tile),
        };
    }
    link
}

/// Clears a dependent branch before its jump tile is recycled.
fn erase_tile(score: &mut Score, id: u16) {
    let tile = usize::from(id);
    if score.tiles[tile].value.kind == TILE_JUMP {
        erase_lane(score, score.tiles[tile].branch as usize);
    }
    // SAFETY: The tile pool index belongs to this exclusively writable score.
    unsafe { ptr::write_bytes(&mut score.tiles[tile], 0, 1) };
}

fn erase_lane(score: &mut Score, lane: usize) {
    let length = score.lanes[lane].length as usize;
    for step in 0..length {
        let mut tile = score.lanes[lane].tiles[step];
        while tile != 0 {
            let next = score.tiles[usize::from(tile)].next;
            erase_tile(score, tile);
            tile = next;
        }
    }
    // SAFETY: The selected lane belongs to this exclusively writable score.
    unsafe { ptr::write_bytes(&mut score.lanes[lane], 0, 1) };
}

/// Deletes a lane and all its descendant branches.
///
/// # Safety
/// `score` must point to a valid exclusively writable C `Score`.
#[no_mangle]
pub unsafe extern "C" fn score_delete(score: *mut Score, lane: c_int) -> c_int {
    // SAFETY: The C caller provides exclusive access to a valid score.
    let score = unsafe { &mut *score };
    if !valid(score, lane) {
        return SCORE_INVALID;
    }
    let source = score.lanes[lane as usize].source;
    if source != 0 {
        let parent = owner(score, source);
        if parent < 0 {
            return SCORE_INVALID;
        }
        let parent = parent as usize;
        for step in 0..score.lanes[parent].length as usize {
            let mut link = Link::Head { lane: parent, step };
            while link_value(score, link) != 0 {
                let tile = link_value(score, link);
                if tile == source {
                    let next = score.tiles[usize::from(source)].next;
                    set_link(score, link, next);
                    erase_tile(score, source);
                    score.revision = score.revision.wrapping_add(1);
                    return SCORE_OK;
                }
                link = Link::Next {
                    tile: usize::from(tile),
                };
            }
        }
    }
    erase_lane(score, lane as usize);
    score.revision = score.revision.wrapping_add(1);
    SCORE_OK
}

/// Removes one tile and any branch it owns.
///
/// # Safety
/// `score` must point to a valid exclusively writable C `Score`.
#[no_mangle]
pub unsafe extern "C" fn score_remove(
    score: *mut Score,
    x: c_int,
    y: c_int,
) -> c_int {
    // SAFETY: The C caller provides exclusive access to a valid score.
    let score = unsafe { &mut *score };
    let cell = at(score, x, y);
    if cell.kind != 3 {
        return SCORE_INVALID;
    }
    let link = link_at(score, cell);
    let next = score.tiles[usize::from(cell.tile)].next;
    set_link(score, link, next);
    erase_tile(score, cell.tile);
    score.revision = score.revision.wrapping_add(1);
    SCORE_OK
}

/// Stages one tile and searches an unobstructed branch location if needed.
fn insert(score: &mut Score, cell: Cell, value: TileValue) -> c_int {
    if !value_valid(value) || cell.kind != 2 && cell.kind != 4 {
        return SCORE_INVALID;
    }
    let lane = cell.lane as usize;
    if cell.kind == 4 {
        if score.lanes[lane].length == STEPS as c_int {
            return SCORE_BOUNDS;
        }
        score.lanes[lane].length += 1;
    }
    let mut id = 1;
    while id <= TILE_CAPACITY && score.tiles[id].value.kind != 0 {
        id += 1;
    }
    if id > TILE_CAPACITY || score.generation == u32::MAX {
        return SCORE_FULL;
    }
    score.generation += 1;
    score.tile_generation[id] = score.generation;
    let link = link_at(score, cell);
    let next = link_value(score, link);
    score.tiles[id] = Tile {
        value,
        next,
        branch: -1,
    };
    set_link(score, link, id as u16);
    if value.kind != TILE_JUMP {
        return SCORE_OK;
    }

    // A branch head needs a clear row below every existing stack.
    let mut bottom = 0;
    for lane in &score.lanes {
        if lane.active == 0 {
            continue;
        }
        bottom = bottom.max(lane.y);
        for &head in &lane.tiles[..lane.length as usize] {
            let mut y = lane.y;
            let mut tile = head;
            while tile != 0 {
                bottom = bottom.max(y);
                tile = score.tiles[usize::from(tile)].next;
                y += 1;
            }
        }
    }
    let Some(branch) = new_lane(score, 0, bottom + 2, 4, id as u16) else {
        return SCORE_FULL;
    };
    score.tiles[id].branch = branch as c_int;
    for y in bottom + 2..SCORE_HEIGHT as c_int {
        for x in 0..(SCORE_WIDTH - 5) as c_int {
            score.lanes[branch].x = x;
            score.lanes[branch].y = y;
            if geometry(score) == SCORE_OK {
                return SCORE_OK;
            }
        }
    }
    SCORE_BOUNDS
}

fn place_value(
    score: &mut Score,
    x: c_int,
    y: c_int,
    value: TileValue,
) -> c_int {
    // SAFETY: Public model calls are serialized; source and scratch differ.
    let staged = unsafe { stage(score) };
    let cell = resolve(score, x, y);
    let result = insert(staged, cell, value);
    if result != SCORE_OK {
        return result;
    }
    // SAFETY: This edit owns a complete staged score.
    unsafe { commit(score) }
}

/// Places a tile value, growing an endpoint if needed.
///
/// # Safety
/// `score` and `value` must point to distinct valid C objects, with the
/// score exclusively writable.
#[no_mangle]
pub unsafe extern "C" fn score_place_value_rust(
    score: *mut Score,
    x: c_int,
    y: c_int,
    value: *const TileValue,
) -> c_int {
    // SAFETY: The C shim supplies exclusive access to a valid score.
    let score = unsafe { &mut *score };
    // SAFETY: The C shim supplies a distinct immutable value.
    place_value(score, x, y, unsafe { *value })
}

/// Places a tile of one supported kind with default values.
///
/// # Safety
/// `score` must point to a valid exclusively writable C `Score`.
#[no_mangle]
pub unsafe extern "C" fn score_place(
    score: *mut Score,
    x: c_int,
    y: c_int,
    kind: c_int,
) -> c_int {
    // SAFETY: The C caller provides exclusive access to a valid score.
    place_value(unsafe { &mut *score }, x, y, default_value(kind))
}

/// Copies a non-jump suffix while preserving an unchanged clipboard on miss.
///
/// # Safety
/// `score` and `clipboard` must point to distinct valid C objects, with
/// `clipboard` exclusively writable.
#[no_mangle]
pub unsafe extern "C" fn score_copy(
    score: *const Score,
    x: c_int,
    y: c_int,
    clipboard: *mut Clipboard,
) {
    // SAFETY: The C caller supplies a valid immutable score.
    let score = unsafe { &*score };
    let cell = at(score, x, y);
    if cell.kind != 3 {
        return;
    }
    let mut tile = cell.tile;
    let mut count = 0;
    while tile != 0 {
        if score.tiles[usize::from(tile)].value.kind != TILE_JUMP {
            count += 1;
        }
        tile = score.tiles[usize::from(tile)].next;
    }
    if count == 0 {
        return;
    }
    // SAFETY: The C caller supplies a distinct writable clipboard.
    let clipboard = unsafe { &mut *clipboard };
    // SAFETY: This successful copy replaces the complete clipboard value.
    unsafe { ptr::write_bytes(clipboard, 0, 1) };
    tile = cell.tile;
    while tile != 0 {
        let entry = &score.tiles[usize::from(tile)];
        if entry.value.kind != TILE_JUMP {
            clipboard.values[clipboard.count as usize] = entry.value;
            clipboard.count += 1;
        }
        tile = entry.next;
    }
}

/// Pastes clipboard values as one admitted stack.
///
/// # Safety
/// `score` and `clipboard` must point to distinct valid C objects, with the
/// score exclusively writable.
#[no_mangle]
pub unsafe extern "C" fn score_paste(
    score: *mut Score,
    x: c_int,
    y: c_int,
    clipboard: *const Clipboard,
) -> c_int {
    // SAFETY: The C caller supplies a valid immutable clipboard.
    let clipboard = unsafe { &*clipboard };
    if !(1..=SCORE_HEIGHT as c_int).contains(&clipboard.count) {
        return SCORE_INVALID;
    }
    // SAFETY: The C caller supplies exclusive access to a distinct score.
    let score = unsafe { &mut *score };
    // SAFETY: Public model calls are serialized; source and scratch differ.
    let staged = unsafe { stage(score) };
    for (i, value) in clipboard.values[..clipboard.count as usize]
        .iter()
        .copied()
        .enumerate()
    {
        if value.kind == TILE_JUMP {
            return SCORE_INVALID;
        }
        let Some(y) = y.checked_add(i as c_int) else {
            return SCORE_BOUNDS;
        };
        let cell = resolve(staged, x, y);
        let result = insert(staged, cell, value);
        if result != SCORE_OK {
            return result;
        }
    }
    // SAFETY: This edit owns a complete staged score.
    unsafe { commit(score) }
}

/// Stages a lane or tile suffix movement for preview and application.
fn move_staged(
    score: &Score,
    sx: c_int,
    sy: c_int,
    x: c_int,
    y: c_int,
) -> c_int {
    let from_cell = at(score, sx, sy);
    let to_cell = resolve(score, x, y);
    // SAFETY: Public model calls are serialized; source and scratch differ.
    let staged = unsafe { stage(score) };
    if from_cell.kind != 1 && from_cell.kind != 3 {
        return SCORE_INVALID;
    }
    if sx == x && sy == y {
        return SCORE_OK;
    }
    if from_cell.kind == 1 {
        staged.lanes[from_cell.lane as usize].x = x;
        staged.lanes[from_cell.lane as usize].y = y;
        return admission(staged);
    }
    if to_cell.kind != 3 && to_cell.kind != 2 && to_cell.kind != 4 {
        return SCORE_INVALID;
    }
    let from = link_at(staged, from_cell);
    let mut tail = from_cell.tile;
    let same = from_cell.lane == to_cell.lane && from_cell.step == to_cell.step;
    // Within a stack the target depth is final after extraction; across
    // steps the entire suffix moves as one chain.
    if same {
        set_link(staged, from, staged.tiles[usize::from(tail)].next);
    } else {
        set_link(staged, from, 0);
        while staged.tiles[usize::from(tail)].next != 0 {
            tail = staged.tiles[usize::from(tail)].next;
        }
    }
    if to_cell.kind == 4 {
        let lane = &mut staged.lanes[to_cell.lane as usize];
        if lane.length == STEPS as c_int {
            return SCORE_BOUNDS;
        }
        lane.length += 1;
    }
    let to = link_at(staged, to_cell);
    staged.tiles[usize::from(tail)].next = link_value(staged, to);
    set_link(staged, to, from_cell.tile);
    admission(staged)
}

/// Writes a move preview through an ABI-stable output pointer.
///
/// # Safety
/// `score` and `plan` must point to distinct valid C objects, with `plan`
/// exclusively writable.
#[no_mangle]
pub unsafe extern "C" fn score_plan_move_rust(
    score: *const Score,
    sx: c_int,
    sy: c_int,
    x: c_int,
    y: c_int,
    plan: *mut MovePlan,
) {
    // SAFETY: The C caller supplies a valid immutable score.
    let result = move_staged(unsafe { &*score }, sx, sy, x, y);
    // SAFETY: The C shim supplies a distinct writable plan.
    unsafe {
        *plan = MovePlan {
            sx,
            sy,
            x,
            y,
            result,
        }
    };
}

/// Rechecks and applies a move plan against the current score.
///
/// # Safety
/// `score` and `plan` must point to distinct valid C objects, with the
/// score exclusively writable.
#[no_mangle]
pub unsafe extern "C" fn score_apply_move_rust(
    score: *mut Score,
    plan: *const MovePlan,
) -> c_int {
    // SAFETY: The C shim supplies a valid immutable plan.
    let plan = unsafe { &*plan };
    // SAFETY: The C caller provides exclusive access to a distinct score.
    let score = unsafe { &mut *score };
    let result = move_staged(score, plan.sx, plan.sy, plan.x, plan.y);
    if result != SCORE_OK || plan.sx == plan.x && plan.sy == plan.y {
        return result;
    }
    // SAFETY: This edit owns a complete admitted staged score.
    unsafe { commit(score) }
}
