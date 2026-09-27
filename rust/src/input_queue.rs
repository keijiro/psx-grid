//! Stores raw controller reports across the pad interrupt boundary.
//!
//! The C caller owns the fixed queue. The pad interrupt writes reports, and
//! the main thread reads them with interrupts disabled by the platform layer.

// Implementation notes:
// Unread slots are uninitialized so resetting the queue only changes its
// indices. The C layout is mirrored here for the interrupt-facing ABI.

use core::ffi::{c_int, c_uint};
use core::mem::MaybeUninit;

const CAPACITY: c_uint = 128;

/// One completed controller report in the C queue layout.
#[repr(C)]
#[derive(Clone, Copy)]
pub struct InputSample {
    connected: c_int,
    held: u16,
}

/// Caller-owned ring with uninitialized storage for reports not yet written.
#[repr(C)]
pub struct InputQueue {
    samples: [MaybeUninit<InputSample>; CAPACITY as usize],
    read: c_uint,
    write: c_uint,
}

const _: () = assert!(core::mem::size_of::<InputSample>() == 8);
const _: () = assert!(core::mem::size_of::<InputQueue>() == 1032);
const _: () = assert!(core::mem::offset_of!(InputQueue, read) == 1024);
const _: () = assert!(core::mem::offset_of!(InputQueue, write) == 1028);

/// Resets a caller-owned queue to empty.
///
/// # Safety
/// `queue` must point to writable storage for a C `InputQueue`. No other
/// access to the queue may occur during this call.
#[no_mangle]
pub unsafe extern "C" fn input_queue_init(queue: *mut InputQueue) {
    // SAFETY: The caller supplies writable storage. Only the indices must be
    // initialized because the sample slots contain MaybeUninit values.
    unsafe {
        core::ptr::addr_of_mut!((*queue).read).write(0);
        core::ptr::addr_of_mut!((*queue).write).write(0);
    }
}

/// Enqueues a report, inserting a disconnect before it after overflow.
///
/// Returns one if unread history was discarded, or zero otherwise.
///
/// # Safety
/// `queue` must point to an initialized C `InputQueue`. No other access to
/// the queue may occur during this call.
#[no_mangle]
pub unsafe extern "C" fn input_queue_push(
    queue: *mut InputQueue,
    sample: InputSample,
) -> c_int {
    // SAFETY: The caller grants exclusive access to the initialized queue.
    let queue = unsafe { &mut *queue };
    let mut next = (queue.write + 1) % CAPACITY;
    let overflow = next == queue.read;
    if overflow {
        // A stalled consumer loses ordering. The disconnect makes the release
        // guard cancel any gesture before processing this report.
        queue.read = 0;
        queue.write = 1;
        queue.samples[0].write(InputSample {
            connected: 0,
            held: 0,
        });
        next = 2;
    }
    queue.samples[queue.write as usize].write(sample);
    queue.write = next;
    c_int::from(overflow)
}

/// Removes the oldest report without touching `sample` when empty.
///
/// Returns one when a report was removed, or zero when the queue was empty.
///
/// # Safety
/// `queue` must point to an initialized C `InputQueue`, and `sample` must
/// point to distinct writable storage. No other access to the queue may
/// occur during this call.
#[no_mangle]
pub unsafe extern "C" fn input_queue_pop(
    queue: *mut InputQueue,
    sample: *mut InputSample,
) -> c_int {
    // SAFETY: The caller grants exclusive access to the initialized queue.
    let queue = unsafe { &mut *queue };
    if queue.read == queue.write {
        return 0;
    }
    // SAFETY: Only a successful push advances write, so this unread slot was
    // initialized. The output is distinct writable storage owned by C.
    unsafe {
        sample.write(queue.samples[queue.read as usize].assume_init_read());
    }
    queue.read = (queue.read + 1) % CAPACITY;
    1
}
