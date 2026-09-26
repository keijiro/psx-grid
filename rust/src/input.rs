//! Converts raw controller samples into edges and independent repeat clocks.
//!
//! The caller owns the input history. All operations are allocation free; the
//! platform driver supplies raw reports through its C queue.

// Implementation notes:
// Cursor, row, and value repeats retain separate clocks so changing editor
// modes cannot carry an unrelated hold into an edit.

use core::ffi::c_int;

const DELAY: c_int = 18;
const INTERVAL: c_int = 3;
const VALUE_DELAY: c_int = 16;

const LEFT: u16 = 1;
const RIGHT: u16 = 2;
const UP: u16 = 4;
const DOWN: u16 = 8;
const CROSS: u16 = 16;
const CIRCLE: u16 = 32;
const START: u16 = 64;
const SELECT: u16 = 128;
const L1: u16 = 256;
const R1: u16 = 512;

/// Caller-owned button history with clocks shared with the C editor.
#[repr(C)]
#[derive(Default)]
pub struct Input {
    previous: u16,
    connected: c_int,
    direction: c_int,
    countdown: c_int,
    row_direction: c_int,
    row_countdown: c_int,
    value_direction: c_int,
    value_coarse: c_int,
    value_countdown: c_int,
    value_held: c_int,
}

/// One normalized frame returned to the C editor.
#[repr(C)]
#[derive(Default)]
pub struct InputFrame {
    pub(crate) connected: c_int,
    pub(crate) dx: c_int,
    pub(crate) dy: c_int,
    pub(crate) row_dy: c_int,
    pub(crate) value_dir: c_int,
    pub(crate) value_coarse: c_int,
    pub(crate) cross: c_int,
    pub(crate) circle: c_int,
    pub(crate) cross_held: c_int,
    pub(crate) cross_released: c_int,
    pub(crate) start: c_int,
    pub(crate) select: c_int,
}

// The C headers own these public layouts; fail the Rust build if the mirrored
// layout changes on a target before the C boundary is updated.
const _: () = assert!(core::mem::size_of::<Input>() == 40);
const _: () = assert!(core::mem::size_of::<InputFrame>() == 48);

fn pressed(held: u16, mask: u16) -> c_int {
    c_int::from(held & mask != 0)
}

fn reset_value_repeat(input: &mut Input) {
    input.value_direction = 0;
    input.value_held = 0;
    input.value_countdown = 0;
}

/// Follows vertical input even when horizontal cursor movement wins.
fn row_repeat(input: &mut Input, direction: c_int) -> c_int {
    if direction == 0 {
        input.row_direction = 0;
        input.row_countdown = 0;
    } else if direction != input.row_direction {
        input.row_direction = direction;
        input.row_countdown = DELAY;
        return direction;
    } else {
        input.row_countdown -= 1;
        if input.row_countdown <= 0 {
            input.row_countdown = INTERVAL;
            return direction;
        }
    }
    0
}

/// Releases the value clock on opposing inputs and restarts it on step changes.
fn value_repeat(input: &mut Input, held: u16, frame: &mut InputFrame) {
    let fine = pressed(held, RIGHT) - pressed(held, LEFT);
    let coarse = pressed(held, R1) - pressed(held, L1);
    let conflict =
        held & (LEFT | RIGHT) == LEFT | RIGHT || held & (L1 | R1) == L1 | R1;
    let value = if conflict || fine != 0 && coarse != 0 && fine != coarse {
        0
    } else if coarse != 0 {
        coarse
    } else {
        fine
    };
    if value == 0 {
        reset_value_repeat(input);
    } else if value != input.value_direction
        || c_int::from(coarse != 0) != input.value_coarse
    {
        input.value_direction = value;
        input.value_coarse = c_int::from(coarse != 0);
        input.value_held = 0;
        input.value_countdown = VALUE_DELAY;
        frame.value_coarse = input.value_coarse;
        frame.value_dir = value;
    } else {
        input.value_held += 1;
        input.value_countdown -= 1;
        if input.value_countdown <= 0 {
            frame.value_coarse = input.value_coarse;
            input.value_countdown = if input.value_held < 45 {
                5
            } else if input.value_held < 90 {
                4
            } else if input.value_held < 150 {
                3
            } else {
                2
            };
            frame.value_dir = value;
        }
    }
}

/// Retains the plane cursor's fixed repeat interval.
fn cursor_repeat(
    input: &mut Input,
    direction: c_int,
    dx: c_int,
    dy: c_int,
    frame: &mut InputFrame,
) {
    if direction == 0 {
        input.direction = 0;
        input.countdown = 0;
    } else if direction != input.direction {
        input.direction = direction;
        input.countdown = DELAY;
        frame.dx = dx;
        frame.dy = dy;
    } else {
        input.countdown -= 1;
        if input.countdown <= 0 {
            input.countdown = INTERVAL;
            frame.dx = dx;
            frame.dy = dy;
        }
    }
}

fn update(input: &mut Input, connected: c_int, held: u16) -> InputFrame {
    let mut frame = InputFrame {
        connected,
        ..InputFrame::default()
    };
    if connected == 0 {
        *input = Input::default();
        return frame;
    }
    let dx = pressed(held, RIGHT) - pressed(held, LEFT);
    let dy = if dx != 0 {
        0
    } else {
        pressed(held, DOWN) - pressed(held, UP)
    };
    let row_dy = pressed(held, DOWN) - pressed(held, UP);
    let direction = if dx > 0 {
        1
    } else if dx < 0 {
        2
    } else if dy > 0 {
        3
    } else if dy < 0 {
        4
    } else {
        0
    };
    if input.connected == 0 {
        input.connected = 1;
        input.previous = held;
        // Ignore buttons held at reconnection until all are released.
        input.direction = if held != 0 { -2 } else { 0 };
        return frame;
    }
    if input.direction == -2 {
        input.previous = held;
        if held == 0 {
            input.direction = 0;
        }
        return frame;
    }
    frame.cross_held = pressed(held, CROSS);
    frame.cross_released = pressed(input.previous & !held, CROSS);
    let edges = held & !input.previous;
    frame.cross = pressed(edges, CROSS);
    frame.circle = pressed(edges, CIRCLE);
    frame.start = pressed(edges, START);
    frame.select = pressed(edges, SELECT);
    input.previous = held;
    frame.row_dy = row_repeat(input, row_dy);
    value_repeat(input, held, &mut frame);
    cursor_repeat(input, direction, dx, dy, &mut frame);
    frame
}

/// Clears the caller-owned input history.
///
/// # Safety
/// `input` must point to a valid, writable C `Input`.
#[no_mangle]
pub unsafe extern "C" fn input_init(input: *mut Input) {
    // SAFETY: The C caller supplies exclusive access to a valid Input.
    unsafe { *input = Input::default() };
}

/// Clears the value adjustment repeat state.
///
/// # Safety
/// `input` must point to a valid, writable C `Input`.
#[no_mangle]
pub unsafe extern "C" fn input_reset_value_repeat(input: *mut Input) {
    // SAFETY: The C caller supplies exclusive access to a valid Input.
    reset_value_repeat(unsafe { &mut *input });
}

/// Defers held cursor repetition after a mode change.
///
/// # Safety
/// `input` must point to a valid, writable C `Input`.
#[no_mangle]
pub unsafe extern "C" fn input_reset_repeat(input: *mut Input) {
    // SAFETY: The C caller supplies exclusive access to a valid Input.
    let input = unsafe { &mut *input };
    input.countdown = DELAY;
    input.row_direction = 0;
    input.row_countdown = 0;
    reset_value_repeat(input);
}

/// Writes the normalized controller frame into caller-owned storage.
///
/// # Safety
/// `input` and `frame` must point to valid, distinct writable C objects.
#[no_mangle]
pub unsafe extern "C" fn input_update(
    input: *mut Input,
    connected: c_int,
    held: u16,
    frame: *mut InputFrame,
) {
    // SAFETY: The C caller passes distinct caller-owned objects.
    unsafe { *frame = update(&mut *input, connected, held) };
}
