//! Portable input logic for the PlayStation application.
//!
//! The C interface keeps SDK and hardware access in the platform layer.
//! State is owned by the caller and uses fixed storage without allocation.

#![no_std]

mod input;

#[cfg(not(test))]
use core::panic::PanicInfo;

#[cfg(not(test))]
#[panic_handler]
fn panic(_info: &PanicInfo) -> ! {
    loop {}
}

// Host-linked C tests abort on panic, but the bundled `core` archive still
// references a personality symbol from unused unwind metadata.
#[cfg(target_vendor = "apple")]
/// Supplies the unused host unwind metadata symbol for C test links.
#[no_mangle]
pub extern "C" fn rust_eh_personality() {}
