//! Coordinates PlayStation playback and score publication.
//!
//! The main thread plans immutable score events ahead of the timer callback.
//! C owns the SPU and timer hardware.

// Implementation notes:
// A single producer publishes complete events through a fixed ring. The timer
// only consumes that ring and controls voices; sequencer and score banks remain
// main-thread owned. Raw pointers avoid aliasing the whole transport across an
// interrupt that accesses a different field.

use core::ffi::{c_int, c_void};
use core::mem::MaybeUninit;
use core::ptr;
use core::sync::atomic::{compiler_fence, Ordering};

use crate::audio::{
    audio_init, audio_late_starts, audio_restart, audio_steals,
    audio_voice_modulation_start, rust_audio_advance, rust_audio_on_gate,
    rust_audio_release_due, rust_audio_stop, Audio,
};
use crate::score::{Score, SoundSettings, LANES};
use crate::sequencer::{
    pending_until, publication_at, sequencer_replace, sequencer_resync,
    sequencer_service, sequencer_set_planner, sequencer_start, sequencer_stop,
    NoteSink, PlannerSink, Sequencer,
};

const REPLACE_IDLE: c_int = 0;
const REPLACE_PREPARING: c_int = 1;
const REPLACE_WAITING: c_int = 2;
const REPLACE_ADOPTED: c_int = 3;
const CLOCK_HZ: u64 = 4_233_600;
// The documented frame and score-copy gaps reach about 28 ms. Three or more
// frame periods leave room for ordinary render jitter before the next refill.
const LOOKAHEAD: u64 = CLOCK_HZ * 60 / 1000;
const EVENT_CAPACITY: u32 = 512;
const MIN_PLAN_FREE: u32 = 82;
const PLAN_CALL_BUDGET: usize = 24;
const DISPATCH_BUDGET: usize = 32;

const EVENT_NOTE: u32 = 1;
const EVENT_STEP: u32 = 2;
const EVENT_REVISION: u32 = 3;
const EVENT_REPLACEMENT: u32 = 4;

#[derive(Clone, Copy)]
struct Event {
    at: u64,
    gate_at: u64,
    until: u64,
    sound: SoundSettings,
    pitch: c_int,
    runner: c_int,
    lane: c_int,
    step: c_int,
    revision: u32,
    kind: u32,
}

#[derive(Clone, Copy)]
struct Playing {
    lane: c_int,
    step: c_int,
    until: u64,
}

#[repr(C)]
#[derive(Clone, Copy)]
pub(crate) struct AudioPlayhead {
    pub(crate) lane: c_int,
    pub(crate) step: c_int,
}

#[repr(C)]
/// Holds a frame-sized copy of timer-owned runner positions.
pub struct AudioPlayheads {
    pub(crate) revision: u32,
    pub(crate) count: c_int,
    pub(crate) items: [AudioPlayhead; crate::score::LANES],
}

/// Shared scalar state for score publication and sequencer handoff.
struct Transport {
    active_snapshot: c_int,
    pending_snapshot: c_int,
    seq: *mut Sequencer,
    replacement_state: c_int,
    replacement_snapshot: c_int,
    audio: *mut Audio,
    enabled: c_int,
    published_revision: u32,
    queued_revision: u32,
    epoch: u64,
    last_control: u64,
    filled_until: u64,
    filled_low: u32,
    planned_skips: u32,
    planned_overloads: u32,
    queue_drops: u32,
    underflowing: c_int,
}

// Interrupt-shared words use volatile access and compiler fences because the
// timer can preempt ordinary Rust code between hardware critical sections.
macro_rules! load {
    ($t:expr, $field:ident) => {{
        let field = ptr::addr_of!((*$t).$field);
        let value = ptr::read_volatile(field);
        compiler_fence(Ordering::Acquire);
        value
    }};
}

macro_rules! store {
    ($t:expr, $field:ident, $value:expr) => {{
        compiler_fence(Ordering::Release);
        ptr::write_volatile(ptr::addr_of_mut!((*$t).$field), $value)
    }};
}

// One console instance persists across card sessions and timer callbacks.
static mut TRANSPORT: MaybeUninit<Transport> = MaybeUninit::uninit();
// Keep large banks outside the scalar state so MIPS address formation never
// needs a large aggregate field offset in the interrupt path.
static mut SNAPSHOTS: MaybeUninit<[Score; 2]> = MaybeUninit::uninit();
static mut STATES: MaybeUninit<[Sequencer; 2]> = MaybeUninit::uninit();
static mut EVENTS: MaybeUninit<[Event; EVENT_CAPACITY as usize]> =
    MaybeUninit::uninit();
static mut EVENT_READ: u32 = 0;
static mut EVENT_WRITE: u32 = 0;
static mut PLAYING: [Playing; LANES] = [Playing {
    lane: 0,
    step: -1,
    until: 0,
}; LANES];

unsafe extern "C" {
    fn audio_hw_init();
    fn audio_hw_enter();
    fn audio_hw_exit();
    fn audio_hw_now() -> u64;
    fn audio_hw_time() -> u64;
    fn audio_hw_reverb(score: *const Score);
    fn audio_hw_restart();
    fn audio_hw_card_stop();
    fn audio_hw_card_resume(score: *const Score);
    fn audio_hw_start_voice(
        context: *mut c_void,
        slot: c_int,
        bank: c_int,
        sound: *const SoundSettings,
    );
    fn audio_hw_volume(context: *mut c_void, slot: c_int, a: c_int, b: c_int);
    fn audio_hw_pitch(context: *mut c_void, slot: c_int, value: c_int);
    fn audio_hw_flush(
        context: *mut c_void,
        starts: u32,
        stops: u32,
        sends: u32,
        dispatch_peak: u32,
    );
    fn audio_hw_ready(context: *mut c_void, slot: c_int) -> c_int;
    fn audio_hw_clock(context: *mut c_void) -> u64;

    static mut audio_dispatch_peak: u32;
    static mut audio_note_count: u32;
    static mut audio_first_note: u32;
    static mut audio_last_note: u32;
    static mut audio_started: u32;
    static mut audio_control_time: u32;
    static mut audio_modulation_start: u32;
    static mut audio_voice_steals: u32;
    static mut audio_skipped_notes: u32;
    static mut audio_overloads: u32;
    static mut audio_queue_underruns: u32;
}

fn transport() -> *mut Transport {
    // SAFETY: `MaybeUninit` has the same address as its initialized value.
    ptr::addr_of_mut!(TRANSPORT).cast::<Transport>()
}

fn snapshot(index: c_int) -> *mut Score {
    // SAFETY: Both banks are fixed storage and callers use only indices 0 or 1.
    unsafe {
        ptr::addr_of_mut!(SNAPSHOTS)
            .cast::<Score>()
            .add(index as usize)
    }
}

fn state(index: usize) -> *mut Sequencer {
    // SAFETY: The transport has exactly two fixed sequencer states.
    unsafe { ptr::addr_of_mut!(STATES).cast::<Sequencer>().add(index) }
}

/// Copies a score while keeping the old playback plan ahead of a live copy.
unsafe fn copy_score(dst: *mut Score, src: *const Score, t: *mut Transport) {
    const WORDS: usize = core::mem::size_of::<Score>() / 4;
    const _: () = assert!(core::mem::size_of::<Score>() % 4 == 0);
    let dst = dst.cast::<u32>();
    let src = src.cast::<u32>();
    for i in 0..WORDS {
        // SAFETY: Score storage is four-byte aligned, distinct, and live for
        // the full copy. Volatile words keep the compiler from using memcpy.
        unsafe {
            ptr::write_volatile(dst.add(i), ptr::read_volatile(src.add(i)))
        };
        if i % 4096 == 4095 && !t.is_null() {
            // The SDK's bytewise copy was replaced by aligned words, but an
            // entire score still outlasts the lookahead on Debug hardware.
            // Keep the source stable in this call and refill from the old
            // snapshot between chunks before publishing the new one.
            // SAFETY: Only the main thread copies scores or plans events.
            unsafe {
                if load!(t, enabled) != 0 {
                    let relative =
                        audio_hw_time().wrapping_sub(load!(t, epoch));
                    plan_to(t, relative + LOOKAHEAD);
                }
            }
        }
    }
}

fn sink() -> NoteSink {
    NoteSink {
        context: ptr::null_mut(),
        on: None,
        off: None,
        stop: None,
        advance: None,
    }
}

fn planner() -> PlannerSink {
    PlannerSink {
        context: ptr::null_mut(),
        note: Some(enqueue_note),
        step: Some(enqueue_step),
        revision: Some(enqueue_revision),
    }
}

/// Returns capacity left after the timer's last committed read.
unsafe fn queue_free() -> u32 {
    // SAFETY: Both indices are aligned words and only their owner writes each.
    unsafe {
        EVENT_CAPACITY
            - ptr::read_volatile(ptr::addr_of!(EVENT_WRITE))
                .wrapping_sub(ptr::read_volatile(ptr::addr_of!(EVENT_READ)))
    }
}

/// Publishes one complete event to the timer consumer.
unsafe fn queue_push(event: Event) -> bool {
    // SAFETY: The main thread alone writes slots and advances EVENT_WRITE.
    unsafe {
        let write = ptr::read_volatile(ptr::addr_of!(EVENT_WRITE));
        let read = ptr::read_volatile(ptr::addr_of!(EVENT_READ));
        if write.wrapping_sub(read) == EVENT_CAPACITY {
            return false;
        }
        let slot = ptr::addr_of_mut!(EVENTS)
            .cast::<Event>()
            .add((write % EVENT_CAPACITY) as usize);
        ptr::write(slot, event);
        compiler_fence(Ordering::Release);
        ptr::write_volatile(
            ptr::addr_of_mut!(EVENT_WRITE),
            write.wrapping_add(1),
        );
        true
    }
}

/// Borrows the oldest event only after its absolute score time is due.
unsafe fn queue_pop_due(now: u64) -> Option<*const Event> {
    // SAFETY: The timer alone reads slots and advances EVENT_READ.
    unsafe {
        let read = ptr::read_volatile(ptr::addr_of!(EVENT_READ));
        let write = ptr::read_volatile(ptr::addr_of!(EVENT_WRITE));
        if read == write {
            return None;
        }
        compiler_fence(Ordering::Acquire);
        let slot = ptr::addr_of!(EVENTS)
            .cast::<Event>()
            .add((read % EVENT_CAPACITY) as usize);
        if ptr::read_volatile(ptr::addr_of!((*slot).at)) > now {
            return None;
        }
        ptr::write_volatile(
            ptr::addr_of_mut!(EVENT_READ),
            read.wrapping_add(1),
        );
        // Main-thread production cannot resume until this interrupt returns.
        Some(slot)
    }
}

/// Discards planned events only while timer consumption is excluded.
unsafe fn queue_reset() {
    // SAFETY: The caller has disabled playback or masked its timer callback.
    unsafe {
        ptr::write_volatile(ptr::addr_of_mut!(EVENT_READ), 0);
        ptr::write_volatile(ptr::addr_of_mut!(EVENT_WRITE), 0);
        for playhead in &mut *ptr::addr_of_mut!(PLAYING) {
            playhead.step = -1;
            playhead.until = 0;
        }
    }
}

fn empty_event(at: u64, kind: u32) -> Event {
    // SAFETY: Every field is an integer or an integer-only sound aggregate.
    let mut event: Event = unsafe { core::mem::zeroed() };
    event.at = at;
    event.kind = kind;
    event
}

unsafe extern "C" fn enqueue_note(
    _context: *mut c_void,
    at: u64,
    gate_at: u64,
    pitch: c_int,
    sound: *const SoundSettings,
) {
    let t = transport();
    let mut event = empty_event(at, EVENT_NOTE);
    event.gate_at = gate_at;
    event.pitch = pitch;
    // SAFETY: The sequencer supplies a live sound pointer for this callback.
    unsafe {
        event.sound = *sound;
        if !queue_push(event) {
            store!(t, queue_drops, load!(t, queue_drops) + 1);
        }
    }
}

unsafe extern "C" fn enqueue_step(
    _context: *mut c_void,
    at: u64,
    runner: c_int,
    lane: c_int,
    step: c_int,
    until: u64,
) {
    let t = transport();
    let mut event = empty_event(at, EVENT_STEP);
    event.runner = runner;
    event.lane = lane;
    event.step = step;
    event.until = until;
    // SAFETY: Main-thread planning is the ring's only producer.
    unsafe {
        if !queue_push(event) {
            store!(t, queue_drops, load!(t, queue_drops) + 1);
        }
    }
}

unsafe extern "C" fn enqueue_revision(
    _context: *mut c_void,
    at: u64,
    revision: u32,
) {
    let t = transport();
    let mut event = empty_event(at, EVENT_REVISION);
    event.revision = revision;
    // SAFETY: Main-thread planning is the ring's only producer.
    unsafe {
        if load!(t, replacement_state) == REPLACE_WAITING {
            event.kind = EVENT_REPLACEMENT;
        }
        if !queue_push(event) {
            store!(t, queue_drops, load!(t, queue_drops) + 1);
        }
    }
}

/// Records a full replacement after the sequencer returns its new owner.
unsafe fn replacement_adopted(t: *mut Transport, next: *mut Sequencer) {
    // SAFETY: Only main-thread planning changes sequencer ownership.
    unsafe {
        if next == load!(t, seq) {
            return;
        }
        store!(t, seq, next);
        store!(t, active_snapshot, load!(t, replacement_snapshot));
        let revision = (*snapshot(load!(t, active_snapshot))).revision;
        store!(t, queued_revision, revision);
        if load!(t, enabled) == 0 {
            store!(t, published_revision, revision);
            store!(t, replacement_state, REPLACE_ADOPTED);
        }
    }
}

/// Completes a planned handoff whose audible marker was discarded by stop.
unsafe fn finish_stopped_replacement(t: *mut Transport) {
    // SAFETY: The caller excludes timer service and has stopped playback.
    unsafe {
        if load!(t, replacement_state) == REPLACE_WAITING {
            store!(t, published_revision, load!(t, queued_revision));
            store!(t, replacement_state, REPLACE_ADOPTED);
        }
    }
}

/// Extends the plan without ever exposing a partial score event to the timer.
unsafe fn plan_to(t: *mut Transport, target: u64) {
    // SAFETY: The main thread owns the sequencer and ring producer.
    unsafe {
        for _ in 0..PLAN_CALL_BUDGET {
            let seq = load!(t, seq);
            if !pending_until(&*seq, target) {
                break;
            }
            // A resumed partial slice can precede two new slices. Reserve
            // room for 48 step markers, 32 notes, and a revision marker.
            if queue_free() < MIN_PLAN_FREE {
                break;
            }
            replacement_adopted(t, sequencer_service(seq, target));
        }
        let seq = &*load!(t, seq);
        if !pending_until(seq, target) {
            store!(t, filled_until, target);
            store!(t, filled_low, target as u32);
        }
        store!(t, planned_skips, seq.skipped);
        store!(t, planned_overloads, seq.overloads);
    }
}

/// Initializes the fixed Rust transport before enabling the hardware timer.
#[no_mangle]
pub extern "C" fn audio_platform_init() {
    let t = transport();
    // SAFETY: Initialization runs once before the timer callback is installed.
    unsafe {
        ptr::write_bytes(t, 0, 1);
        ptr::write_bytes(ptr::addr_of_mut!(STATES), 0, 1);
        store!(t, pending_snapshot, -1);
        store!(t, seq, state(0));
        store!(
            t,
            audio,
            audio_init(
                ptr::null_mut(),
                Some(audio_hw_start_voice),
                Some(audio_hw_volume),
                Some(audio_hw_pitch),
                Some(audio_hw_flush),
                Some(audio_hw_ready),
                Some(audio_hw_clock),
            )
        );
        audio_hw_init();
    }
}

/// Returns the extended hardware clock with the timer interrupt excluded.
#[no_mangle]
pub extern "C" fn audio_platform_time() -> u64 {
    // SAFETY: The C backend serializes its wrapping clock accumulator.
    unsafe { audio_hw_time() }
}

/// Dispatches due events and controls active voices from the timer.
///
/// # Safety
/// The hardware timer calls this only after initialization, without reentry.
#[no_mangle]
pub unsafe extern "C" fn audio_transport_service(now: u64) {
    let t = transport();
    // SAFETY: The timer is the ring's only consumer and owns active voices.
    unsafe {
        if load!(t, enabled) != 0 {
            let epoch = load!(t, epoch);
            let audio = load!(t, audio);
            let relative = now.wrapping_sub(epoch);
            let mut last_note_at = None;
            let mut note_due = false;
            for _ in 0..DISPATCH_BUDGET {
                let Some(event) = queue_pop_due(relative) else {
                    break;
                };
                let event = &*event;
                match event.kind {
                    EVENT_NOTE => {
                        note_due = true;
                        if last_note_at != Some(event.at) {
                            rust_audio_release_due(
                                audio.cast(),
                                epoch + event.at,
                            );
                            last_note_at = Some(event.at);
                        }
                        rust_audio_on_gate(
                            audio.cast(),
                            epoch + event.at,
                            epoch + event.gate_at,
                            event.pitch,
                            &event.sound,
                        );
                    }
                    EVENT_STEP => {
                        let playhead = &mut (*ptr::addr_of_mut!(PLAYING))
                            [event.runner as usize];
                        playhead.lane = event.lane;
                        playhead.step = event.step;
                        playhead.until = event.until;
                    }
                    EVENT_REVISION => {
                        store!(t, published_revision, event.revision);
                    }
                    EVENT_REPLACEMENT => {
                        store!(t, published_revision, event.revision);
                        store!(t, replacement_state, REPLACE_ADOPTED);
                    }
                    _ => {}
                }
            }
            let gap =
                (relative as u32).wrapping_sub(load!(t, filled_low)) as i32;
            if gap > 0 {
                if load!(t, underflowing) == 0 {
                    store!(t, underflowing, 1);
                    let count = ptr::read_volatile(ptr::addr_of!(
                        audio_queue_underruns
                    ));
                    ptr::write_volatile(
                        ptr::addr_of_mut!(audio_queue_underruns),
                        count + 1,
                    );
                }
            } else {
                store!(t, underflowing, 0);
            }
            // Queue deadlines need the 0.25 ms timer cadence. Dense voice
            // control takes longer than one such period, so update envelopes
            // at 1 ms intervals unless a note needs an immediate key-on.
            if note_due
                || now.wrapping_sub(load!(t, last_control))
                    >= CLOCK_HZ / 1000
            {
                rust_audio_advance(audio.cast(), now);
                store!(t, last_control, now);
                ptr::write_volatile(
                    ptr::addr_of_mut!(audio_control_time),
                    now as u32,
                );
                ptr::write_volatile(
                    ptr::addr_of_mut!(audio_modulation_start),
                    audio_voice_modulation_start(audio, now) as u32,
                );
                ptr::write_volatile(
                    ptr::addr_of_mut!(audio_voice_steals),
                    audio_steals(audio),
                );
            }
            ptr::write_volatile(
                ptr::addr_of_mut!(audio_skipped_notes),
                load!(t, planned_skips)
                    + load!(t, queue_drops)
                    + audio_late_starts(audio),
            );
            ptr::write_volatile(
                ptr::addr_of_mut!(audio_overloads),
                load!(t, planned_overloads),
            );
        } else {
            // The main thread may be preparing this sequencer while stopped.
            if now.wrapping_sub(load!(t, last_control))
                >= CLOCK_HZ / 1000
            {
                let audio = load!(t, audio);
                rust_audio_advance(audio.cast(), now);
                store!(t, last_control, now);
                ptr::write_volatile(
                    ptr::addr_of_mut!(audio_control_time),
                    now as u32,
                );
                ptr::write_volatile(
                    ptr::addr_of_mut!(audio_modulation_start),
                    audio_voice_modulation_start(audio, now) as u32,
                );
                ptr::write_volatile(
                    ptr::addr_of_mut!(audio_voice_steals),
                    audio_steals(audio),
                );
            }
        }
    }
}

/// Stops playback and detaches the timer for BIOS memory-card service.
#[no_mangle]
pub extern "C" fn audio_platform_card_stop() {
    let t = transport();
    // SAFETY: The main thread excludes timer service for the whole handoff.
    unsafe {
        audio_hw_enter();
        if load!(t, enabled) != 0 {
            let now = audio_hw_now();
            store!(t, enabled, 0);
            let next = sequencer_stop(
                load!(t, seq),
                now.wrapping_sub(load!(t, epoch)),
            );
            replacement_adopted(t, next);
            finish_stopped_replacement(t);
            rust_audio_stop(load!(t, audio).cast(), now);
        }
        store!(t, pending_snapshot, -1);
        queue_reset();
        audio_hw_card_stop();
        store!(
            t,
            audio,
            audio_init(
                ptr::null_mut(),
                Some(audio_hw_start_voice),
                Some(audio_hw_volume),
                Some(audio_hw_pitch),
                Some(audio_hw_flush),
                Some(audio_hw_ready),
                Some(audio_hw_clock),
            )
        );
        audio_hw_exit();
    }
}

/// Restores hardware timing after BIOS memory-card service.
///
/// # Safety
/// `score` must point to a valid score for this call.
#[no_mangle]
pub unsafe extern "C" fn audio_platform_card_resume(score: *const Score) {
    // SAFETY: The BIOS session is complete and the score remains live.
    unsafe { audio_hw_card_resume(score) };
}

/// Publishes edits and applies a main-thread transport button edge.
///
/// # Safety
/// `score` must remain valid and immutable during this call.
#[no_mangle]
pub unsafe extern "C" fn audio_platform_update(
    score: *const Score,
    connected: c_int,
    start: c_int,
) {
    let t = transport();
    // SAFETY: The main thread owns unpublished banks and excludes service for
    // each shared state change. The caller keeps the source score immutable.
    unsafe {
        if load!(t, replacement_state) == REPLACE_IDLE {
            if connected != 0 && start == 0 && load!(t, enabled) != 0 {
                audio_platform_fill();
            }
            audio_hw_reverb(score);
        }
        if connected == 0 || (start != 0 && load!(t, enabled) != 0) {
            audio_hw_enter();
            if load!(t, enabled) != 0 {
                let now = audio_hw_now();
                store!(t, enabled, 0);
                let next = sequencer_stop(
                    load!(t, seq),
                    now.wrapping_sub(load!(t, epoch)),
                );
                replacement_adopted(t, next);
                finish_stopped_replacement(t);
                rust_audio_stop(load!(t, audio).cast(), now);
            }
            store!(t, pending_snapshot, -1);
            queue_reset();
            audio_hw_exit();
        } else if start != 0 {
            if load!(t, replacement_state) != REPLACE_IDLE {
                return;
            }
            // An abstract zero origin lets planning finish before the clock
            // epoch is fixed, so even a slow initial copy cannot age notes.
            copy_score(snapshot(0), score, ptr::null_mut());
            let note_sink = sink();
            sequencer_start(load!(t, seq), snapshot(0), &note_sink, 0);
            let writer = planner();
            sequencer_set_planner(load!(t, seq), &writer);
            audio_hw_enter();
            queue_reset();
            audio_hw_exit();
            store!(t, filled_until, 0);
            store!(t, filled_low, 0);
            store!(t, queue_drops, 0);
            store!(t, underflowing, 0);
            plan_to(t, LOOKAHEAD);
            audio_hw_enter();
            audio_hw_restart();
            audio_restart(load!(t, audio));
            let now = audio_hw_now();
            ptr::write_volatile(ptr::addr_of_mut!(audio_started), now as u32);
            ptr::write_volatile(ptr::addr_of_mut!(audio_dispatch_peak), 0);
            ptr::write_volatile(ptr::addr_of_mut!(audio_note_count), 0);
            ptr::write_volatile(ptr::addr_of_mut!(audio_first_note), 0);
            ptr::write_volatile(ptr::addr_of_mut!(audio_last_note), 0);
            ptr::write_volatile(ptr::addr_of_mut!(audio_queue_underruns), 0);
            store!(t, active_snapshot, 0);
            store!(t, pending_snapshot, -1);
            store!(t, published_revision, (*score).revision);
            store!(t, queued_revision, (*score).revision);
            store!(t, epoch, now);
            store!(t, last_control, now);
            store!(t, enabled, 1);
            audio_hw_exit();
        } else if load!(t, replacement_state) == REPLACE_IDLE
            && load!(t, enabled) != 0
            && load!(t, pending_snapshot) < 0
            && (*score).revision != load!(t, queued_revision)
        {
            // Edits made while a bank is pending are copied on a later call.
            let spare = 1 - load!(t, active_snapshot);
            copy_score(snapshot(spare), score, t);
            store!(t, pending_snapshot, spare);
        }
    }
}

/// Refills future events after input updates and before the next frame wait.
#[no_mangle]
pub extern "C" fn audio_platform_fill() {
    let t = transport();
    // SAFETY: Only the main thread owns snapshots, the planner, and ring writes.
    unsafe {
        if load!(t, enabled) == 0 {
            return;
        }
        let pending = load!(t, pending_snapshot);
        if pending >= 0 && queue_free() != 0 {
            let seq = &*load!(t, seq);
            if let Some(at) = publication_at(seq, load!(t, filled_until)) {
                if sequencer_resync(load!(t, seq), snapshot(pending), at) != 0 {
                    store!(t, active_snapshot, pending);
                    store!(t, queued_revision, (*snapshot(pending)).revision);
                    store!(t, pending_snapshot, -1);
                }
            }
        }
        let relative = audio_hw_time().wrapping_sub(load!(t, epoch));
        plan_to(t, relative + LOOKAHEAD);
    }
}

/// Returns whether the transport is currently playing.
#[no_mangle]
pub extern "C" fn audio_platform_playing() -> c_int {
    let t = transport();
    // SAFETY: The timer and main thread exchange this word atomically on PSX.
    unsafe { load!(t, enabled) }
}

/// Copies the currently audible runner positions for one rendered frame.
///
/// # Safety
/// `out` must point to writable `AudioPlayheads` storage.
#[no_mangle]
pub unsafe extern "C" fn audio_platform_playheads(out: *mut AudioPlayheads) {
    let t = transport();
    // SAFETY: The caller supplies writable storage. Initialize even unused
    // seats because rendering borrows the complete C layout as a Rust value.
    unsafe { ptr::write_bytes(out, 0, 1) };
    // SAFETY: Excluding timer service keeps consumed positions consistent.
    unsafe {
        audio_hw_enter();
        if load!(t, enabled) != 0 {
            (*out).revision = load!(t, published_revision);
            let now = audio_hw_now().wrapping_sub(load!(t, epoch));
            for playing in &*ptr::addr_of!(PLAYING) {
                if playing.step >= 0 && now < playing.until {
                    let count = (*out).count as usize;
                    (*out).items[count] = AudioPlayhead {
                        lane: playing.lane,
                        step: playing.step,
                    };
                    (*out).count += 1;
                }
            }
        }
        audio_hw_exit();
    }
}

/// Returns the revision whose planned events reached the timer.
#[no_mangle]
pub extern "C" fn audio_platform_revision() -> u32 {
    let t = transport();
    // SAFETY: The timer publishes this aligned word at a revision marker.
    unsafe { load!(t, published_revision) }
}

/// Prepares a full score replacement in the inactive bank.
///
/// # Safety
/// `incoming` must remain valid and immutable during this call.
#[no_mangle]
pub unsafe extern "C" fn audio_platform_replace(
    incoming: *const Score,
) -> c_int {
    let t = transport();
    // SAFETY: The main thread locks publication before preparing the bank.
    unsafe {
        audio_hw_enter();
        if load!(t, replacement_state) != REPLACE_IDLE {
            audio_hw_exit();
            return 0;
        }
        store!(t, replacement_state, REPLACE_PREPARING);
        store!(t, pending_snapshot, -1);
        store!(t, replacement_snapshot, 1 - load!(t, active_snapshot));
        audio_hw_exit();
        copy_score(snapshot(load!(t, replacement_snapshot)), incoming, t);
        let prepared = if load!(t, seq) == state(0) {
            state(1)
        } else {
            state(0)
        };
        let note_sink = sink();
        sequencer_start(
            prepared,
            snapshot(load!(t, replacement_snapshot)),
            &note_sink,
            0,
        );
        let writer = planner();
        sequencer_set_planner(prepared, &writer);
        if load!(t, enabled) != 0 {
            sequencer_replace(load!(t, seq), prepared, load!(t, filled_until));
            store!(t, replacement_state, REPLACE_WAITING);
        } else {
            (*prepared).playing = 0;
            replacement_adopted(t, prepared);
        }
        1
    }
}

/// Copies an adopted replacement into the editable score once.
///
/// # Safety
/// `score` must point to a writable score distinct from transport storage.
#[no_mangle]
pub unsafe extern "C" fn audio_platform_take_replacement(
    score: *mut Score,
) -> c_int {
    let t = transport();
    // SAFETY: The adopted bank remains locked through the editor copy.
    unsafe {
        if load!(t, replacement_state) != REPLACE_ADOPTED {
            return 0;
        }
        copy_score(score, snapshot(load!(t, active_snapshot)), t);
        audio_hw_reverb(score);
        audio_hw_enter();
        store!(t, replacement_state, REPLACE_IDLE);
        audio_hw_exit();
        1
    }
}

/// Returns whether a full replacement is in progress.
#[no_mangle]
pub extern "C" fn audio_platform_replacing() -> c_int {
    let t = transport();
    // SAFETY: Both threads exchange this word under the publication protocol.
    unsafe { (load!(t, replacement_state) != REPLACE_IDLE) as c_int }
}
