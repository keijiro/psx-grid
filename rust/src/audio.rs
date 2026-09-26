//! Allocates logical SPU voice pairs and evaluates their control envelopes.
//!
//! The caller owns fixed voice storage and supplies register callbacks. The
//! timer service serializes every callback without allocation.

// Implementation notes:
// Keep divisions in 32 bits on MIPS. The C ABI adapter owns aggregate values
// passed to or returned from C callbacks.

use core::ffi::{c_int, c_void};

use crate::audio_tables::{BANKS, PITCH, SNAP};
use crate::score::SoundSettings;

const VOICES: usize = 12;
const IDLE_MASK: u32 = (1 << VOICES) - 1;
const LEVEL: u32 = 0x3fff;
const HZ: u32 = 4_233_600;

type Start = unsafe extern "C" fn(*mut c_void, c_int, c_int, SoundSettings);
type Volume = unsafe extern "C" fn(*mut c_void, c_int, c_int, c_int);
type Pitch = unsafe extern "C" fn(*mut c_void, c_int, c_int);
type Flush = unsafe extern "C" fn(*mut c_void, u32, u32);
type Ready = unsafe extern "C" fn(*mut c_void, c_int) -> c_int;

/// C register callbacks mirrored for the fixed voice driver.
#[repr(C)]
pub(crate) struct AudioDriver {
    context: *mut c_void,
    start: Option<Start>,
    volume: Option<Volume>,
    pitch: Option<Pitch>,
    flush: Option<Flush>,
    ready: Option<Ready>,
}

#[repr(C)]
struct AudioVoice {
    start: u64,
    release_at: u64,
    end: u64,
    modulation_start: u64,
    generation: u32,
    sound: SoundSettings,
    active: c_int,
    releasing: c_int,
    waiting: c_int,
    level: c_int,
    release_level: c_int,
    pitch: c_int,
    bank: c_int,
    base_pitch: c_int,
}

#[repr(C)]
pub(crate) struct Audio {
    voices: [AudioVoice; VOICES],
    driver: AudioDriver,
    starts: u32,
    stops: u32,
    steals: u32,
    idle_mask: u32,
    allocation_time: u64,
}

const _: () = assert!(core::mem::size_of::<AudioVoice>() == 104);
const _: () = assert!(core::mem::offset_of!(Audio, driver) == 1248);

unsafe extern "C" {
    fn audio_driver_start(
        driver: *const AudioDriver,
        slot: c_int,
        bank: c_int,
        sound: *const SoundSettings,
    );
}

/// Converts a supported millisecond duration to sequencer ticks.
#[no_mangle]
pub extern "C" fn audio_ms(ms: c_int) -> u64 {
    let ms = ms as u32;
    (ms * (HZ / 1000) + ms * (HZ % 1000) / 1000) as u64
}

/// Keeps both sides of a full-scale ramp within a 32-bit product.
fn gain_shift(span: u32) -> u32 {
    // Short envelopes retain tick precision; long ones share coarser steps.
    let limit = u32::MAX / (LEVEL + 1);
    if span <= limit {
        0
    } else if span >> 4 <= limit {
        4
    } else if span >> 6 <= limit {
        6
    } else if span >> 8 <= limit {
        8
    } else {
        9
    }
}

/// Keeps a nonzero release audible until its exact deadline.
fn level_at(v: &AudioVoice, now: u64) -> c_int {
    if v.active == 0 {
        return 0;
    }
    if v.releasing != 0 {
        if now >= v.end {
            return 0;
        }
        let mut span = (v.end - v.release_at) as u32;
        let shift = gain_shift(span);
        let left = (v.end - now) as u32 >> shift;
        span >>= shift;
        let level = if span != 0 {
            (left * v.release_level as u32 + span - 1) / span
        } else {
            v.release_level as u32
        };
        return if level != 0 {
            level as c_int
        } else {
            c_int::from(v.release_level != 0)
        };
    }

    let attack = audio_ms(v.sound.attack) as u32;
    let elapsed = now.wrapping_sub(v.start);
    if attack == 0 || elapsed >= attack as u64 {
        return LEVEL as c_int;
    }
    let shift = gain_shift(attack);
    let level =
        ((elapsed as u32 >> shift) * LEVEL / (attack >> shift)) as c_int;
    level.min(LEVEL as c_int - 1)
}

/// Evaluates signed sweep before the selected bank's register lookup.
fn pitch_at(v: &AudioVoice, now: u64) -> c_int {
    let elapsed = if v.waiting != 0 {
        0
    } else {
        now.wrapping_sub(v.modulation_start)
    };
    let duration = audio_ms(v.sound.decay) as u32;
    if v.sound.sweep == 0 || duration == 0 || elapsed >= duration as u64 {
        return v.base_pitch;
    }

    // Two radix-256 divisions avoid a 64-bit divide on the timer path.
    let scaled = elapsed as u32 * 256;
    let x = (scaled / duration) * 256 + (scaled % duration) * 256 / duration;
    let index = (x >> 6) as usize;
    let fraction = x & 63;
    let snap = SNAP[index] as c_int
        - ((SNAP[index] - SNAP[index + 1]) * fraction / 64) as c_int;
    let note = (v.pitch * 65536 + v.sound.sweep * snap).clamp(0, 108 * 65536);

    // Q8 semitone entries use a Q12 interpolation fraction in one bank.
    let index = (note as usize) >> 16;
    let table = &PITCH[v.bank as usize * 109..];
    let mut value = table[index];
    if index < 108 {
        value +=
            (table[index + 1] - value) * ((note as u32 & 65535) >> 4) / 4096;
    }
    ((value + 128) >> 8) as c_int
}

/// Crossfades against playback time, which can begin after the score event.
fn mix_at(v: &AudioVoice, now: u64) -> c_int {
    let elapsed = if v.waiting != 0 {
        0
    } else {
        now.wrapping_sub(v.modulation_start)
    };
    let attack = audio_ms(v.sound.mix_attack) as u32;
    let release = audio_ms(v.sound.mix_release) as u32;
    if attack != 0 && elapsed < attack as u64 {
        let shift = gain_shift(attack);
        return ((elapsed as u32 >> shift) * LEVEL / (attack >> shift))
            as c_int;
    }
    if release != 0 && elapsed < (attack + release) as u64 {
        let shift = gain_shift(release);
        return (((attack + release - elapsed as u32) >> shift) * LEVEL
            / (release >> shift)) as c_int;
    }
    0
}

/// Clears caller-owned voice state and installs its register driver.
#[no_mangle]
pub unsafe extern "C" fn rust_audio_init(
    a: *mut Audio,
    driver: *const AudioDriver,
) {
    // SAFETY: C supplies a valid exclusive Audio and a live driver pointer.
    unsafe {
        core::ptr::write_bytes(a, 0, 1);
        core::ptr::copy_nonoverlapping(
            driver.cast::<AudioDriver>(),
            &mut (*a).driver,
            1,
        );
        (*a).idle_mask = IDLE_MASK;
        (*a).allocation_time = u64::MAX;
    }
}

/// Advances every active pair and commits queued key changes.
#[no_mangle]
pub unsafe extern "C" fn rust_audio_advance(ctx: *mut c_void, now: u64) {
    // SAFETY: The sequencer serializes calls and keeps its Audio context live.
    let a = unsafe { &mut *(ctx as *mut Audio) };
    if a.idle_mask == IDLE_MASK && a.starts | a.stops == 0 {
        return;
    }
    for i in 0..VOICES {
        let v = &mut a.voices[i];
        if v.active == 0 {
            continue;
        }
        // Redux can defer key-on. The mix clock begins only after both
        // hardware halves are observed, while release deadlines stay fixed.
        if v.waiting != 0 && a.starts & (1 << i) == 0 {
            if let Some(ready) = a.driver.ready {
                // SAFETY: The driver and its context outlive this service.
                if unsafe { ready(a.driver.context, i as c_int) } != 0 {
                    v.waiting = 0;
                    v.modulation_start = now;
                }
            }
        }
        v.level = level_at(v, now);
        if a.starts & (1 << i) != 0 {
            // SAFETY: C owns aggregate callback ABI and sound is live here.
            unsafe {
                audio_driver_start(&a.driver, i as c_int, v.bank, &v.sound)
            };
        }
        let b = v.level * mix_at(v, now) / LEVEL as c_int;
        // SAFETY: Required driver callbacks and context remain live.
        unsafe {
            a.driver.volume.unwrap_unchecked()(
                a.driver.context,
                i as c_int,
                v.level - b,
                b,
            );
            a.driver.pitch.unwrap_unchecked()(
                a.driver.context,
                i as c_int,
                pitch_at(v, now),
            );
        }
        if v.releasing != 0 && now >= v.end {
            v.active = 0;
            a.idle_mask |= 1 << i;
            a.stops |= 1 << i;
            a.starts &= !(1 << i);
        }
    }
    // SAFETY: Flush is required by the C interface and serializes key writes.
    unsafe {
        a.driver.flush.unwrap_unchecked()(a.driver.context, a.starts, a.stops)
    };
    a.starts = 0;
    a.stops = 0;
}

/// Allocates one pair and returns its generation-tagged gate token.
#[no_mangle]
pub unsafe extern "C" fn rust_audio_on(
    ctx: *mut c_void,
    now: u64,
    pitch: c_int,
    sound: *const SoundSettings,
) -> u32 {
    // SAFETY: The C adapter passes live pointers under serialized access.
    let a = unsafe { &mut *(ctx as *mut Audio) };
    // SAFETY: The sound argument remains live for this call.
    let sound = unsafe { *sound };
    let mut slot = 0;

    // Reclaim at most once per event time so chords share allocation state.
    if a.allocation_time != now {
        a.allocation_time = now;
        for i in 0..VOICES {
            let v = &mut a.voices[i];
            if v.active != 0 && v.releasing != 0 && now >= v.end {
                v.active = 0;
                a.idle_mask |= 1 << i;
                a.stops |= 1 << i;
                a.starts &= !(1 << i);
                // SAFETY: The driver and its context outlive this service.
                unsafe {
                    a.driver.volume.unwrap_unchecked()(
                        a.driver.context,
                        i as c_int,
                        0,
                        0,
                    )
                };
            }
        }
    }
    if a.idle_mask != 0 {
        slot = a.idle_mask.trailing_zeros() as usize;
        a.idle_mask &= !(1 << slot);
    } else {
        let mut quiet = None;
        let mut quiet_level = LEVEL as c_int + 1;
        for i in 0..VOICES {
            let v = &a.voices[i];
            if v.releasing != 0 {
                let level = level_at(v, now);
                if level < quiet_level {
                    quiet = Some(i);
                    quiet_level = level;
                }
            }
            if v.start < a.voices[slot].start {
                slot = i;
            }
        }
        if let Some(i) = quiet {
            slot = i;
        }
        a.steals += 1;
    }

    let v = &mut a.voices[slot];
    let mut generation = (v.generation + 1) & 0x07ff_ffff;
    if generation == 0 {
        generation = 1;
    }
    v.start = now;
    v.modulation_start = now;
    v.generation = generation;
    v.sound = sound;
    v.waiting = c_int::from(a.driver.ready.is_some());
    v.active = 1;
    v.releasing = 0;
    v.level = 0;
    v.pitch = pitch;
    v.bank = BANKS[(pitch * 49
        + if sound.decay != 0 { sound.sweep } else { 0 }
        + 24) as usize] as c_int;
    v.base_pitch =
        ((PITCH[v.bank as usize * 109 + pitch as usize] + 128) >> 8) as c_int;

    // A stolen pair cannot receive key-off in the same flush as key-on.
    a.stops &= !(1 << slot);
    a.starts |= 1 << slot;
    generation << 5 | slot as u32
}

/// Captures the current level so a release begins without a discontinuity.
fn release(a: &mut Audio, i: usize, now: u64, ms: c_int) {
    let v = &mut a.voices[i];
    v.release_level = level_at(v, now);
    v.release_at = now;
    v.end = now.wrapping_add(audio_ms(ms));
    v.releasing = 1;
}

/// Releases a voice only when its generation still matches the gate token.
#[no_mangle]
pub unsafe extern "C" fn rust_audio_off(
    ctx: *mut c_void,
    now: u64,
    token: u32,
) {
    // SAFETY: The sequencer keeps its context live and serializes callbacks.
    let a = unsafe { &mut *(ctx as *mut Audio) };
    let i = (token & 31) as usize;
    if i >= VOICES {
        return;
    }
    let v = &a.voices[i];
    if v.active != 0 && v.releasing == 0 && v.generation == token >> 5 {
        let ms = v.sound.release;
        release(a, i, now, ms);
        if ms == 0 {
            a.voices[i].active = 0;
            a.idle_mask |= 1 << i;
            a.stops |= 1 << i;
            a.starts &= !(1 << i);
            // SAFETY: The driver and context remain live for this callback.
            unsafe {
                a.driver.volume.unwrap_unchecked()(
                    a.driver.context,
                    i as c_int,
                    0,
                    0,
                )
            };
        }
    }
}

/// Gives active pairs a short release ramp at transport stop.
#[no_mangle]
pub unsafe extern "C" fn rust_audio_stop(ctx: *mut c_void, now: u64) {
    // SAFETY: The sequencer keeps its context live and serializes callbacks.
    let a = unsafe { &mut *(ctx as *mut Audio) };
    for i in 0..VOICES {
        if a.voices[i].active != 0 {
            release(a, i, now, 5);
        }
    }
}

/// Returns both hardware channel bits for each active wet pair.
#[no_mangle]
pub unsafe extern "C" fn audio_reverb_mask(a: *const Audio) -> u32 {
    // SAFETY: The platform passes a live Audio under serialized access.
    let a = unsafe { &*a };
    let mut mask = 0;
    for i in 0..VOICES {
        let v = &a.voices[i];
        if v.active != 0 && v.sound.reverb != 0 {
            mask |= 3 << (i * 2);
        }
    }
    mask
}
