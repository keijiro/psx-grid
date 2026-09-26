//! Coordinates transactional score files through an injected card backend.
//!
//! Main-thread callers own fixed buffers and one card session at a time. The
//! backend retains BIOS and media access while this module selects generations.

// Implementation notes:
// Backend calls use pointer and integer arguments only; the C shim preserves
// the public by-value CardBackend initialization ABI.

use core::ffi::{c_char, c_int, c_void};
use core::ptr;

use crate::score::Score;
use crate::score_format::{
    score_format_decode, score_format_encode, score_format_set_generation,
};

const FILE_BYTES: usize = 8192;
const SLOTS: usize = 15;
const CARD_OK: c_int = 0;
const CARD_END: c_int = 1;
const CARD_MISSING: c_int = 2;
const CARD_TIMEOUT: c_int = 3;
const CARD_CHANGED: c_int = 4;
const CARD_UNFORMATTED: c_int = 6;
const CARD_DAMAGED: c_int = 7;
const FORMAT_OK: c_int = 0;
const FORMAT_NEWER: c_int = 2;
const STORAGE_EMPTY: c_int = 1;
const STORAGE_SAVED: c_int = 2;
const STORAGE_CORRUPT: c_int = 4;
const STORAGE_NEWER: c_int = 5;
const STORAGE_NO_CARD: c_int = 6;
const STORAGE_TIMEOUT: c_int = 7;
const STORAGE_CHANGED: c_int = 8;
const STORAGE_IO: c_int = 9;
const STORAGE_UNFORMATTED: c_int = 10;
const STORAGE_CARD_DAMAGED: c_int = 11;
const STORAGE_NO_SPACE: c_int = 12;
const STORAGE_SCORE_FULL: c_int = 13;
const STORAGE_CLEANUP: c_int = 14;
const STORAGE_GENERATION_FULL: c_int = 15;

/// Card directory entry shared with the C backend.
#[repr(C)]
#[derive(Clone, Copy)]
struct CardFile {
    name: [c_char; 21],
    size: c_int,
}

/// Backend callbacks for one card session.
#[repr(C)]
#[derive(Clone, Copy)]
pub struct CardBackend {
    context: *mut c_void,
    begin: Option<unsafe extern "C" fn(*mut c_void) -> c_int>,
    end: Option<unsafe extern "C" fn(*mut c_void)>,
    check: Option<unsafe extern "C" fn(*mut c_void) -> c_int>,
    list: Option<
        unsafe extern "C" fn(*mut c_void, c_int, *mut CardFile) -> c_int,
    >,
    open_read:
        Option<unsafe extern "C" fn(*mut c_void, *const c_char) -> c_int>,
    open_create:
        Option<unsafe extern "C" fn(*mut c_void, *const c_char) -> c_int>,
    read: Option<unsafe extern "C" fn(*mut c_void, *mut u8, c_int) -> c_int>,
    write: Option<unsafe extern "C" fn(*mut c_void, *const u8, c_int) -> c_int>,
    close: Option<unsafe extern "C" fn(*mut c_void) -> c_int>,
    erase: Option<unsafe extern "C" fn(*mut c_void, *const c_char) -> c_int>,
}

impl CardBackend {
    fn begin(self) -> c_int {
        // SAFETY: The C caller supplies a complete backend callback table.
        unsafe { (self.begin.expect("card begin callback"))(self.context) }
    }

    fn end(self) {
        // SAFETY: The C caller supplies a complete backend callback table.
        unsafe { (self.end.expect("card end callback"))(self.context) };
    }

    fn check(self) -> c_int {
        // SAFETY: The C caller supplies a complete backend callback table.
        unsafe { (self.check.expect("card check callback"))(self.context) }
    }

    fn list(self, index: c_int, file: &mut CardFile) -> c_int {
        // SAFETY: The C callback receives a valid writable directory entry.
        unsafe {
            (self.list.expect("card list callback"))(self.context, index, file)
        }
    }

    fn open_read(self, name: *const c_char) -> c_int {
        // SAFETY: `name` points to a live NUL-terminated card filename.
        unsafe {
            (self.open_read.expect("card read-open callback"))(
                self.context,
                name,
            )
        }
    }

    fn open_create(self, name: *const c_char) -> c_int {
        // SAFETY: `name` points to a live NUL-terminated card filename.
        unsafe {
            (self.open_create.expect("card create callback"))(
                self.context,
                name,
            )
        }
    }

    fn read(self, data: &mut [u8; FILE_BYTES]) -> c_int {
        // SAFETY: `data` is the complete writable score file buffer.
        unsafe {
            (self.read.expect("card read callback"))(
                self.context,
                data.as_mut_ptr(),
                FILE_BYTES as c_int,
            )
        }
    }

    fn write(self, data: &[u8; FILE_BYTES]) -> c_int {
        // SAFETY: `data` stays immutable through the synchronous write.
        unsafe {
            (self.write.expect("card write callback"))(
                self.context,
                data.as_ptr(),
                FILE_BYTES as c_int,
            )
        }
    }

    fn close(self) -> c_int {
        // SAFETY: The C caller supplies a complete backend callback table.
        unsafe { (self.close.expect("card close callback"))(self.context) }
    }

    fn erase(self, name: *const c_char) -> c_int {
        // SAFETY: `name` points to a live NUL-terminated card filename.
        unsafe {
            (self.erase.expect("card erase callback"))(self.context, name)
        }
    }
}

/// Caller-owned buffers, inventory, status, and staged incoming score.
#[repr(C)]
pub struct Storage {
    card: CardBackend,
    slots: [c_int; SLOTS],
    free_blocks: c_int,
    file_count: c_int,
    save: [u8; FILE_BYTES],
    readback: [u8; FILE_BYTES],
    files: [CardFile; SLOTS],
    incoming: Score,
}

const _: () = assert!(core::mem::size_of::<CardFile>() == 28);

fn card_result(result: c_int) -> c_int {
    match result {
        CARD_MISSING => STORAGE_NO_CARD,
        CARD_TIMEOUT => STORAGE_TIMEOUT,
        CARD_CHANGED => STORAGE_CHANGED,
        CARD_UNFORMATTED => STORAGE_UNFORMATTED,
        CARD_DAMAGED => STORAGE_CARD_DAMAGED,
        _ => STORAGE_IO,
    }
}

/// Parses only the application's fixed-width card filenames.
fn identity(file: &CardFile) -> Option<(c_int, u32)> {
    let name = file.name.map(|ch| ch as u8);
    if &name[..10] != b"BIJACQUARD"
        || name[20] != 0
        || !(b'0'..=b'1').contains(&name[10])
        || !name[11].is_ascii_digit()
    {
        return None;
    }
    let slot = c_int::from(name[10] - b'0') * 10 + c_int::from(name[11] - b'0');
    if !(1..=SLOTS as c_int).contains(&slot) {
        return None;
    }
    let mut generation = 0u32;
    for &digit in &name[12..20] {
        let nibble = match digit {
            b'0'..=b'9' => u32::from(digit - b'0'),
            b'A'..=b'F' => u32::from(digit - b'A' + 10),
            _ => return None,
        };
        generation = generation << 4 | nibble;
    }
    if generation == 0 {
        return None;
    }
    Some((slot, generation))
}

/// Closes a read handle even when the transfer itself fails.
fn read_file(storage: &mut Storage, name: *const c_char) -> c_int {
    let card = storage.card;
    let result = card.check();
    if result != CARD_OK {
        return result;
    }
    let result = card.open_read(name);
    if result != CARD_OK {
        return result;
    }
    let transfer = card.read(&mut storage.readback);
    let closed = card.close();
    if transfer != CARD_OK {
        return transfer;
    }
    if closed != CARD_OK {
        return closed;
    }
    card.check()
}

/// Closes a write handle even when the transfer itself fails.
fn write_file(storage: &Storage, name: *const c_char) -> c_int {
    let card = storage.card;
    let result = card.check();
    if result != CARD_OK {
        return result;
    }
    let result = card.open_create(name);
    if result != CARD_OK {
        return result;
    }
    let transfer = card.write(&storage.save);
    let closed = card.close();
    if transfer != CARD_OK {
        return transfer;
    }
    if closed != CARD_OK {
        return closed;
    }
    card.check()
}

/// Retains both the chosen valid file and the largest named generation.
struct Discovery {
    file: c_int,
    generation: u32,
    maximum: u32,
    result: c_int,
}

/// Keeps the first valid file when directory entries tie on generation.
fn discover(storage: &mut Storage, slot: c_int) -> Discovery {
    let mut found = Discovery {
        file: -1,
        generation: 0,
        maximum: 0,
        result: STORAGE_EMPTY,
    };
    let mut newer = 0;
    for i in 0..storage.file_count as usize {
        let Some((number, generation)) = identity(&storage.files[i]) else {
            continue;
        };
        if number != slot {
            continue;
        }
        found.maximum = found.maximum.max(generation);
        if storage.files[i].size != FILE_BYTES as c_int {
            if found.result == STORAGE_EMPTY {
                found.result = STORAGE_CORRUPT;
            }
            continue;
        }
        let name = storage.files[i].name.as_ptr();
        let result = read_file(storage, name);
        if result != CARD_OK {
            found.result = card_result(result);
            return found;
        }
        let mut payload_slot = 0;
        let mut payload_generation = 0;
        // SAFETY: The file buffer and incoming score are distinct owned fields.
        let format = unsafe {
            score_format_decode(
                storage.readback.as_ptr(),
                FILE_BYTES,
                &mut storage.incoming,
                &mut payload_slot,
                &mut payload_generation,
            )
        };
        if format == FORMAT_NEWER {
            newer = newer.max(generation);
            continue;
        }
        if format != FORMAT_OK
            || payload_slot != slot
            || payload_generation != generation
        {
            if found.result == STORAGE_EMPTY {
                found.result = STORAGE_CORRUPT;
            }
            continue;
        }
        if found.file < 0 || generation > found.generation {
            found.file = i as c_int;
            found.generation = generation;
            found.result = STORAGE_SAVED;
        }
    }
    if newer >= found.generation && newer != 0 {
        found.result = STORAGE_NEWER;
    }
    found
}

/// Initializes fixed storage while preserving the injected backend table.
///
/// # Safety
/// `storage` and `backend` must point to distinct valid C objects, with
/// `storage` exclusively writable.
#[no_mangle]
pub unsafe extern "C" fn storage_init_rust(
    storage: *mut Storage,
    backend: *const CardBackend,
) {
    // SAFETY: The C shim provides distinct valid writable and immutable objects.
    unsafe { ptr::write_bytes(storage, 0, 1) };
    // SAFETY: `backend` is valid for this call after storage is initialized.
    let backend = unsafe { *backend };
    // SAFETY: The zeroed storage is valid and writable.
    let storage = unsafe { &mut *storage };
    storage.card = backend;
    storage.free_blocks = -1;
}

/// Inventories the whole card inside one backend session.
fn begin(storage: &mut Storage) -> c_int {
    storage.free_blocks = -1;
    storage.file_count = 0;
    let card = storage.card;
    let result = card.begin();
    if result != CARD_OK {
        return card_result(result);
    }
    let mut used = 0;
    for i in 0..=SLOTS {
        let mut file = CardFile {
            name: [0; 21],
            size: 0,
        };
        let result = card.list(i as c_int, &mut file);
        if result == CARD_END {
            break;
        }
        if result != CARD_OK {
            card.end();
            return card_result(result);
        }
        if i == SLOTS
            || file.size <= 0
            || file.size % FILE_BYTES as c_int != 0
            || used + file.size / FILE_BYTES as c_int > SLOTS as c_int
        {
            card.end();
            return STORAGE_CARD_DAMAGED;
        }
        used += file.size / FILE_BYTES as c_int;
        storage.files[storage.file_count as usize] = file;
        storage.file_count += 1;
    }
    let result = card.check();
    if result != CARD_OK {
        card.end();
        return card_result(result);
    }
    storage.free_blocks = SLOTS as c_int - used;
    STORAGE_SAVED
}

fn finish(storage: &mut Storage, slot: c_int, result: c_int) -> c_int {
    storage.card.end();
    storage.slots[slot as usize - 1] = result;
    result
}

/// Refreshes a slot's state from the card inventory.
///
/// # Safety
/// `storage` must point to a valid exclusively writable C `Storage`.
#[no_mangle]
pub unsafe extern "C" fn storage_refresh(
    storage: *mut Storage,
    slot: c_int,
) -> c_int {
    if !(1..=SLOTS as c_int).contains(&slot) {
        return STORAGE_IO;
    }
    // SAFETY: The C caller provides exclusive access to valid storage.
    let storage = unsafe { &mut *storage };
    let result = begin(storage);
    if result != STORAGE_SAVED {
        storage.slots[slot as usize - 1] = result;
        return result;
    }
    let result = discover(storage, slot).result;
    finish(storage, slot, result)
}

/// Loads the highest valid generation into staged incoming storage.
///
/// # Safety
/// `storage` must point to a valid exclusively writable C `Storage`.
#[no_mangle]
pub unsafe extern "C" fn storage_load(
    storage: *mut Storage,
    slot: c_int,
) -> c_int {
    if !(1..=SLOTS as c_int).contains(&slot) {
        return STORAGE_IO;
    }
    // SAFETY: The C caller provides exclusive access to valid storage.
    let storage = unsafe { &mut *storage };
    let result = begin(storage);
    if result != STORAGE_SAVED {
        storage.slots[slot as usize - 1] = result;
        return result;
    }
    let found = discover(storage, slot);
    if found.result != STORAGE_SAVED {
        return finish(storage, slot, found.result);
    }
    let name = storage.files[found.file as usize].name.as_ptr();
    let result = read_file(storage, name);
    if result != CARD_OK {
        return finish(storage, slot, card_result(result));
    }
    let mut number = 0;
    let mut generation = 0;
    // SAFETY: The file buffer and incoming score are distinct owned fields.
    let format = unsafe {
        score_format_decode(
            storage.readback.as_ptr(),
            FILE_BYTES,
            &mut storage.incoming,
            &mut number,
            &mut generation,
        )
    };
    if format != FORMAT_OK || number != slot || generation != found.generation {
        return finish(
            storage,
            slot,
            if format == FORMAT_NEWER {
                STORAGE_NEWER
            } else {
                STORAGE_CORRUPT
            },
        );
    }
    finish(storage, slot, STORAGE_SAVED)
}

/// Erases only recognized obsolete generations after a valid file exists.
fn retire(storage: &mut Storage, slot: c_int, keep: c_int) -> c_int {
    for i in 0..storage.file_count as usize {
        if i as c_int == keep {
            continue;
        }
        let Some((number, _generation)) = identity(&storage.files[i]) else {
            continue;
        };
        if number != slot || storage.files[i].size != FILE_BYTES as c_int {
            continue;
        }
        let card = storage.card;
        let result = card.check();
        if result != CARD_OK {
            return result;
        }
        let result = card.erase(storage.files[i].name.as_ptr());
        if result != CARD_OK {
            return result;
        }
        storage.free_blocks += 1;
        // Erased entries cannot be retired twice in the same session.
        storage.files[i].name[0] = 0;
    }
    CARD_OK
}

fn filename(slot: c_int, generation: u32) -> [c_char; 21] {
    let mut name = [0; 21];
    for (i, &byte) in b"BIJACQUARD".iter().enumerate() {
        name[i] = byte as c_char;
    }
    name[10] = (b'0' + (slot / 10) as u8) as c_char;
    name[11] = (b'0' + (slot % 10) as u8) as c_char;
    for i in 0..8 {
        let nibble = ((generation >> ((7 - i) * 4)) & 15) as u8;
        name[12 + i] = if nibble < 10 {
            b'0' + nibble
        } else {
            b'A' + nibble - 10
        } as c_char;
    }
    name
}

/// Saves and verifies a new generation before retiring the previous one.
///
/// # Safety
/// `storage` and `score` must point to distinct valid C objects, with
/// `storage` exclusively writable.
#[no_mangle]
pub unsafe extern "C" fn storage_save(
    storage: *mut Storage,
    slot: c_int,
    score: *const Score,
) -> c_int {
    if !(1..=SLOTS as c_int).contains(&slot) {
        return STORAGE_IO;
    }
    // SAFETY: The C caller supplies distinct storage and score objects.
    let storage = unsafe { &mut *storage };
    // SAFETY: The score is immutable and the save buffer is writable.
    if unsafe { score_format_encode(score, storage.save.as_mut_ptr(), slot, 1) }
        != FORMAT_OK
    {
        return STORAGE_SCORE_FULL;
    }
    let result = begin(storage);
    if result != STORAGE_SAVED {
        storage.slots[slot as usize - 1] = result;
        return result;
    }
    let found = discover(storage, slot);
    if found.result != STORAGE_EMPTY
        && found.result != STORAGE_SAVED
        && found.result != STORAGE_CORRUPT
    {
        return finish(storage, slot, found.result);
    }
    if found.maximum == u32::MAX {
        return finish(storage, slot, STORAGE_GENERATION_FULL);
    }
    // Reclaim only obsolete generations while the latest verified file lives.
    if found.file >= 0 {
        let result = retire(storage, slot, found.file);
        if result != CARD_OK {
            return finish(storage, slot, card_result(result));
        }
    }
    if storage.free_blocks == 0 {
        return finish(storage, slot, STORAGE_NO_SPACE);
    }
    let generation = found.maximum + 1;
    // SAFETY: The owned save buffer contains a complete encoded score.
    unsafe {
        score_format_set_generation(storage.save.as_mut_ptr(), generation)
    };
    let name = filename(slot, generation);
    let result = write_file(storage, name.as_ptr());
    if result != CARD_OK {
        return finish(storage, slot, card_result(result));
    }
    let result = read_file(storage, name.as_ptr());
    if result != CARD_OK {
        return finish(storage, slot, card_result(result));
    }
    let mut number = 0;
    let mut read_generation = 0;
    // SAFETY: The file buffer and incoming score are distinct owned fields.
    let format = unsafe {
        score_format_decode(
            storage.readback.as_ptr(),
            FILE_BYTES,
            &mut storage.incoming,
            &mut number,
            &mut read_generation,
        )
    };
    if storage.save != storage.readback
        || format != FORMAT_OK
        || number != slot
        || read_generation != generation
    {
        return finish(storage, slot, STORAGE_CORRUPT);
    }
    storage.free_blocks -= 1;
    // A failed final cleanup leaves the verified generation recoverable.
    let result = retire(storage, slot, -1);
    if result == CARD_CHANGED || result == CARD_MISSING {
        return finish(storage, slot, card_result(result));
    }
    finish(
        storage,
        slot,
        if result != CARD_OK {
            STORAGE_CLEANUP
        } else {
            STORAGE_SAVED
        },
    )
}

const MESSAGES: [&[u8]; 16] = [
    b"CHECK\0",
    b"EMPTY\0",
    b"SAVED\0",
    b"BUSY\0",
    b"CORRUPT\0",
    b"NEWER VERSION\0",
    b"NO CARD\0",
    b"CARD TIMEOUT\0",
    b"CARD CHANGED\0",
    b"CARD I/O ERROR\0",
    b"UNFORMATTED\0",
    b"CARD DAMAGED\0",
    b"NO FREE BLOCK\0",
    b"SCORE FULL\0",
    b"SAVED / CLEANUP PENDING\0",
    b"GENERATION FULL\0",
];

/// Returns the user-facing message for a storage result.
#[no_mangle]
pub extern "C" fn storage_message(result: c_int) -> *const c_char {
    let value = if result < 0 {
        b"?\0".as_slice()
    } else {
        MESSAGES.get(result as usize).copied().unwrap_or(b"?\0")
    };
    value.as_ptr().cast()
}
