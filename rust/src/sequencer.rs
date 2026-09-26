//! Traverses immutable score snapshots into bounded, clocked note events.
//!
//! Caller-owned state can resume a split slice without replaying events. The
//! timer service serializes access and prepared replacements enter at a lap seam.

// Implementation notes:
// The layout mirrors sequencer.h while C tests and the platform inspect state.
// A C entry point owns the aggregate NoteSink ABI. Note callbacks use pointers
// to keep the timer path compatible with the shared C/Rust interface.

use core::ffi::{c_int, c_void};

use crate::score::{Score, SoundSettings, TileValue, CHANNELS, LANES};

const HEIGHT: usize = 64;
const VOICES: usize = 12;
const HZ: u32 = 4_233_600;
const SLICE_BUDGET: c_int = 2;
const TILE_BUDGET: c_int = 32;
const MAX_MS: c_int = 16_000;
const NOTE: c_int = 1;
const CYCLE: c_int = 2;
const PROBABILITY: c_int = 3;
const JUMP: c_int = 4;
const RELATIVE: c_int = 5;
const LOCK_ATTACK: c_int = 1;
const LOCK_RELEASE: c_int = 2;

#[repr(C)]
#[derive(Clone, Copy)]
pub struct NoteSink {
    context: *mut c_void,
    on: Option<
        unsafe extern "C" fn(
            *mut c_void,
            u64,
            c_int,
            *const SoundSettings,
        ) -> u32,
    >,
    off: Option<unsafe extern "C" fn(*mut c_void, u64, u32)>,
    stop: Option<unsafe extern "C" fn(*mut c_void, u64)>,
    advance: Option<unsafe extern "C" fn(*mut c_void, u64)>,
}

#[repr(C)]
#[derive(Clone, Copy)]
struct Runner {
    origin: c_int,
    lane: c_int,
    step: c_int,
    playing_lane: c_int,
    playing_step: c_int,
    lap: u32,
    duration: u32,
    active: c_int,
    next: u64,
    held: [u16; HEIGHT],
    held_generation: [u32; HEIGHT],
    held_count: c_int,
}

#[repr(C)]
#[derive(Clone, Copy)]
struct NoteOff {
    at: u64,
    token: u32,
}

/// Caller-owned transport state mirrored from the C diagnostic layout.
#[repr(C)]
pub struct Sequencer {
    score: *const Score,
    sink: NoteSink,
    runners: [Runner; LANES],
    order: [c_int; LANES],
    offs: [NoteOff; VOICES],
    count: c_int,
    playing: c_int,
    master: c_int,
    replacement: *mut Sequencer,
    replacement_at: u64,
    random: u32,
    skipped: u32,
    overloads: u32,
    slice_at: u64,
    working: [SoundSettings; CHANNELS],
    slicing: c_int,
    runner_index: c_int,
    visiting: c_int,
    held_index: c_int,
    jump: c_int,
    cursor: u16,
    step_ticks: u32,
    channels: [c_int; LANES],
    working_dirty: c_int,
    off_min: u64,
}

const _: () = assert!(core::mem::size_of::<Runner>() == 432);
#[cfg(target_pointer_width = "64")]
const _: () = assert!(core::mem::size_of::<Sequencer>() == 7664);
#[cfg(target_pointer_width = "32")]
const _: () = assert!(core::mem::size_of::<Sequencer>() == 7632);

fn bound(value: c_int) -> c_int {
    value.clamp(0, MAX_MS)
}

/// Accumulates relative locks on the working channel within edit bounds.
fn lock(sound: &mut SoundSettings, value: TileValue) {
    if value.lock_mask & LOCK_ATTACK != 0 {
        sound.attack = bound(sound.attack + value.attack);
    }
    if value.lock_mask & LOCK_RELEASE != 0 {
        sound.release = bound(sound.release + value.release);
    }
}

/// Consumes exactly one deterministic draw per probability visit.
fn random_next(s: &mut Sequencer) -> u32 {
    let mut x = s.random;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    s.random = x;
    x
}

fn precedes(score: &Score, a: c_int, b: c_int) -> bool {
    let x = &score.lanes[a as usize];
    let y = &score.lanes[b as usize];
    (x.y, x.x, a) < (y.y, y.x, b)
}

fn runner_start(r: &mut Runner, lane: c_int, at: u64) {
    r.active = 1;
    r.origin = lane;
    r.lane = lane;
    r.step = 0;
    r.playing_lane = lane;
    r.playing_step = -1;
    r.lap = 0;
    r.duration = 0;
    r.next = at;
    r.held_count = 0;
}

/// Sorts runner seats without moving their held locks or cursors.
fn order_runners(s: &mut Sequencer) {
    // SAFETY: Start and resync retain a live immutable score snapshot.
    let score = unsafe { &*s.score };
    s.count = 0;
    for i in 0..LANES {
        if s.runners[i].active != 0 {
            let mut n = s.count as usize;
            s.count += 1;
            while n != 0
                && precedes(
                    score,
                    s.runners[i].origin,
                    s.runners[s.order[n - 1] as usize].origin,
                )
            {
                s.order[n] = s.order[n - 1];
                n -= 1;
            }
            s.order[n] = i as c_int;
        }
    }
    s.master = if s.count != 0 { s.order[0] } else { -1 };
    for i in 0..s.count as usize {
        let index = s.order[i] as usize;
        if score.lanes[s.runners[index].origin as usize].channel == 0 {
            s.master = index as c_int;
            break;
        }
    }
}

/// Starts traversal from a caller-owned score and a copied C sink.
///
/// # Safety
/// Both pointers must be valid, nonoverlapping and kept alive during service.
#[no_mangle]
pub unsafe extern "C" fn rust_sequencer_start(
    s: *mut Sequencer,
    score: *const Score,
    sink: *const NoteSink,
    now: u64,
) {
    // SAFETY: The caller excludes service and supplies writable state.
    let s = unsafe {
        core::ptr::write_bytes(s, 0, 1);
        &mut *s
    };
    s.score = score;
    // SAFETY: The C adapter owns a valid sink value for this call.
    s.sink = unsafe { *sink };
    s.playing = 1;
    s.random = 0x6d2b79f5;
    s.off_min = u64::MAX;
    // SAFETY: The snapshot is immutable while start and service use it.
    let score = unsafe { &*score };
    s.step_ticks = 240 * HZ / score.bpm as u32;
    for i in 0..CHANNELS {
        s.working[i] = score.sounds[i];
    }
    for i in 0..LANES {
        if score.lanes[i].active != 0 && score.lanes[i].source == 0 {
            let mut n = s.count as usize;
            s.count += 1;
            while n != 0 && precedes(score, i as c_int, s.runners[n - 1].origin)
            {
                s.runners[n] = s.runners[n - 1];
                s.channels[n] = s.channels[n - 1];
                n -= 1;
            }
            runner_start(&mut s.runners[n], i as c_int, now);
            s.channels[n] = score.lanes[i].channel;
        }
    }
    order_runners(s);
}

/// Distinguishes a reused lane slot from the prior runner's origin.
fn same_lane(old: &Score, score: &Score, lane: c_int) -> bool {
    let lane = lane as usize;
    score.lanes[lane].active != 0
        && old.lane_generation[lane] == score.lane_generation[lane]
}

/// Reconciles only runner seats at a complete slice boundary.
///
/// # Safety
/// `s` and `score` must be live, nonoverlapping and serialized with service.
#[no_mangle]
pub unsafe extern "C" fn sequencer_resync(
    s: *mut Sequencer,
    score: *const Score,
    now: u64,
) -> c_int {
    // SAFETY: The caller serializes publication and owns all three objects.
    let s = unsafe { &mut *s };
    if s.slicing != 0 || !s.replacement.is_null() {
        return 0;
    }
    // SAFETY: Both score snapshots remain immutable through publication.
    let old = unsafe { &*s.score };
    let new_score = unsafe { &*score };
    // Held locks are validated lazily under the tile budget. Publication
    // scans only the 16 runner seats, without copying a runner's full hold.
    for (i, r) in s.runners.iter_mut().enumerate() {
        if r.active == 0 {
            continue;
        }
        if !same_lane(old, new_score, r.origin)
            || new_score.lanes[r.origin as usize].source != 0
        {
            r.active = 0;
            continue;
        }
        s.channels[i] = new_score.lanes[r.origin as usize].channel;
        if !same_lane(old, new_score, r.lane) {
            r.lane = r.origin;
            r.step = 0;
        }
        if r.step >= new_score.lanes[r.lane as usize].length {
            r.step = 0;
        }
        if !same_lane(old, new_score, r.playing_lane)
            || r.playing_step >= new_score.lanes[r.playing_lane as usize].length
        {
            r.playing_lane = r.origin;
            r.playing_step = -1;
            r.held_count = 0;
        }
    }
    for (lane, incoming) in new_score.lanes.iter().enumerate() {
        if incoming.active != 0 && incoming.source == 0 {
            let mut found = false;
            let mut free_slot = None;
            for (i, r) in s.runners.iter().enumerate() {
                if r.active != 0 && r.origin == lane as c_int {
                    found = true;
                }
                if r.active == 0 && free_slot.is_none() {
                    free_slot = Some(i);
                }
            }
            if !found {
                let slot = free_slot.expect("regular lane has a runner seat");
                runner_start(&mut s.runners[slot], lane as c_int, u64::MAX);
                s.channels[slot] = incoming.channel;
            }
        }
    }
    s.score = score;
    s.step_ticks = 240 * HZ / new_score.bpm as u32;
    s.working_dirty = 1;
    order_runners(s);
    // A new master cannot wait for its own lap, including the first lane
    // inserted into an otherwise empty playing score.
    if s.count != 0 {
        let master = &mut s.runners[s.master as usize];
        if master.next == u64::MAX {
            master.next = now;
        }
    }
    1
}

/// Arms a prepared state for adoption at the outgoing lap seam.
///
/// # Safety
/// Both states must be distinct, live and serialized with service.
#[no_mangle]
pub unsafe extern "C" fn sequencer_replace(
    s: *mut Sequencer,
    prepared: *mut Sequencer,
    now: u64,
) -> c_int {
    // SAFETY: The caller supplies a live sequencer outside service.
    let state = unsafe { &mut *s };
    if !state.replacement.is_null() || prepared.is_null() || prepared == s {
        return 0;
    }
    state.replacement = prepared;
    state.replacement_at = if state.playing == 0 || state.count == 0 {
        now
    } else {
        u64::MAX
    };
    1
}

/// Moves only bounded gate and counter state at the handoff seam.
fn adopt(s: &mut Sequencer, at: u64) -> *mut Sequencer {
    // SAFETY: Replacement is a distinct prepared state owned by the caller.
    let next = unsafe { &mut *s.replacement };
    next.sink = s.sink;
    next.playing = s.playing;
    // Typed copies avoid the SDK's bytewise memcpy on the interrupt seam.
    for i in 0..VOICES {
        next.offs[i] = s.offs[i];
    }
    next.off_min = s.off_min;
    for i in 0..next.count as usize {
        next.runners[next.order[i] as usize].next = at;
    }
    next.skipped = s.skipped;
    next.overloads = s.overloads;
    s.replacement = core::ptr::null_mut();
    next
}

/// Stops outstanding notes and returns the state owning playback.
///
/// # Safety
/// `s` must be live and serialized with service; sink context must stay live.
#[no_mangle]
pub unsafe extern "C" fn sequencer_stop(
    s: *mut Sequencer,
    now: u64,
) -> *mut Sequencer {
    // SAFETY: The caller excludes concurrent service.
    let state = unsafe { &mut *s };
    state.playing = 0;
    // Clearing all gate storage matches C memset, including padding.
    // SAFETY: The entire field is writable and contains no owning pointers.
    unsafe { core::ptr::write_bytes(&mut state.offs, 0, 1) };
    state.off_min = u64::MAX;
    if let Some(stop) = state.sink.stop {
        // SAFETY: The caller keeps the sink context and callback live.
        unsafe { stop(state.sink.context, now) };
    }
    if state.replacement.is_null() {
        s
    } else {
        adopt(state, now)
    }
}

/// Drains due gate-offs in deadline order, grouping chord deadlines.
fn offs_until(s: &mut Sequencer, now: u64) {
    if s.off_min > now {
        return;
    }
    loop {
        let mut first = None;
        let mut earliest = u64::MAX;
        for i in 0..VOICES {
            if s.offs[i].token != 0 {
                earliest = earliest.min(s.offs[i].at);
                if s.offs[i].at <= now
                    && first.is_none_or(|f: usize| s.offs[i].at < s.offs[f].at)
                {
                    first = Some(i);
                }
            }
        }
        let Some(first) = first else {
            s.off_min = earliest;
            return;
        };
        let at = s.offs[first].at;
        for gate in &mut s.offs {
            if gate.token != 0 && gate.at == at {
                let token = gate.token;
                gate.token = 0;
                // SAFETY: A live sink has an off callback and context.
                unsafe {
                    s.sink.off.expect("note sink off callback")(
                        s.sink.context,
                        at,
                        token,
                    );
                }
            }
        }
    }
}

/// Applies a visited tile at the slice deadline, including gates and locks.
fn tile_event(s: &mut Sequencer, index: usize, tile: u16, now: u64) {
    // SAFETY: The active snapshot remains immutable during service.
    let score = unsafe { &*s.score };
    let value = score.tiles[usize::from(tile)].value;
    match value.kind {
        CYCLE => {
            if value.pattern
                & (1_u32 << (s.runners[index].lap % value.period as u32))
                == 0
            {
                s.cursor = 0;
            }
        }
        PROBABILITY => {
            let draw = random_next(s);
            if value.chance == 0
                || (value.chance < 100 && draw % 100 >= value.chance as u32)
            {
                s.cursor = 0;
            }
        }
        RELATIVE => {
            let channel = s.channels[index] as usize;
            lock(&mut s.working[channel], value);
            s.working_dirty = 1;
            let r = &mut s.runners[index];
            let count = r.held_count as usize;
            r.held[count] = tile;
            r.held_generation[count] = score.tile_generation[usize::from(tile)];
            r.held_count += 1;
        }
        JUMP => s.jump = score.tiles[usize::from(tile)].branch,
        NOTE => {
            if now.wrapping_sub(s.slice_at) > u64::from(HZ / 1000) {
                s.skipped += 1;
                return;
            }
            let sound = &s.working[s.channels[index] as usize];
            // SAFETY: The callback and sound stay live through this call.
            let token = unsafe {
                s.sink.on.expect("note sink requires on callback")(
                    s.sink.context,
                    s.slice_at,
                    value.pitch,
                    sound,
                )
            };
            if token != 0 {
                let at = s.slice_at
                    + u64::from(s.runners[index].duration / 20)
                        * value.length as u64;
                s.offs[(token & 31) as usize] = NoteOff { at, token };
                s.off_min = s.off_min.min(at);
            }
        }
        _ => {}
    }
}

/// Resumes a split slice without repeating gates or random draws.
fn slice(s: &mut Sequencer, now: u64, budget: &mut c_int) -> bool {
    while s.runner_index < s.count {
        let index = s.order[s.runner_index as usize] as usize;
        if s.visiting == 0 {
            s.visiting = 1;
            s.held_index = 0;
            s.jump = -1;
            if s.runners[index].next == s.slice_at {
                // SAFETY: The active snapshot is immutable during service.
                let score = unsafe { &*s.score };
                let r = &mut s.runners[index];
                r.held_count = 0;
                r.playing_lane = r.lane;
                r.playing_step = r.step;
                // Tempo edits affect the step beginning now, never a prior
                // deadline or an already scheduled gate.
                r.duration = s.step_ticks
                    / score.lanes[r.origin as usize].division as u32;
                s.cursor = score.lanes[r.lane as usize].tiles[r.step as usize];
            }
        }
        if s.runners[index].next == s.slice_at {
            while s.cursor != 0 {
                if *budget == 0 {
                    return false;
                }
                *budget -= 1;
                let tile = s.cursor;
                // SAFETY: The active snapshot is immutable during service.
                s.cursor = unsafe { (*s.score).tiles[usize::from(tile)].next };
                tile_event(s, index, tile, now);
            }
            // SAFETY: The active snapshot is immutable during service.
            let score = unsafe { &*s.score };
            if s.jump >= 0 {
                s.runners[index].lane = s.jump;
                s.runners[index].step = 0;
            } else {
                let r = &mut s.runners[index];
                r.step += 1;
                if r.step == score.lanes[r.lane as usize].length {
                    r.lane = r.origin;
                    r.step = 0;
                    r.lap += 1;
                    if index == s.master as usize {
                        let seam = r.next + u64::from(r.duration);
                        if !s.replacement.is_null()
                            && s.replacement_at == u64::MAX
                        {
                            s.replacement_at = seam;
                        }
                        for i in 0..s.count as usize {
                            let pending = &mut s.runners[s.order[i] as usize];
                            if pending.next == u64::MAX {
                                pending.next = seam;
                            }
                        }
                    }
                }
            }
            let r = &mut s.runners[index];
            r.next = r.next.wrapping_add(u64::from(r.duration));
        } else {
            while s.held_index < s.runners[index].held_count {
                if *budget == 0 {
                    return false;
                }
                *budget -= 1;
                let held = s.held_index as usize;
                s.held_index += 1;
                let tile = usize::from(s.runners[index].held[held]);
                // SAFETY: The active snapshot is immutable during service.
                let score = unsafe { &*s.score };
                if s.runners[index].held_generation[held]
                    == score.tile_generation[tile]
                    && score.tiles[tile].value.kind == RELATIVE
                {
                    let channel = s.channels[index] as usize;
                    lock(&mut s.working[channel], score.tiles[tile].value);
                    s.working_dirty = 1;
                }
            }
        }
        s.visiting = 0;
        s.runner_index += 1;
    }
    s.slicing = 0;
    true
}

/// Services bounded due slices and returns the active state after handoff.
///
/// # Safety
/// The state, score and sink must remain live with all access serialized.
#[no_mangle]
pub unsafe extern "C" fn sequencer_service(
    s: *mut Sequencer,
    now: u64,
) -> *mut Sequencer {
    // SAFETY: The timer or caller owns exclusive service access.
    let mut state = unsafe { &mut *s };
    let mut work = 0;
    let mut budget = TILE_BUDGET;
    let mut incoming_at = None;
    if state.playing == 0 && !state.replacement.is_null() {
        // SAFETY: adopt returns the distinct prepared state.
        state = unsafe { &mut *adopt(state, now) };
    }
    if state.playing != 0 {
        loop {
            if state.slicing == 0 {
                let at = if let Some(at) = incoming_at.take() {
                    at
                } else {
                    let mut at = u64::MAX;
                    for i in 0..state.count as usize {
                        at =
                            at.min(state.runners[state.order[i] as usize].next);
                    }
                    at
                };
                // The split outgoing slice completes before the lap seam;
                // takeover precedes selecting a new slice even when late.
                if !state.replacement.is_null()
                    && state.replacement_at <= now
                    && at >= state.replacement_at
                {
                    let seam = state.replacement_at;
                    // SAFETY: adopt returns the distinct prepared state.
                    state = unsafe { &mut *adopt(state, seam) };
                    // adopt assigns every incoming runner this same deadline.
                    incoming_at =
                        Some(if state.count != 0 { seam } else { u64::MAX });
                    continue;
                }
                if at > now {
                    break;
                }
                if work == SLICE_BUDGET {
                    state.overloads += 1;
                    break;
                }
                work += 1;
                offs_until(state, at);
                // Locks and publication invalidate the working bank. Clean
                // note-only slices reuse it without a timer-path score copy.
                if state.working_dirty != 0 {
                    // SAFETY: The active snapshot is immutable during service.
                    let score = unsafe { &*state.score };
                    for i in 0..CHANNELS {
                        state.working[i] = score.sounds[i];
                    }
                    state.working_dirty = 0;
                }
                state.slice_at = at;
                state.slicing = 1;
                state.runner_index = 0;
                state.visiting = 0;
            }
            // Dense stacks resume from their exact cursor on later calls;
            // stale note-ons are dropped instead of bursting after overload.
            if !slice(state, now, &mut budget) {
                state.overloads += 1;
                break;
            }
        }
    }
    offs_until(state, now);
    if let Some(advance) = state.sink.advance {
        // SAFETY: The caller keeps the callback and its context live.
        unsafe { advance(state.sink.context, now) };
    }
    state
}
