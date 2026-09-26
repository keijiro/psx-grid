//! Provides the shared score layout and portable model queries.
//!
//! The caller owns fixed score storage. Queries, validation, and settings
//! operate on that storage without allocation.

// Implementation notes:
// These layouts mirror score.h. Aggregate values cross the C boundary through
// pointers so the MIPS C ABI never has to pass or return them by value.

use core::ffi::c_int;

pub(crate) const LANES: usize = 16;
pub(crate) const CHANNELS: usize = 8;
pub(crate) const STEPS: usize = 64;
pub(crate) const TILE_CAPACITY: usize = 4096;

#[repr(C)]
#[derive(Clone, Copy)]
pub(crate) struct SoundSettings {
    pub(crate) attack: c_int,
    pub(crate) release: c_int,
    pub(crate) wave_a: c_int,
    pub(crate) wave_b: c_int,
    pub(crate) mix_attack: c_int,
    pub(crate) mix_release: c_int,
    pub(crate) sweep: c_int,
    pub(crate) decay: c_int,
    pub(crate) reverb: c_int,
}

#[repr(C)]
#[derive(Clone, Copy)]
pub(crate) struct ReverbSettings {
    pub(crate) size: c_int,
    pub(crate) amount: c_int,
}

#[repr(C)]
#[derive(Clone, Copy)]
pub(crate) struct TileValue {
    pub(crate) kind: c_int,
    pub(crate) pitch: c_int,
    pub(crate) length: c_int,
    pub(crate) period: c_int,
    pub(crate) chance: c_int,
    pub(crate) pattern: u32,
    pub(crate) lock_mask: c_int,
    pub(crate) attack: c_int,
    pub(crate) release: c_int,
}

#[repr(C)]
pub(crate) struct Tile {
    pub(crate) value: TileValue,
    pub(crate) next: u16,
    pub(crate) branch: c_int,
}

#[repr(C)]
pub(crate) struct Lane {
    pub(crate) active: c_int,
    pub(crate) x: c_int,
    pub(crate) y: c_int,
    pub(crate) length: c_int,
    pub(crate) division: c_int,
    pub(crate) channel: c_int,
    pub(crate) source: u16,
    pub(crate) tiles: [u16; STEPS],
}

/// Shared fixed-pool score layout supplied by C callers.
#[repr(C)]
pub struct Score {
    pub(crate) lanes: [Lane; LANES],
    pub(crate) tiles: [Tile; TILE_CAPACITY + 1],
    pub(crate) sounds: [SoundSettings; CHANNELS],
    pub(crate) bpm: c_int,
    pub(crate) reverb: ReverbSettings,
    pub(crate) revision: u32,
    pub(crate) generation: u32,
    pub(crate) lane_generation: [u32; LANES],
    pub(crate) tile_generation: [u32; TILE_CAPACITY + 1],
}

/// C-facing cell location returned through an output pointer.
#[repr(C)]
#[derive(Clone, Copy)]
pub struct Cell {
    pub(crate) kind: c_int,
    pub(crate) lane: c_int,
    pub(crate) step: c_int,
    pub(crate) depth: c_int,
    pub(crate) tile: u16,
}

const _: () = assert!(core::mem::size_of::<TileValue>() == 36);
const _: () = assert!(core::mem::size_of::<Tile>() == 44);
const _: () = assert!(core::mem::size_of::<Lane>() == 156);
const _: () = assert!(core::mem::size_of::<Score>() == 199524);
const _: () = assert!(core::mem::size_of::<Cell>() == 20);

/// Supported step divisions shared with C editor menus and fixtures.
#[no_mangle]
pub static score_divisions: [c_int; 12] =
    [1, 2, 3, 4, 6, 8, 12, 16, 24, 32, 48, 64];

pub(crate) fn valid(score: &Score, lane: c_int) -> bool {
    lane >= 0
        && (lane as usize) < LANES
        && score.lanes[lane as usize].active != 0
}

pub(crate) fn owner(score: &Score, source: u16) -> c_int {
    for i in 0..LANES {
        let lane = &score.lanes[i];
        if lane.active == 0 {
            continue;
        }
        for &head in &lane.tiles[..lane.length as usize] {
            let mut tile = head;
            while tile != 0 {
                if tile == source {
                    return i as c_int;
                }
                tile = score.tiles[usize::from(tile)].next;
            }
        }
    }
    -1
}

fn inherited(score: &Score, mut lane: c_int, channel: bool) -> c_int {
    for _ in 0..LANES {
        if !valid(score, lane) {
            break;
        }
        let current = &score.lanes[lane as usize];
        if current.source == 0 {
            return if channel {
                current.channel
            } else {
                current.division
            };
        }
        lane = owner(score, current.source);
    }
    if channel {
        -1
    } else {
        16
    }
}

pub(crate) fn at(score: &Score, x: c_int, y: c_int) -> Cell {
    let empty = Cell {
        kind: 0,
        lane: -1,
        step: -1,
        depth: 0,
        tile: 0,
    };
    for (i, lane) in score.lanes.iter().enumerate() {
        if lane.active == 0
            || x < lane.x
            || x > lane.x + lane.length + 1
            || y < lane.y
        {
            continue;
        }
        let step = x - lane.x - 1;
        let depth = y - lane.y;
        if depth == 0 && (step == -1 || step == lane.length) {
            return Cell {
                kind: if step == -1 { 1 } else { 4 },
                lane: i as c_int,
                step,
                depth: 0,
                tile: 0,
            };
        }
        if step < 0 || step >= lane.length {
            continue;
        }
        let mut tile = lane.tiles[step as usize];
        for _ in 0..depth {
            if tile == 0 {
                break;
            }
            tile = score.tiles[usize::from(tile)].next;
        }
        if tile != 0 || depth == 0 {
            return Cell {
                kind: if tile != 0 { 3 } else { 2 },
                lane: i as c_int,
                step,
                depth,
                tile,
            };
        }
    }
    empty
}

pub(crate) fn resolve(score: &Score, x: c_int, y: c_int) -> Cell {
    let mut cell = at(score, x, y);
    if cell.kind != 0 {
        return cell;
    }
    if y > 0 {
        cell = at(score, x, y - 1);
        if cell.kind == 3 {
            cell.kind = 2;
            cell.depth += 1;
            cell.tile = 0;
            return cell;
        }
    }
    Cell {
        kind: 0,
        lane: -1,
        step: -1,
        depth: 0,
        tile: 0,
    }
}

pub(crate) fn default_value(kind: c_int) -> TileValue {
    TileValue {
        kind,
        pitch: 48,
        length: 20,
        period: 4,
        chance: 50,
        pattern: 1,
        lock_mask: 0,
        attack: 0,
        release: 0,
    }
}

/// Initializes caller-owned score storage without a large stack temporary.
///
/// # Safety
/// `score` must point to a valid exclusively writable C `Score`.
#[no_mangle]
pub unsafe extern "C" fn score_init(score: *mut Score) {
    // SAFETY: The C caller owns the entire writable score allocation.
    unsafe { core::ptr::write_bytes(score, 0, 1) };
    // SAFETY: The allocation now contains a valid zeroed C Score.
    let score = unsafe { &mut *score };
    score.sounds.fill(SoundSettings {
        attack: 5,
        release: 5,
        wave_a: 0,
        wave_b: 0,
        mix_attack: 120,
        mix_release: 280,
        sweep: 0,
        decay: 200,
        reverb: 0,
    });
    score.bpm = 120;
    score.reverb = ReverbSettings {
        size: 1,
        amount: 30,
    };
}

/// Writes default values for one valid tile kind.
///
/// # Safety
/// `value` must point to a valid writable C `TileValue`.
#[no_mangle]
pub unsafe extern "C" fn score_default(kind: c_int, value: *mut TileValue) {
    // SAFETY: The C caller supplies a writable output object.
    unsafe { *value = default_value(kind) };
}

/// Returns the inherited step division, or 16 for an unresolved lane.
///
/// # Safety
/// `score` must point to a valid immutable C `Score`.
#[no_mangle]
pub unsafe extern "C" fn score_division(
    score: *const Score,
    lane: c_int,
) -> c_int {
    // SAFETY: The C caller supplies a valid immutable score.
    inherited(unsafe { &*score }, lane, false)
}

/// Returns the inherited channel, or -1 for an unresolved lane.
///
/// # Safety
/// `score` must point to a valid immutable C `Score`.
#[no_mangle]
pub unsafe extern "C" fn score_channel(
    score: *const Score,
    lane: c_int,
) -> c_int {
    // SAFETY: The C caller supplies a valid immutable score.
    inherited(unsafe { &*score }, lane, true)
}

/// Sets a root lane's channel and increments its revision on success.
///
/// # Safety
/// `score` must point to a valid exclusively writable C `Score`.
#[no_mangle]
pub unsafe extern "C" fn score_set_channel(
    score: *mut Score,
    lane: c_int,
    channel: c_int,
) -> c_int {
    // SAFETY: The C caller supplies exclusive access to a valid score.
    let score = unsafe { &mut *score };
    if !valid(score, lane)
        || score.lanes[lane as usize].source != 0
        || !(0..CHANNELS as c_int).contains(&channel)
    {
        return 5;
    }
    score.lanes[lane as usize].channel = channel;
    score.revision = score.revision.wrapping_add(1);
    0
}

/// Sets a root lane's supported division and increments its revision.
///
/// # Safety
/// `score` must point to a valid exclusively writable C `Score`.
#[no_mangle]
pub unsafe extern "C" fn score_set_division(
    score: *mut Score,
    lane: c_int,
    division: c_int,
) -> c_int {
    // SAFETY: The C caller supplies exclusive access to a valid score.
    let score = unsafe { &mut *score };
    if !valid(score, lane) || score.lanes[lane as usize].source != 0 {
        return 5;
    }
    if !score_divisions.contains(&division) {
        return 5;
    }
    score.lanes[lane as usize].division = division;
    score.revision = score.revision.wrapping_add(1);
    0
}

/// Sets a supported tempo and increments the score revision on success.
///
/// # Safety
/// `score` must point to a valid exclusively writable C `Score`.
#[no_mangle]
pub unsafe extern "C" fn score_set_bpm(score: *mut Score, bpm: c_int) -> c_int {
    // SAFETY: The C caller supplies exclusive access to a valid score.
    let score = unsafe { &mut *score };
    if !(30..=300).contains(&bpm) {
        return 5;
    }
    score.bpm = bpm;
    score.revision = score.revision.wrapping_add(1);
    0
}

/// Sets a supported shared reverb configuration.
///
/// # Safety
/// `score` and `reverb` must point to distinct valid C objects, with the
/// score exclusively writable.
#[no_mangle]
pub unsafe extern "C" fn score_set_reverb(
    score: *mut Score,
    reverb: *const ReverbSettings,
) -> c_int {
    // SAFETY: The C caller supplies a valid immutable setting object.
    let reverb = unsafe { &*reverb };
    if !(0..=2).contains(&reverb.size) || !(0..=100).contains(&reverb.amount) {
        return SCORE_INVALID;
    }
    // SAFETY: The C caller supplies exclusive access to the score.
    let score = unsafe { &mut *score };
    score.reverb = ReverbSettings {
        size: reverb.size,
        amount: reverb.amount,
    };
    score.revision = score.revision.wrapping_add(1);
    0
}

/// Sets one channel's validated sound settings.
///
/// # Safety
/// `score` and `sound` must point to distinct valid C objects, with the
/// score exclusively writable.
#[no_mangle]
pub unsafe extern "C" fn score_set_sound(
    score: *mut Score,
    channel: c_int,
    sound: *const SoundSettings,
) -> c_int {
    // SAFETY: The C caller supplies a valid immutable sound object.
    let sound = unsafe { *sound };
    if !(0..CHANNELS as c_int).contains(&channel)
        || !(0..=1).contains(&sound.reverb)
        || !(0..=SOUND_MAX_MS).contains(&sound.attack)
        || !(0..=SOUND_MAX_MS).contains(&sound.release)
        || !(0..5).contains(&sound.wave_a)
        || !(0..5).contains(&sound.wave_b)
        || !(0..=500).contains(&sound.mix_attack)
        || !(0..=500).contains(&sound.mix_release)
        || !(-24..=24).contains(&sound.sweep)
        || !(0..=2000).contains(&sound.decay)
    {
        return SCORE_INVALID;
    }
    // SAFETY: The C caller supplies exclusive access to the score.
    let score = unsafe { &mut *score };
    score.sounds[channel as usize] = sound;
    score.revision = score.revision.wrapping_add(1);
    0
}

/// Writes the exact cell found at a grid coordinate.
///
/// # Safety
/// `score` and `cell` must point to distinct valid C objects.
#[no_mangle]
pub unsafe extern "C" fn score_at(
    score: *const Score,
    x: c_int,
    y: c_int,
    cell: *mut Cell,
) {
    // SAFETY: The C caller supplies distinct valid input and output objects.
    unsafe { *cell = at(&*score, x, y) };
}

/// Writes a cell, resolving insertion just below an occupied stack.
///
/// # Safety
/// `score` and `cell` must point to distinct valid C objects.
#[no_mangle]
pub unsafe extern "C" fn score_resolve(
    score: *const Score,
    x: c_int,
    y: c_int,
    cell: *mut Cell,
) {
    // SAFETY: The C caller supplies distinct valid input and output objects.
    unsafe { *cell = resolve(&*score, x, y) };
}

const SCORE_WIDTH: usize = 128;
const SCORE_HEIGHT: usize = 64;
const SOUND_MAX_MS: c_int = 16000;
const SCORE_BOUNDS: c_int = 1;
const SCORE_COLLISION: c_int = 2;
const SCORE_INVALID: c_int = 5;
const SCORE_CYCLE: c_int = 6;

// Model validation is serialized and uses one fixed geometry map so it never
// adds an 8 KiB frame to the console stack.
static mut OCCUPIED: [u8; SCORE_WIDTH * SCORE_HEIGHT] =
    [0; SCORE_WIDTH * SCORE_HEIGHT];

pub(crate) fn value_valid(value: TileValue) -> bool {
    (1..6).contains(&value.kind)
        && (0..=108).contains(&value.pitch)
        && (5..=1280).contains(&value.length)
        && (2..=32).contains(&value.period)
        && (0..=100).contains(&value.chance)
        && (0..=3).contains(&value.lock_mask)
        && (-SOUND_MAX_MS..=SOUND_MAX_MS).contains(&value.attack)
        && (-SOUND_MAX_MS..=SOUND_MAX_MS).contains(&value.release)
        && (value.lock_mask & 1 != 0 || value.attack == 0)
        && (value.lock_mask & 2 != 0 || value.release == 0)
}

/// Changes a live tile's values while preserving its kind and pool identity.
///
/// # Safety
/// `score` must point to a valid exclusively writable C `Score`, and `value`
/// must point to a distinct immutable C `TileValue`.
#[no_mangle]
pub unsafe extern "C" fn score_edit(
    score: *mut Score,
    id: u16,
    value: *const TileValue,
) -> c_int {
    // SAFETY: The C caller supplies distinct valid score and value objects.
    let score = unsafe { &mut *score };
    // SAFETY: The C caller supplies a valid immutable value.
    let value = unsafe { *value };
    if id == 0
        || usize::from(id) > TILE_CAPACITY
        || score.tiles[usize::from(id)].value.kind != value.kind
        || !value_valid(value)
    {
        return SCORE_INVALID;
    }
    score.tiles[usize::from(id)].value = value;
    score.revision = score.revision.wrapping_add(1);
    0
}

fn mark(x: i64, y: i64) -> c_int {
    if x < 0 || x >= SCORE_WIDTH as i64 || y < 0 || y >= SCORE_HEIGHT as i64 {
        return SCORE_BOUNDS;
    }
    let index = y as usize * SCORE_WIDTH + x as usize;
    // SAFETY: Model validation is serialized and `index` is in bounds.
    let cell =
        unsafe { core::ptr::addr_of_mut!(OCCUPIED).cast::<u8>().add(index) };
    // SAFETY: This validation call owns the scratch geometry map.
    if unsafe { *cell } != 0 {
        return SCORE_COLLISION;
    }
    // SAFETY: This validation call owns the scratch geometry map.
    unsafe { *cell = 1 };
    0
}

/// Checks geometry with one lane's position optionally substituted.
fn geometry_at(
    score: &Score,
    moved: usize,
    moved_x: c_int,
    moved_y: c_int,
) -> c_int {
    // SAFETY: Model calls are serialized and own the fixed scratch map.
    unsafe {
        core::ptr::write_bytes(
            core::ptr::addr_of_mut!(OCCUPIED).cast::<u8>(),
            0,
            SCORE_WIDTH * SCORE_HEIGHT,
        )
    };
    for i in 0..LANES {
        if score.lanes[i].active == 0 {
            continue;
        }
        let lane = &score.lanes[i];
        let (lane_x, lane_y) = if i == moved {
            (moved_x, moved_y)
        } else {
            (lane.x, lane.y)
        };
        if !(1..=STEPS as c_int).contains(&lane.length) {
            return SCORE_BOUNDS;
        }
        for step in -1..=lane.length {
            let x = i64::from(lane_x) + i64::from(step) + 1;
            let result = mark(x, i64::from(lane_y));
            if result != 0 {
                return result;
            }
            if step < 0 || step == lane.length {
                continue;
            }
            let mut depth = 0;
            let mut tile = lane.tiles[step as usize];
            while tile != 0 {
                if depth != 0 {
                    let result = mark(x, i64::from(lane_y) + depth);
                    if result != 0 {
                        return result;
                    }
                }
                depth += 1;
                if usize::from(tile) > TILE_CAPACITY
                    || depth > SCORE_HEIGHT as i64
                {
                    return SCORE_INVALID;
                }
                tile = score.tiles[usize::from(tile)].next;
            }
        }
        let mut parent = i as c_int;
        for n in 0..=LANES {
            let source = score.lanes[parent as usize].source;
            if source == 0 {
                break;
            }
            parent = owner(score, source);
            if parent < 0 {
                return SCORE_INVALID;
            }
            if parent == i as c_int || n >= LANES {
                return SCORE_CYCLE;
            }
        }
    }
    0
}

/// Checks the current score's rendered geometry.
pub(crate) fn geometry(score: &Score) -> c_int {
    geometry_at(score, LANES, 0, 0)
}

/// Checks a lane translation without copying the score for a drag preview.
pub(crate) fn geometry_lane_move(
    score: &Score,
    lane: usize,
    x: c_int,
    y: c_int,
) -> c_int {
    geometry_at(score, lane, x, y)
}

/// Validates pool links before traversing geometry or branch ancestry.
fn validate_import(score: &Score) -> c_int {
    let mut seen = [0u8; TILE_CAPACITY + 1];
    for i in 0..LANES {
        let lane = &score.lanes[i];
        if lane.active == 0 {
            continue;
        }
        if !(1..=STEPS as c_int).contains(&lane.length)
            || !(0..SCORE_WIDTH as c_int).contains(&lane.x)
            || !(0..SCORE_HEIGHT as c_int).contains(&lane.y)
        {
            return SCORE_INVALID;
        }
        if usize::from(lane.source) > TILE_CAPACITY
            || lane.source != 0 && lane.channel != -1
            || lane.source == 0
                && !(0..CHANNELS as c_int).contains(&lane.channel)
        {
            return SCORE_INVALID;
        }
        if !score_divisions.contains(&lane.division) {
            return SCORE_INVALID;
        }
        for step in 0..STEPS {
            let mut tile = lane.tiles[step];
            if step >= lane.length as usize && tile != 0 {
                return SCORE_INVALID;
            }
            let mut depth = 0;
            while tile != 0 {
                let index = usize::from(tile);
                depth += 1;
                if index > TILE_CAPACITY
                    || seen[index] != 0
                    || depth > SCORE_HEIGHT - lane.y as usize
                {
                    return SCORE_INVALID;
                }
                seen[index] = 1;
                let value = &score.tiles[index];
                if !value_valid(value.value) {
                    return SCORE_INVALID;
                }
                if value.value.kind == 4
                    && (!valid(score, value.branch)
                        || score.lanes[value.branch as usize].source != tile)
                {
                    return SCORE_INVALID;
                }
                tile = value.next;
            }
        }
    }
    for (i, lane) in score.lanes.iter().enumerate() {
        if lane.active == 0 || lane.source == 0 {
            continue;
        }
        let tile = usize::from(lane.source);
        if seen[tile] == 0
            || score.tiles[tile].value.kind != 4
            || score.tiles[tile].branch != i as c_int
        {
            return SCORE_INVALID;
        }
    }
    for (tile, &visited) in score.tiles.iter().zip(seen.iter()).skip(1) {
        if (tile.value.kind != 0) != (visited != 0) {
            return SCORE_INVALID;
        }
    }
    geometry(score)
}

/// Validates an imported score before its links are trusted by the model.
///
/// # Safety
/// `score` must point to a valid immutable C `Score`. Calls must be
/// serialized because geometry validation shares scratch space.
#[no_mangle]
pub unsafe extern "C" fn score_validate_import(score: *const Score) -> c_int {
    // SAFETY: The C caller supplies a valid immutable score.
    validate_import(unsafe { &*score })
}

const NOTE_NAMES: [&[u8]; 12] = [
    b"C\0", b"C#\0", b"D\0", b"D#\0", b"E\0", b"F\0", b"F#\0", b"G\0", b"G#\0",
    b"A\0", b"A#\0", b"B\0",
];
const TILE_LABELS: [&[u8]; 6] = [
    b"EMPTY\0",
    b"NOTE\0",
    b"CYCLE GATE\0",
    b"PROBABILITY GATE\0",
    b"JUMP\0",
    b"RELATIVE LOCK\0",
];
const SCORE_MESSAGES: [&[u8]; 7] = [
    b"\0",
    b"LIMIT / EDGE\0",
    b"COLLISION\0",
    b"SCORE FULL\0",
    b"REMOVE TRAILING TILES FIRST\0",
    b"INVALID CELL\0",
    b"BRANCH CYCLE\0",
];
const WAVE_NAMES: [&[u8]; 5] =
    [b"SINE\0", b"TRIANGLE\0", b"SAW\0", b"SQUARE\0", b"NOISE\0"];
const REVERB_SIZES: [&[u8]; 3] = [b"SMALL\0", b"MEDIUM\0", b"LARGE\0"];

fn label(names: &[&[u8]], index: c_int) -> *const core::ffi::c_char {
    if index < 0 {
        return c"?".as_ptr();
    }
    names
        .get(index as usize)
        .copied()
        .unwrap_or(b"?\0")
        .as_ptr()
        .cast()
}

/// Returns a pitch-class label for a nonnegative semitone index.
#[no_mangle]
pub extern "C" fn score_note_name(pitch: c_int) -> *const core::ffi::c_char {
    if pitch < 0 {
        return c"?".as_ptr();
    }
    NOTE_NAMES[pitch as usize % NOTE_NAMES.len()]
        .as_ptr()
        .cast()
}

/// Returns a tile-kind label or a fallback for an unknown kind.
#[no_mangle]
pub extern "C" fn score_tile_label(kind: c_int) -> *const core::ffi::c_char {
    label(&TILE_LABELS, kind)
}

/// Returns an editor message for a score result.
#[no_mangle]
pub extern "C" fn score_message(result: c_int) -> *const core::ffi::c_char {
    label(&SCORE_MESSAGES, result)
}

/// Returns a waveform label or a fallback for an unknown wave.
#[no_mangle]
pub extern "C" fn score_wave_name(wave: c_int) -> *const core::ffi::c_char {
    label(&WAVE_NAMES, wave)
}

/// Returns a reverb size label or a fallback for an unknown preset.
#[no_mangle]
pub extern "C" fn score_reverb_size(size: c_int) -> *const core::ffi::c_char {
    label(&REVERB_SIZES, size)
}
