//! Encodes and decodes fixed-size score files for PlayStation memory cards.
//!
//! The caller owns the score and file block. Measurement and encoding use
//! the same traversal, and the wire fields never depend on host alignment.

// Implementation notes:
// The C score header remains the shared ABI while the model and codec run in
// Rust. Size checks in the model and C adapters guard layout drift.

use core::ffi::c_int;

use crate::score::{
    default_value, score_init, score_set_bpm, score_set_division,
    score_set_reverb_rust, score_set_sound_rust, score_validate_import,
    ReverbSettings, Score, SoundSettings, CHANNELS, LANES, STEPS,
    TILE_CAPACITY,
};

const FILE_BYTES: usize = 8192;
const WRAPPER: usize = 512;
const HEADER: usize = 32;

struct Writer<'a> {
    data: Option<&'a mut [u8]>,
    at: usize,
}

impl Writer<'_> {
    fn emit(&mut self, value: u32, bytes: usize) {
        if let Some(data) = &mut self.data {
            for i in 0..bytes {
                data[self.at + i] = (value >> (i * 8)) as u8;
            }
        }
        self.at += bytes;
    }

    fn chunk(&mut self, tag: u32, bytes: usize) {
        self.emit(tag, 2);
        self.emit(1, 2);
        self.emit(1, 4);
        self.emit(bytes as u32, 4);
    }
}

// The C decoder asserts these model discriminants still match the wire tags.
fn wave_tag(wave: c_int) -> u32 {
    match wave {
        0..=4 => wave as u32,
        _ => u32::MAX,
    }
}

fn lane_order(score: &Score) -> ([usize; LANES], [u8; LANES], usize) {
    let mut order = [0; LANES];
    let mut indices = [0; LANES];
    let mut count = 0;
    for i in 0..LANES {
        if score.lanes[i].active == 0 {
            continue;
        }
        let mut n = count;
        count += 1;
        while n > 0 {
            let previous = &score.lanes[order[n - 1]];
            let lane = &score.lanes[i];
            if previous.y < lane.y
                || previous.y == lane.y && previous.x <= lane.x
            {
                break;
            }
            order[n] = order[n - 1];
            n -= 1;
        }
        order[n] = i;
    }
    for i in 0..count {
        indices[order[i]] = i as u8;
    }
    (order, indices, count)
}

fn emit_steps(
    writer: &mut Writer<'_>,
    score: &Score,
    order: &[usize; LANES],
    indices: &[u8; LANES],
    count: usize,
) {
    for &lane_index in &order[..count] {
        let lane = &score.lanes[lane_index];
        for &head in &lane.tiles[..lane.length as usize] {
            let mut n = 0;
            let mut tile = head;
            while tile != 0 {
                n += 1;
                tile = score.tiles[tile as usize].next;
            }
            writer.emit(n, 1);
            tile = head;
            while tile != 0 {
                let entry = &score.tiles[tile as usize];
                let value = entry.value;
                match value.kind {
                    1 => {
                        writer.emit(1, 1);
                        writer.emit(3, 1);
                        writer.emit(value.pitch as u32, 1);
                        writer.emit(value.length as u32, 2);
                    }
                    2 => {
                        writer.emit(2, 1);
                        writer.emit(5, 1);
                        writer.emit(value.period as u32, 1);
                        writer.emit(value.pattern, 4);
                    }
                    3 => {
                        writer.emit(3, 1);
                        writer.emit(1, 1);
                        writer.emit(value.chance as u32, 1);
                    }
                    4 => {
                        writer.emit(4, 1);
                        writer.emit(1, 1);
                        writer.emit(indices[entry.branch as usize] as u32, 1);
                    }
                    5 => {
                        writer.emit(5, 1);
                        writer.emit(5, 1);
                        writer.emit(value.lock_mask as u32, 1);
                        writer.emit(value.attack as u32, 2);
                        writer.emit(value.release as u32, 2);
                    }
                    _ => {}
                }
                tile = entry.next;
            }
        }
    }
}

fn payload(score: &Score, data: Option<&mut [u8]>) -> usize {
    let (order, indices, count) = lane_order(score);
    let mut writer = Writer { data, at: 0 };
    writer.chunk(1, 8);
    writer.emit(score.bpm as u32, 2);
    writer.emit(score.reverb.size as u32, 1);
    writer.emit(score.reverb.amount as u32, 1);
    writer.emit(0, 4);
    writer.chunk(2, 16 * CHANNELS);
    for sound in &score.sounds {
        writer.emit(sound.attack as u32, 2);
        writer.emit(sound.release as u32, 2);
        writer.emit(wave_tag(sound.wave_a), 1);
        writer.emit(wave_tag(sound.wave_b), 1);
        writer.emit(sound.mix_attack as u32, 2);
        writer.emit(sound.mix_release as u32, 2);
        writer.emit(sound.sweep as u32, 1);
        writer.emit(sound.decay as u32, 2);
        writer.emit(sound.reverb as u32, 1);
        writer.emit(0, 2);
    }
    writer.chunk(3, count * 12);
    for &i in &order[..count] {
        let lane = &score.lanes[i];
        writer.emit(lane.x as u32, 1);
        writer.emit(lane.y as u32, 1);
        writer.emit(lane.length as u32, 1);
        writer.emit(
            if lane.source != 0 {
                0
            } else {
                lane.division as u32
            },
            1,
        );
        writer.emit(
            if lane.source != 0 {
                255
            } else {
                lane.channel as u32
            },
            1,
        );
        writer.emit(u32::from(lane.source != 0), 1);
        writer.emit(0, 4);
        writer.emit(0, 2);
    }
    let mut measure = Writer { data: None, at: 0 };
    emit_steps(&mut measure, score, &order, &indices, count);
    writer.chunk(4, measure.at);
    emit_steps(&mut writer, score, &order, &indices, count);
    writer.at
}

pub(crate) fn measure(score: &Score) -> usize {
    WRAPPER + HEADER + payload(score, None)
}

fn put(data: &mut [u8], value: u32) {
    for (i, byte) in data.iter_mut().enumerate() {
        *byte = (value >> (i * 8)) as u8;
    }
}

// The checksum field is read as zero so the stored value verifies itself.
fn checksum(data: &[u8]) -> u32 {
    let mut crc = u32::MAX;
    for (i, &byte) in data.iter().enumerate() {
        crc ^= if (20..24).contains(&i) {
            0
        } else {
            u32::from(byte)
        };
        for _ in 0..8 {
            crc = (crc >> 1) ^ (0xedb8_8320 & 0u32.wrapping_sub(crc & 1));
        }
    }
    !crc
}

/// Returns the encoded byte count, including the card wrapper.
///
/// # Safety
/// `score` must point to a valid, initialized C `Score` whose links terminate.
#[no_mangle]
pub unsafe extern "C" fn score_format_measure(score: *const Score) -> usize {
    // SAFETY: The C caller provides a valid immutable Score.
    measure(unsafe { &*score })
}

/// Encodes a validated score into an owned card block.
///
/// # Safety
/// `score` must point to a valid C `Score`, and `block` must point to a
/// writable block of `FILE_BYTES` bytes. The regions must not overlap.
#[no_mangle]
pub unsafe extern "C" fn score_format_encode(
    score: *const Score,
    block: *mut u8,
    slot: c_int,
    generation: u32,
) -> c_int {
    // SAFETY: The C caller provides a valid immutable Score.
    let score = unsafe { &*score };
    // SAFETY: The score reference comes from a C Score of the same layout.
    let valid = unsafe { score_validate_import(score) };
    if valid == 3 {
        return 3;
    }
    if valid != 0 {
        return 1;
    }
    let used = WRAPPER + HEADER + payload(score, None);
    if used > FILE_BYTES {
        return 3;
    }
    if !(1..=15).contains(&slot) || generation == 0 {
        return 1;
    }

    // SAFETY: The C caller owns a distinct writable fixed-size block.
    let block = unsafe { core::slice::from_raw_parts_mut(block, FILE_BYTES) };
    block.fill(0);
    block[..4].copy_from_slice(&[b'S', b'C', 0x11, 1]);
    for (i, &character) in b"JACQUARD 00".iter().enumerate() {
        let c = if i == 9 {
            b'0' + (slot / 10) as u8
        } else if i == 10 {
            b'0' + (slot % 10) as u8
        } else {
            character
        };
        // Full-width Shift-JIS ASCII is understood by the card manager.
        let sjis = match c {
            b' ' => 0x8140,
            b'0'..=b'9' => 0x824f + u16::from(c - b'0'),
            _ => 0x8260 + u16::from(c - b'A'),
        };
        block[4 + i * 2] = (sjis >> 8) as u8;
        block[5 + i * 2] = sjis as u8;
    }
    put(&mut block[98..100], 0x7fff);
    for y in 0..16 {
        for x in 0..16 {
            if x == 2
                || y == 13
                || y == 3 && x > 2 && x < 13
                || x == 12 && y < 14 && y > 2
            {
                block[128 + y * 8 + x / 2] |= 1 << ((x & 1) * 4);
            }
        }
    }
    let header = &mut block[WRAPPER..];
    header[..4].copy_from_slice(b"JQSC");
    put(&mut header[4..6], 1);
    put(&mut header[6..8], 1);
    put(&mut header[8..10], HEADER as u32);
    put(&mut header[12..16], (used - WRAPPER - HEADER) as u32);
    put(&mut header[24..28], generation);
    header[28] = slot as u8;
    payload(score, Some(&mut header[HEADER..]));
    let crc = checksum(&header[..used - WRAPPER]);
    put(&mut header[20..24], crc);
    0
}

/// Updates the generation and checksum of an encoded card block.
///
/// # Safety
/// `block` must point to a writable block of `FILE_BYTES` bytes produced
/// by `score_format_encode`.
#[no_mangle]
pub unsafe extern "C" fn score_format_set_generation(
    block: *mut u8,
    generation: u32,
) {
    // SAFETY: The C caller owns a writable encoded block.
    let block = unsafe { core::slice::from_raw_parts_mut(block, FILE_BYTES) };
    let header = &mut block[WRAPPER..];
    put(&mut header[24..28], generation);
    let bytes =
        u32::from_le_bytes([header[12], header[13], header[14], header[15]])
            as usize;
    let crc = checksum(&header[..HEADER + bytes]);
    put(&mut header[20..24], crc);
}

const FORMAT_CORRUPT: c_int = 1;
const FORMAT_NEWER: c_int = 2;
const CHUNK: usize = 12;
const GLOBAL: usize = 1;
const SOUNDS: usize = 2;
const LANES_TAG: usize = 3;
const STEPS_TAG: usize = 4;
const REQUIRED_CHUNKS: u32 =
    (1 << GLOBAL) | (1 << SOUNDS) | (1 << LANES_TAG) | (1 << STEPS_TAG);

fn get(data: &[u8]) -> u32 {
    data.iter()
        .enumerate()
        .fold(0, |value, (i, byte)| value | u32::from(*byte) << (i * 8))
}

fn zero(data: &[u8]) -> bool {
    data.iter().all(|&byte| byte == 0)
}

/// Keeps each validated chunk's data and length together through decoding.
fn decode_chunks(
    header: &[u8],
    header_bytes: usize,
    payload_bytes: usize,
) -> Result<[&[u8]; 5], c_int> {
    let mut chunks = [&[][..]; 5];
    let mut seen = 0u32;
    let end = header_bytes + payload_bytes;
    let mut at = header_bytes;
    while at < end {
        if end - at < CHUNK {
            return Err(FORMAT_CORRUPT);
        }
        let chunk = &header[at..at + CHUNK];
        let tag = get(&chunk[..2]) as usize;
        let schema = get(&chunk[2..4]);
        let flags = get(&chunk[4..8]);
        let bytes = get(&chunk[8..12]) as usize;
        at += CHUNK;
        if bytes > end - at {
            return Err(FORMAT_CORRUPT);
        }
        if flags & !1 != 0 {
            return Err(FORMAT_NEWER);
        }
        if (GLOBAL..=STEPS_TAG).contains(&tag) {
            if seen & (1 << tag) != 0 {
                return Err(FORMAT_CORRUPT);
            }
            if schema != 1 || flags != 1 {
                return Err(FORMAT_NEWER);
            }
            seen |= 1 << tag;
            chunks[tag] = &header[at..at + bytes];
        } else if flags & 1 != 0 {
            return Err(FORMAT_NEWER);
        }
        at += bytes;
    }
    if seen != REQUIRED_CHUNKS
        || chunks[GLOBAL].len() != 8
        || chunks[SOUNDS].len() != 128
        || chunks[LANES_TAG].len() % 12 != 0
        || chunks[LANES_TAG].len() / 12 > LANES
    {
        return Err(FORMAT_CORRUPT);
    }
    Ok(chunks)
}

/// Preserves the distinction between newer waveform tags and corrupt values.
fn decode_sounds(data: &[u8], score: &mut Score) -> Result<(), c_int> {
    for (i, sound) in data.as_chunks::<16>().0.iter().enumerate() {
        if sound[4] > 4 || sound[5] > 4 {
            return Err(FORMAT_NEWER);
        }
        let value = SoundSettings {
            attack: get(&sound[..2]) as c_int,
            release: get(&sound[2..4]) as c_int,
            wave_a: c_int::from(sound[4]),
            wave_b: c_int::from(sound[5]),
            mix_attack: get(&sound[6..8]) as c_int,
            mix_release: get(&sound[8..10]) as c_int,
            sweep: c_int::from(sound[10] as i8),
            decay: get(&sound[11..13]) as c_int,
            reverb: c_int::from(sound[13]),
        };
        if !zero(&sound[14..16]) {
            return Err(FORMAT_CORRUPT);
        }
        // SAFETY: `score` and `value` are valid, distinct C layout objects.
        if unsafe { score_set_sound_rust(score, i as c_int, &value) } != 0 {
            return Err(FORMAT_CORRUPT);
        }
    }
    Ok(())
}

/// Rejects lane ordering the encoder cannot produce.
fn decode_lanes(
    data: &[u8],
    score: &mut Score,
    roles: &mut [u8; LANES],
) -> Result<usize, c_int> {
    let count = data.len() / 12;
    for (i, lane_bytes) in data.as_chunks::<12>().0.iter().enumerate() {
        let role = lane_bytes[5];
        if !zero(&lane_bytes[6..12]) || role > 1 {
            return Err(FORMAT_CORRUPT);
        }
        if role != 0 && (lane_bytes[3] != 0 || lane_bytes[4] != 255)
            || role == 0 && usize::from(lane_bytes[4]) >= CHANNELS
        {
            return Err(FORMAT_CORRUPT);
        }
        let length = usize::from(lane_bytes[2]);
        if length == 0
            || length > STEPS
            || usize::from(lane_bytes[0]) + length + 1 >= 128
            || lane_bytes[1] >= 64
        {
            return Err(FORMAT_CORRUPT);
        }
        if i > 0 {
            let previous = &score.lanes[i - 1];
            let y = c_int::from(lane_bytes[1]);
            let x = c_int::from(lane_bytes[0]);
            if y < previous.y || y == previous.y && x <= previous.x {
                return Err(FORMAT_CORRUPT);
            }
        }
        let lane = &mut score.lanes[i];
        lane.active = 1;
        lane.x = c_int::from(lane_bytes[0]);
        lane.y = c_int::from(lane_bytes[1]);
        lane.length = length as c_int;
        lane.division = if role != 0 {
            16
        } else {
            c_int::from(lane_bytes[3])
        };
        lane.channel = if role != 0 {
            -1
        } else {
            c_int::from(lane_bytes[4])
        };
        roles[i] = role;
        if role == 0 {
            // SAFETY: This lane is active, regular and owned by `score`.
            if unsafe {
                score_set_division(
                    score,
                    i as c_int,
                    c_int::from(lane_bytes[3]),
                )
            } != 0
            {
                return Err(FORMAT_CORRUPT);
            }
        }
    }
    Ok(count)
}

/// Decodes one tile after its bounded payload has been checked.
fn decode_tile(
    score: &mut Score,
    roles: &[u8; LANES],
    lane_count: usize,
    id: u16,
    tag: u8,
    data: &[u8],
) -> Result<(), c_int> {
    let expected = match tag {
        1 => 3,
        2 => 5,
        3 | 4 => 1,
        5 => 5,
        _ => return Err(FORMAT_NEWER),
    };
    if data.len() != expected {
        return Err(FORMAT_CORRUPT);
    }
    let tile = &mut score.tiles[usize::from(id)];
    tile.value = default_value(c_int::from(tag));
    tile.branch = -1;
    match tag {
        1 => {
            tile.value.pitch = c_int::from(data[0]);
            tile.value.length = get(&data[1..3]) as c_int;
        }
        2 => {
            tile.value.period = c_int::from(data[0]);
            tile.value.pattern = get(&data[1..5]);
        }
        3 => tile.value.chance = c_int::from(data[0]),
        4 => {
            let branch = usize::from(data[0]);
            if branch >= lane_count
                || roles[branch] == 0
                || score.lanes[branch].source != 0
            {
                return Err(FORMAT_CORRUPT);
            }
            tile.branch = branch as c_int;
            score.lanes[branch].source = id;
        }
        5 => {
            tile.value.lock_mask = c_int::from(data[0]);
            tile.value.attack = c_int::from(get(&data[1..3]) as i16);
            tile.value.release = c_int::from(get(&data[3..5]) as i16);
        }
        _ => return Err(FORMAT_NEWER),
    }
    Ok(())
}

/// Bounds every stack element before advancing the input cursor.
fn decode_steps(
    data: &[u8],
    score: &mut Score,
    roles: &[u8; LANES],
    lane_count: usize,
) -> Result<usize, c_int> {
    let mut at = 0;
    let mut next_tile = 1;
    for lane_index in 0..lane_count {
        for step in 0..score.lanes[lane_index].length as usize {
            if at == data.len() {
                return Err(FORMAT_CORRUPT);
            }
            let count = usize::from(data[at]);
            at += 1;
            if count > 64 - score.lanes[lane_index].y as usize {
                return Err(FORMAT_CORRUPT);
            }
            let mut previous = 0;
            for _ in 0..count {
                if data.len() - at < 2 || next_tile > TILE_CAPACITY {
                    return Err(FORMAT_CORRUPT);
                }
                let tag = data[at];
                let length = usize::from(data[at + 1]);
                at += 2;
                if data.len() - at < length {
                    return Err(FORMAT_CORRUPT);
                }
                let id = next_tile as u16;
                decode_tile(
                    score,
                    roles,
                    lane_count,
                    id,
                    tag,
                    &data[at..at + length],
                )?;
                if previous == 0 {
                    score.lanes[lane_index].tiles[step] = id;
                } else {
                    score.tiles[previous].next = id;
                }
                previous = next_tile;
                next_tile += 1;
                at += length;
            }
        }
    }
    if at != data.len() {
        return Err(FORMAT_CORRUPT);
    }
    Ok(next_tile)
}

/// Decodes a card block into a caller-owned staging score.
///
/// A failure may modify `staging`. Optional outputs are written only on
/// success.
///
/// # Safety
/// `block` must point to `size` readable bytes and `staging` must point to a
/// valid writable C `Score`. The input and output regions must not overlap.
#[no_mangle]
pub unsafe extern "C" fn score_format_decode(
    block: *const u8,
    size: usize,
    staging: *mut Score,
    slot: *mut c_int,
    generation: *mut u32,
) -> c_int {
    if !(WRAPPER + HEADER..=FILE_BYTES).contains(&size) {
        return FORMAT_CORRUPT;
    }
    // SAFETY: The C caller provides a readable `size`-byte input block.
    let block = unsafe { core::slice::from_raw_parts(block, size) };
    if &block[..2] != b"SC" || block[2] != 0x11 || block[3] != 1 {
        return FORMAT_CORRUPT;
    }
    let header = &block[WRAPPER..];
    if &header[..4] != b"JQSC" {
        return FORMAT_CORRUPT;
    }
    let header_bytes = get(&header[8..10]) as usize;
    let payload_bytes = get(&header[12..16]) as usize;
    if header_bytes < HEADER
        || header_bytes > header.len()
        || payload_bytes > header.len() - header_bytes
    {
        return FORMAT_CORRUPT;
    }
    if get(&header[20..24]) != checksum(&header[..header_bytes + payload_bytes])
    {
        return FORMAT_CORRUPT;
    }
    if get(&header[4..6]) != 1
        || get(&header[6..8]) > 1
        || get(&header[16..20]) != 0
        || header_bytes != HEADER
    {
        return FORMAT_NEWER;
    }
    if !zero(&header[10..12])
        || !zero(&header[29..32])
        || !(1..=15).contains(&header[28])
        || get(&header[24..28]) == 0
    {
        return FORMAT_CORRUPT;
    }
    let chunks = match decode_chunks(header, header_bytes, payload_bytes) {
        Ok(chunks) => chunks,
        Err(error) => return error,
    };
    // SAFETY: The C caller owns a valid, exclusively writable staging score.
    let score = unsafe { &mut *staging };
    // SAFETY: `score` is a valid writable C Score.
    unsafe { score_init(score) };
    let global = chunks[GLOBAL];
    let reverb = ReverbSettings {
        size: c_int::from(global[2]),
        amount: c_int::from(global[3]),
    };
    // SAFETY: The score and reverb arguments are distinct valid objects.
    if !zero(&global[4..8])
        || unsafe { score_set_bpm(score, get(&global[..2]) as c_int) } != 0
        || unsafe { score_set_reverb_rust(score, &reverb) } != 0
    {
        return FORMAT_CORRUPT;
    }
    if let Err(error) = decode_sounds(chunks[SOUNDS], score) {
        return error;
    }
    let mut roles = [0; LANES];
    let lane_count = match decode_lanes(chunks[LANES_TAG], score, &mut roles) {
        Ok(count) => count,
        Err(error) => return error,
    };
    let next_tile =
        match decode_steps(chunks[STEPS_TAG], score, &roles, lane_count) {
            Ok(next_tile) => next_tile,
            Err(error) => return error,
        };
    for (i, role) in roles[..lane_count].iter().enumerate() {
        if *role != u8::from(score.lanes[i].source != 0) {
            return FORMAT_CORRUPT;
        }
    }
    // SAFETY: The model validator receives the initialized staged score.
    if unsafe { score_validate_import(score) } != 0
        || WRAPPER + HEADER + payload(score, None) > FILE_BYTES
    {
        return FORMAT_CORRUPT;
    }
    score.generation = 0;
    for i in 0..lane_count {
        score.generation += 1;
        score.lane_generation[i] = score.generation;
    }
    for i in 1..next_tile {
        score.generation += 1;
        score.tile_generation[i] = score.generation;
    }
    score.revision = 1;
    if !slot.is_null() {
        // SAFETY: The optional C output is writable when non-null.
        unsafe { *slot = c_int::from(header[28]) };
    }
    if !generation.is_null() {
        // SAFETY: The optional C output is writable when non-null.
        unsafe { *generation = get(&header[24..28]) };
    }
    0
}
