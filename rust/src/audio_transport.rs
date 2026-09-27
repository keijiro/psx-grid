//! Coordinates PlayStation playback and score publication.
//!
//! The main thread prepares immutable snapshots; the timer callback adopts
//! them at sequencer boundaries. C owns the SPU and timer hardware.

// Implementation notes:
// Keep shared indices and flags in fixed storage. The main thread copies only
// a spare snapshot while the timer is live, then publishes it under the
// platform critical section. Raw pointers avoid aliasing the whole transport
// across an interrupt that accesses a different field.

use core::ffi::{c_int, c_void};
use core::mem::MaybeUninit;
use core::ptr;
use core::sync::atomic::{compiler_fence, Ordering};

use crate::audio::{
    audio_init, audio_late_starts, audio_restart, audio_steals,
    audio_voice_modulation_start, rust_audio_advance, rust_audio_off,
    rust_audio_on, rust_audio_stop, Audio,
};
use crate::score::{Score, SoundSettings};
use crate::sequencer::{
    sequencer_replace, sequencer_resync, sequencer_service, sequencer_start,
    sequencer_stop, NoteSink, Sequencer,
};

const REPLACE_IDLE: c_int = 0;
const REPLACE_PREPARING: c_int = 1;
const REPLACE_WAITING: c_int = 2;
const REPLACE_ADOPTED: c_int = 3;

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

/// Copies a score with aligned words to avoid the SDK's bytewise `memcpy`.
unsafe fn copy_score(dst: *mut Score, src: *const Score) {
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
    }
}

fn sink(audio: *mut Audio) -> NoteSink {
    NoteSink {
        context: audio.cast(),
        on: Some(rust_audio_on),
        off: Some(rust_audio_off),
        stop: Some(rust_audio_stop),
        advance: Some(rust_audio_advance),
    }
}

/// Records a full replacement after the sequencer returns its new owner.
unsafe fn replacement_adopted(t: *mut Transport, next: *mut Sequencer) {
    // SAFETY: The timer or excluded main thread owns the transport handoff.
    unsafe {
        if next == load!(t, seq) {
            return;
        }
        store!(t, seq, next);
        store!(t, active_snapshot, load!(t, replacement_snapshot));
        store!(
            t,
            published_revision,
            (*snapshot(load!(t, active_snapshot))).revision
        );
        store!(t, replacement_state, REPLACE_ADOPTED);
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

/// Services publication, playback, and voice diagnostics from the timer.
///
/// # Safety
/// The hardware timer calls this only after initialization, without reentry.
#[no_mangle]
pub unsafe extern "C" fn audio_transport_service(now: u64) {
    let t = transport();
    // SAFETY: This timer callback owns active playback and published banks.
    unsafe {
        if load!(t, enabled) != 0 {
            let pending = load!(t, pending_snapshot);
            if pending >= 0
                && sequencer_resync(load!(t, seq), snapshot(pending), now) != 0
            {
                store!(t, active_snapshot, pending);
                store!(t, published_revision, (*snapshot(pending)).revision);
                store!(t, pending_snapshot, -1);
            }
            replacement_adopted(t, sequencer_service(load!(t, seq), now));
            ptr::write_volatile(
                ptr::addr_of_mut!(audio_skipped_notes),
                (*load!(t, seq)).skipped + audio_late_starts(load!(t, audio)),
            );
            ptr::write_volatile(
                ptr::addr_of_mut!(audio_overloads),
                (*load!(t, seq)).overloads,
            );
        } else {
            // The main thread may be preparing this sequencer while stopped.
            rust_audio_advance(load!(t, audio).cast(), now);
        }
        ptr::write_volatile(ptr::addr_of_mut!(audio_control_time), now as u32);
        ptr::write_volatile(
            ptr::addr_of_mut!(audio_modulation_start),
            audio_voice_modulation_start(load!(t, audio), now) as u32,
        );
        ptr::write_volatile(
            ptr::addr_of_mut!(audio_voice_steals),
            audio_steals(load!(t, audio)),
        );
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
            let next = sequencer_stop(load!(t, seq), audio_hw_now());
            replacement_adopted(t, next);
            store!(t, enabled, 0);
        }
        store!(t, pending_snapshot, -1);
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
            audio_hw_reverb(score);
        }
        if connected == 0 || (start != 0 && load!(t, enabled) != 0) {
            audio_hw_enter();
            if load!(t, enabled) != 0 {
                let next = sequencer_stop(load!(t, seq), audio_hw_now());
                replacement_adopted(t, next);
                store!(t, enabled, 0);
            }
            store!(t, pending_snapshot, -1);
            audio_hw_exit();
        } else if start != 0 {
            if load!(t, replacement_state) != REPLACE_IDLE {
                return;
            }
            // The timer cannot observe this bank until publication.
            copy_score(snapshot(0), score);
            let note_sink = sink(load!(t, audio));
            sequencer_start(load!(t, seq), snapshot(0), &note_sink, 0);
            audio_hw_enter();
            // Register cleanup precedes the first dispatch deadline.
            audio_hw_restart();
            audio_restart(load!(t, audio));
            let now = audio_hw_now();
            for i in 0..(*load!(t, seq)).count as usize {
                (*load!(t, seq)).runners[i].next = now;
            }
            ptr::write_volatile(ptr::addr_of_mut!(audio_started), now as u32);
            ptr::write_volatile(ptr::addr_of_mut!(audio_dispatch_peak), 0);
            ptr::write_volatile(ptr::addr_of_mut!(audio_note_count), 0);
            ptr::write_volatile(ptr::addr_of_mut!(audio_first_note), 0);
            ptr::write_volatile(ptr::addr_of_mut!(audio_last_note), 0);
            store!(t, active_snapshot, 0);
            store!(t, pending_snapshot, -1);
            store!(t, published_revision, (*score).revision);
            store!(t, enabled, 1);
            audio_hw_exit();
        } else if load!(t, replacement_state) == REPLACE_IDLE
            && load!(t, enabled) != 0
            && load!(t, pending_snapshot) < 0
            && (*score).revision != load!(t, published_revision)
        {
            // Edits made while a bank is pending are copied on a later call.
            let spare = 1 - load!(t, active_snapshot);
            copy_score(snapshot(spare), score);
            audio_hw_enter();
            store!(t, pending_snapshot, spare);
            audio_hw_exit();
        }
    }
}

/// Returns whether the transport is currently playing.
#[no_mangle]
pub extern "C" fn audio_platform_playing() -> c_int {
    let t = transport();
    // SAFETY: The timer and main thread exchange this word atomically on PSX.
    unsafe { load!(t, enabled) }
}

/// Returns the revision currently owned by timer service.
#[no_mangle]
pub extern "C" fn audio_platform_revision() -> u32 {
    let t = transport();
    // SAFETY: The timer publishes this word only after adopting its score.
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
        copy_score(snapshot(load!(t, replacement_snapshot)), incoming);
        let prepared = if load!(t, seq) == state(0) {
            state(1)
        } else {
            state(0)
        };
        let note_sink = sink(load!(t, audio));
        sequencer_start(
            prepared,
            snapshot(load!(t, replacement_snapshot)),
            &note_sink,
            0,
        );
        audio_hw_enter();
        if load!(t, enabled) != 0 {
            sequencer_replace(load!(t, seq), prepared, audio_hw_now());
            store!(t, replacement_state, REPLACE_WAITING);
        } else {
            (*prepared).playing = 0;
            replacement_adopted(t, prepared);
        }
        audio_hw_exit();
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
        copy_score(score, snapshot(load!(t, active_snapshot)));
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
