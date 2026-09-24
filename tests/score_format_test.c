#include "score_format.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { ENVELOPE = 512, HEADER = 32, CHUNK = 12 };
static uint8_t block[SCORE_FILE_BYTES], changed[SCORE_FILE_BYTES];

static uint32_t get(const uint8_t *p, int n) {
    uint32_t v = 0;
    for (int i = 0; i < n; i++)
        v |= (uint32_t)p[i] << (8 * i);
    return v;
}

static void put(uint8_t *p, uint32_t v, int n) {
    for (int i = 0; i < n; i++)
        p[i] = (uint8_t)(v >> (8 * i));
}

static uint32_t checksum(const uint8_t *p, size_t n) {
    uint32_t crc = UINT32_MAX;
    for (size_t i = 0; i < n; i++) {
        crc ^= i >= 20 && i < 24 ? 0 : p[i];
        for (int b = 0; b < 8; b++)
            crc = (crc >> 1) ^ (0xedb88320u & -(crc & 1));
    }
    return ~crc;
}

static void repair_crc(uint8_t *data) {
    uint8_t *h = data + ENVELOPE;
    put(h + 20, checksum(h, get(h + 8, 2) + get(h + 12, 4)), 4);
}

static uint8_t *chunk(uint8_t *data, int wanted) {
    uint8_t *h = data + ENVELOPE;
    size_t end = get(h + 8, 2) + get(h + 12, 4);
    for (size_t at = get(h + 8, 2); at < end;) {
        uint8_t *c = h + at;
        if (get(c, 2) == (unsigned)wanted)
            return c;
        at += CHUNK + get(c + 8, 4);
    }
    return NULL;
}

static TileId tile(const Score *s, int x, int y) {
    return score_at(s, x, y).tile;
}

static void example(Score *s) {
    score_init(s);
    assert(score_create(s, 20, 10, 3) == SCORE_OK);
    assert(score_create(s, 2, 0, 5) == SCORE_OK);
    assert(score_set_bpm(s, 173) == SCORE_OK);
    assert(score_set_reverb(s, (ReverbSettings){2, 87}) == SCORE_OK);
    SoundSettings sound = {16000, 1234, WAVE_NOISE, WAVE_TRIANGLE, 500, 321, -24, 2000, 1};
    assert(score_set_sound(s, 7, sound) == SCORE_OK);
    assert(score_set_channel(s, 0, 7) == SCORE_OK);
    assert(score_set_division(s, 0, 3) == SCORE_OK);
    assert(score_set_channel(s, 1, 2) == SCORE_OK);
    assert(score_set_division(s, 1, 64) == SCORE_OK);
    assert(score_place(s, 21, 10, TILE_NOTE) == SCORE_OK);
    TileValue v = s->tiles[tile(s, 21, 10)].value;
    v.pitch = 108;
    v.length = 1280;
    assert(score_edit(s, tile(s, 21, 10), v) == SCORE_OK);
    assert(score_place(s, 3, 0, TILE_CYCLE) == SCORE_OK);
    v = s->tiles[tile(s, 3, 0)].value;
    v.period = 32;
    v.pattern = 0xa5c30081u;
    assert(score_edit(s, tile(s, 3, 0), v) == SCORE_OK);
    assert(score_place(s, 4, 0, TILE_PROBABILITY) == SCORE_OK);
    v = s->tiles[tile(s, 4, 0)].value;
    v.chance = 0;
    assert(score_edit(s, tile(s, 4, 0), v) == SCORE_OK);
    assert(score_place(s, 5, 0, TILE_RELATIVE) == SCORE_OK);
    v = s->tiles[tile(s, 5, 0)].value;
    v.lock_mask = 3;
    v.attack = -16000;
    v.release = 16000;
    assert(score_edit(s, tile(s, 5, 0), v) == SCORE_OK);
    assert(score_place(s, 6, 0, TILE_JUMP) == SCORE_OK);
}

static void equivalent(const Score *a, const Score *b) {
    uint8_t x[SCORE_FILE_BYTES], y[SCORE_FILE_BYTES];
    assert(score_format_encode(a, x, 15, 0x89abcdefu) == FORMAT_OK);
    assert(score_format_encode(b, y, 15, 0x89abcdefu) == FORMAT_OK);
    assert(!memcmp(x, y, sizeof(x)));
}

static FormatResult decode_publish(const uint8_t *data, size_t size, Score *published, int *slot,
                                   uint32_t *generation) {
    static Score staging;
    FormatResult result = score_format_decode(data, size, &staging, slot, generation);
    if (result == FORMAT_OK)
        *published = staging;
    return result;
}

static void rejected(FormatResult expected, size_t size) {
    Score published, before;
    memset(&published, 0xa5, sizeof(published));
    before = published;
    int slot = 91, before_slot = slot;
    uint32_t generation = 0x12345678u, before_generation = generation;
    assert(decode_publish(changed, size, &published, &slot, &generation) == expected);
    assert(!memcmp(&published, &before, sizeof(published)));
    assert(slot == before_slot && generation == before_generation);
}

static void mutate(size_t offset, uint8_t value, FormatResult expected) {
    memcpy(changed, block, sizeof(changed));
    changed[offset] = value;
    repair_crc(changed);
    rejected(expected, sizeof(changed));
}

static void golden(int write_fixture) {
    Score source, decoded;
    example(&source);
    assert(score_format_measure(&source) == 801);
    assert(score_format_encode(&source, block, 15, 0x89abcdefu) == FORMAT_OK);
    assert(get(block + ENVELOPE + 12, 4) + ENVELOPE + HEADER == score_format_measure(&source));
    if (write_fixture) {
        FILE *f = fopen("tests/fixtures/score-v1.bin", "wb");
        assert(f);
        assert(fwrite(block, 1, sizeof(block), f) == sizeof(block));
        assert(!fclose(f));
    }
    FILE *f = fopen("tests/fixtures/score-v1.bin", "rb");
    assert(f);
    assert(fread(changed, 1, sizeof(changed), f) == sizeof(changed));
    assert(fgetc(f) == EOF);
    assert(!fclose(f));
    assert(!memcmp(block, changed, sizeof(block)));
    int slot = 0;
    uint32_t generation = 0;
    assert(score_format_decode(changed, sizeof(changed), &decoded, &slot, &generation) ==
           FORMAT_OK);
    assert(slot == 15 && generation == 0x89abcdefu);
    equivalent(&source, &decoded);
    score_format_set_generation(changed, 7);
    assert(score_format_decode(changed, sizeof(changed), &decoded, &slot, &generation) ==
           FORMAT_OK);
    assert(generation == 7);
    equivalent(&source, &decoded);
}

static void compatibility(void) {
    Score expected, decoded;
    example(&expected);
    memcpy(changed, block, sizeof(changed));
    uint8_t *h = changed + ENVELOPE;
    size_t bytes = get(h + 12, 4), at = HEADER + bytes;
    put(h + at, 99, 2);
    put(h + at + 2, 1, 2);
    put(h + at + 4, 0, 4);
    put(h + at + 8, 3, 4);
    h[at + 12] = 1;
    h[at + 13] = 2;
    h[at + 14] = 3;
    put(h + 12, bytes + 15, 4);
    repair_crc(changed);
    assert(score_format_decode(changed, sizeof(changed), &decoded, NULL, NULL) == FORMAT_OK);
    equivalent(&expected, &decoded);

    memcpy(changed, block, sizeof(changed));
    put(chunk(changed, 1), 99, 2);
    repair_crc(changed);
    rejected(FORMAT_NEWER, sizeof(changed));
    memcpy(changed, block, sizeof(changed));
    put(chunk(changed, 1) + 2, 2, 2);
    repair_crc(changed);
    rejected(FORMAT_NEWER, sizeof(changed));
    memcpy(changed, block, sizeof(changed));
    put(chunk(changed, 1) + 4, 3, 4);
    repair_crc(changed);
    rejected(FORMAT_NEWER, sizeof(changed));
    mutate(ENVELOPE + 4, 2, FORMAT_NEWER);
    mutate(ENVELOPE + 6, 2, FORMAT_NEWER);
    mutate(ENVELOPE + 16, 1, FORMAT_NEWER);
}

static void invalid_inputs(void) {
    memcpy(changed, block, sizeof(changed));
    changed[700] ^= 1;
    rejected(FORMAT_CORRUPT, sizeof(changed));
    rejected(FORMAT_CORRUPT, ENVELOPE + HEADER - 1);
    mutate(0, 'X', FORMAT_CORRUPT);
    mutate(2, 0, FORMAT_CORRUPT);
    mutate(ENVELOPE, 0, FORMAT_CORRUPT);
    mutate(ENVELOPE + 10, 1, FORMAT_CORRUPT);
    mutate(ENVELOPE + 28, 0, FORMAT_CORRUPT);
    mutate(ENVELOPE + 29, 1, FORMAT_CORRUPT);
    memcpy(changed, block, sizeof(changed));
    put(changed + ENVELOPE + 24, 0, 4);
    repair_crc(changed);
    rejected(FORMAT_CORRUPT, sizeof(changed));
    memcpy(changed, block, sizeof(changed));
    put(changed + ENVELOPE + 12, SCORE_FILE_BYTES, 4);
    rejected(FORMAT_CORRUPT, sizeof(changed));

    memcpy(changed, block, sizeof(changed));
    uint8_t *global = chunk(changed, 1);
    put(global, 2, 2);
    repair_crc(changed);
    rejected(FORMAT_CORRUPT, sizeof(changed));
    memcpy(changed, block, sizeof(changed));
    global = chunk(changed, 1);
    put(global + 8, 7, 4);
    repair_crc(changed);
    rejected(FORMAT_CORRUPT, sizeof(changed));
    memcpy(changed, block, sizeof(changed));
    global = chunk(changed, 1);
    global[CHUNK + 4] = 1;
    repair_crc(changed);
    rejected(FORMAT_CORRUPT, sizeof(changed));
    memcpy(changed, block, sizeof(changed));
    global = chunk(changed, 1);
    put(global + CHUNK, 29, 2);
    repair_crc(changed);
    rejected(FORMAT_CORRUPT, sizeof(changed));
    memcpy(changed, block, sizeof(changed));
    uint8_t *sounds = chunk(changed, 2);
    sounds[CHUNK + 4] = 9;
    repair_crc(changed);
    rejected(FORMAT_NEWER, sizeof(changed));
    memcpy(changed, block, sizeof(changed));
    sounds = chunk(changed, 2);
    sounds[CHUNK + 14] = 1;
    repair_crc(changed);
    rejected(FORMAT_CORRUPT, sizeof(changed));
    memcpy(changed, block, sizeof(changed));
    uint8_t *lanes = chunk(changed, 3);
    lanes[CHUNK + 6] = 1;
    repair_crc(changed);
    rejected(FORMAT_CORRUPT, sizeof(changed));
    memcpy(changed, block, sizeof(changed));
    lanes = chunk(changed, 3);
    lanes[CHUNK + 2] = 0;
    repair_crc(changed);
    rejected(FORMAT_CORRUPT, sizeof(changed));
    memcpy(changed, block, sizeof(changed));
    lanes = chunk(changed, 3);
    lanes[CHUNK + 3] = 5;
    repair_crc(changed);
    rejected(FORMAT_CORRUPT, sizeof(changed));
    memcpy(changed, block, sizeof(changed));
    lanes = chunk(changed, 3);
    lanes[CHUNK + 12] = 2;
    lanes[CHUNK + 13] = 0;
    repair_crc(changed);
    rejected(FORMAT_CORRUPT, sizeof(changed));
    memcpy(changed, block, sizeof(changed));
    uint8_t *steps = chunk(changed, 4);
    steps[CHUNK] = 64;
    repair_crc(changed);
    rejected(FORMAT_CORRUPT, sizeof(changed));
    memcpy(changed, block, sizeof(changed));
    steps = chunk(changed, 4);
    steps[CHUNK + 1] = 77;
    repair_crc(changed);
    rejected(FORMAT_NEWER, sizeof(changed));
    memcpy(changed, block, sizeof(changed));
    steps = chunk(changed, 4);
    steps[CHUNK + 2] = 9;
    repair_crc(changed);
    rejected(FORMAT_CORRUPT, sizeof(changed));
    // The golden Steps payload begins Cycle, Probability, Relative, Jump,
    // empty, then Note. Mutate each semantic payload without changing shape.
    memcpy(changed, block, sizeof(changed));
    steps = chunk(changed, 4);
    steps[CHUNK + 3] = 1;
    repair_crc(changed);
    rejected(FORMAT_CORRUPT, sizeof(changed));
    memcpy(changed, block, sizeof(changed));
    steps = chunk(changed, 4);
    steps[CHUNK + 11] = 101;
    repair_crc(changed);
    rejected(FORMAT_CORRUPT, sizeof(changed));
    memcpy(changed, block, sizeof(changed));
    steps = chunk(changed, 4);
    steps[CHUNK + 15] = 4;
    repair_crc(changed);
    rejected(FORMAT_CORRUPT, sizeof(changed));
    memcpy(changed, block, sizeof(changed));
    steps = chunk(changed, 4);
    steps[CHUNK + 23] = 0;
    repair_crc(changed);
    rejected(FORMAT_CORRUPT, sizeof(changed));
    memcpy(changed, block, sizeof(changed));
    steps = chunk(changed, 4);
    steps[CHUNK + 28] = 109;
    repair_crc(changed);
    rejected(FORMAT_CORRUPT, sizeof(changed));
}

static void encode_rejects_invalid(void) {
    Score s;
    example(&s);
    TileId id = tile(&s, 21, 10);
    s.tiles[id].next = id;
    memset(changed, 0xa5, sizeof(changed));
    assert(score_format_encode(&s, changed, 1, 1) == FORMAT_CORRUPT);
    assert(changed[0] == 0xa5);
    example(&s);
    assert(score_format_encode(&s, changed, 0, 1) == FORMAT_CORRUPT);
    assert(score_format_encode(&s, changed, 1, 0) == FORMAT_CORRUPT);

    score_init(&s);
    assert(score_create(&s, 0, 0, 64) == SCORE_OK);
    assert(score_create(&s, 70, 0, 4) == SCORE_OK);
    for (int n = 0; n < 1474; n++)
        assert(score_place(&s, n / 64 + 1, n % 64, TILE_NOTE) == SCORE_OK);
    assert(score_remove(&s, 24, 1) == SCORE_OK);
    assert(score_place(&s, 24, 1, TILE_CYCLE) == SCORE_OK);
    assert(score_format_measure(&s) == SCORE_FILE_BYTES);
    TileId tail = s.lanes[0].tiles[23];
    while (s.tiles[tail].next)
        tail = s.tiles[tail].next;
    TileId extra = 1;
    while (s.tiles[extra].value.kind)
        extra++;
    s.tiles[tail].next = extra;
    s.tiles[extra].value = score_default(TILE_NOTE);
    s.tiles[extra].branch = -1;
    assert(score_format_measure(&s) == SCORE_FILE_BYTES + 5);
    memset(changed, 0xa5, sizeof(changed));
    assert(score_format_encode(&s, changed, 1, 1) == FORMAT_FULL);
    assert(changed[0] == 0xa5);
}

int main(int argc, char **argv) {
    golden(argc == 2 && !strcmp(argv[1], "--write-fixture"));
    compatibility();
    invalid_inputs();
    encode_rejects_invalid();
    puts("PASS: deterministic v1 fixture, round trip, compatibility, staged rejection and "
         "validation");
}
