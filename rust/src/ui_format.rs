//! Formats editor values into caller-owned, NUL-terminated buffers.
//!
//! The fixed writer works without allocation for both menu rendering and the
//! C-facing formatting function.

use core::ffi::{c_char, c_int, CStr};
use core::fmt::{self, Write};

use crate::editor::Editor;
use crate::score::{
    score_channel, score_division, score_note_name, score_reverb_size,
    score_wave_name,
};

const ROW_BPM: c_int = 100;
const ROW_SLOT: c_int = 102;
const ROW_SAVE: c_int = 104;
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
const ACTION_LENGTH: c_int = 3;
const ACTION_PITCH: c_int = 7;
const ACTION_DURATION: c_int = 8;
const ACTION_PERIOD: c_int = 9;
const ACTION_CHANCE: c_int = 11;
const ACTION_DIVISION: c_int = 12;
const ACTION_CHANNEL: c_int = 14;
const ACTION_LOCK_ATTACK_ENABLE: c_int = 15;
const ACTION_LOCK_RELEASE_ENABLE: c_int = 16;
const ACTION_LOCK_ATTACK: c_int = 17;
const ACTION_LOCK_RELEASE: c_int = 18;
const LOCK_ATTACK: c_int = 1;
const LOCK_RELEASE: c_int = 2;

/// Writes ASCII display text without allocating, retaining a final NUL byte.
pub(crate) struct Text<'a> {
    bytes: &'a mut [u8],
    len: usize,
}

impl<'a> Text<'a> {
    pub(crate) fn new(bytes: &'a mut [u8]) -> Self {
        assert!(!bytes.is_empty());
        bytes[0] = 0;
        Self { bytes, len: 0 }
    }

    pub(crate) fn clear(&mut self) {
        self.len = 0;
        self.bytes[0] = 0;
    }

    pub(crate) fn as_bytes(&self) -> &[u8] {
        &self.bytes[..self.len]
    }

    pub(crate) fn is_empty(&self) -> bool {
        self.len == 0
    }

    pub(crate) fn replace(&mut self, from: u8, to: u8) -> bool {
        let mut found = false;
        for byte in &mut self.bytes[..self.len] {
            if *byte == from {
                *byte = to;
                found = true;
            }
        }
        found
    }

    pub(crate) fn push(&mut self, bytes: &[u8]) {
        let count = bytes.len().min(self.bytes.len() - self.len - 1);
        self.bytes[self.len..self.len + count].copy_from_slice(&bytes[..count]);
        self.len += count;
        self.bytes[self.len] = 0;
    }

    /// Copies a static C label into the bounded buffer.
    ///
    /// # Safety
    /// `ptr` must point to a live NUL-terminated string.
    pub(crate) unsafe fn push_c(&mut self, ptr: *const c_char) {
        // SAFETY: The caller supplies a live NUL-terminated label.
        let bytes = unsafe { CStr::from_ptr(ptr) }.to_bytes();
        self.push(bytes);
    }
}

impl Write for Text<'_> {
    fn write_str(&mut self, text: &str) -> fmt::Result {
        self.push(text.as_bytes());
        Ok(())
    }
}

/// Formats a menu row with the same bounded output used by C callers.
pub(crate) fn row_value(editor: &Editor, id: c_int, out: &mut Text<'_>) {
    out.clear();
    let sound = &editor.score.sounds[editor.sound_channel as usize];
    let value = if editor.target != 0 {
        editor.score.tiles[usize::from(editor.target)].value
    } else {
        editor.value
    };
    match id {
        ROW_BPM => write!(out, "{}", editor.score.bpm),
        ROW_SLOT => write!(out, "{:02}", editor.storage_slot),
        ROW_SAVE => write!(out, "FREE {} B", editor.free_bytes),
        ROW_SIZE => {
            // SAFETY: The model returns a static C string.
            unsafe { out.push_c(score_reverb_size(editor.score.reverb.size)) };
            Ok(())
        }
        ROW_AMOUNT => write!(out, "{}%", editor.score.reverb.amount),
        ROW_WAVE1 | ROW_WAVE2 => {
            let wave = if id == ROW_WAVE1 {
                sound.wave_a
            } else {
                sound.wave_b
            };
            // SAFETY: The model returns a static C string.
            unsafe { out.push_c(score_wave_name(wave)) };
            Ok(())
        }
        ROW_AMP_ATTACK => write!(out, "{} MS", sound.attack),
        ROW_AMP_RELEASE => write!(out, "{} MS", sound.release),
        ROW_MIX_ATTACK => write!(out, "{} MS", sound.mix_attack),
        ROW_MIX_RELEASE => write!(out, "{} MS", sound.mix_release),
        ROW_SWEEP => write!(out, "{:+} ST", sound.sweep),
        ROW_DECAY => write!(out, "{} MS", sound.decay),
        ROW_SEND => {
            out.push(if sound.reverb != 0 { b"ON" } else { b"OFF" });
            Ok(())
        }
        ACTION_LENGTH => {
            write!(out, "{}", editor.score.lanes[editor.lane as usize].length)
        }
        ACTION_DIVISION => {
            // SAFETY: The editor's selected lane is a valid model lane.
            let division =
                unsafe { score_division(&editor.score, editor.lane) };
            write!(out, "1/{division}")
        }
        ACTION_CHANNEL => {
            // SAFETY: The editor's selected lane is a valid model lane.
            let channel = unsafe { score_channel(&editor.score, editor.lane) };
            write!(out, "{}", channel + 1)
        }
        ACTION_PITCH => {
            // SAFETY: The model returns a static C string.
            unsafe { out.push_c(score_note_name(value.pitch)) };
            write!(out, "{}", value.pitch / 12)
        }
        ACTION_DURATION => {
            write!(out, "{}.{:02}", value.length / 20, value.length % 20 * 5)
        }
        ACTION_PERIOD => write!(out, "{}", value.period),
        ACTION_CHANCE => write!(out, "{}%", value.chance),
        ACTION_LOCK_ATTACK_ENABLE | ACTION_LOCK_RELEASE_ENABLE => {
            let mask = if id == ACTION_LOCK_ATTACK_ENABLE {
                LOCK_ATTACK
            } else {
                LOCK_RELEASE
            };
            out.push(if value.lock_mask & mask != 0 {
                b"ON"
            } else {
                b"OFF"
            });
            Ok(())
        }
        ACTION_LOCK_ATTACK => write!(out, "{:+} MS", value.attack),
        ACTION_LOCK_RELEASE => write!(out, "{:+} MS", value.release),
        _ => Ok(()),
    }
    .expect("fixed text writer cannot fail");
}

/// Formats a valid menu row into a C buffer.
///
/// # Safety
/// `editor` must point to a valid editor. `buffer` must point to `size`
/// writable bytes, with `size` positive, and must not overlap `editor`.
#[no_mangle]
pub unsafe extern "C" fn ui_format_row_value(
    editor: *const Editor,
    id: c_int,
    buffer: *mut c_char,
    size: c_int,
) {
    // SAFETY: The caller supplies a valid editor and a distinct output buffer.
    let editor = unsafe { &*editor };
    // SAFETY: The caller supplies a positive writable buffer length.
    let bytes = unsafe {
        core::slice::from_raw_parts_mut(buffer.cast(), size as usize)
    };
    row_value(editor, id, &mut Text::new(bytes));
}
