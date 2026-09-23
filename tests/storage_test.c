#include "storage.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

enum { SECTORS = 1024 };

typedef struct {
    uint8_t sectors[SECTORS][128];
    int present, begun, ends;
    int fail_read_sector, fail_write_sector;
    int fail_read_skip, fail_write_skip;
    int fail_begin, fail_read_at;
    unsigned reads, writes, begins;
    CardResult failure;
    Score *mutate;
} MemoryCard;

static uint8_t checksum(const uint8_t *data) {
    uint8_t value = 0;
    for (int i = 0; i < 127; i++) value ^= data[i];
    return value;
}

static uint32_t crc32(const uint8_t *data, size_t size) {
    uint32_t value = UINT32_MAX;
    for (size_t i = 0; i < size; i++) {
        value ^= i >= 20 && i < 24 ? 0 : data[i];
        for (int bit = 0; bit < 8; bit++)
            value = (value >> 1) ^ (0xedb88320u & -(value & 1));
    }
    return ~value;
}

static void put32(uint8_t *data, uint32_t value) {
    for (int i = 0; i < 4; i++) data[i] = (uint8_t)(value >> (i * 8));
}

static void set_remap(MemoryCard *card, int index, uint32_t sector) {
    uint8_t *entry = card->sectors[16 + index];
    put32(entry, sector);
    entry[127] = checksum(entry);
}

static void initialize_card(MemoryCard *card) {
    memset(card, 0, sizeof(*card));
    card->present = 1;
    card->fail_read_sector = card->fail_write_sector = -1;
    card->failure = CARD_IO;
    for (int sector = 0; sector < 16; sector++) {
        if (sector) card->sectors[sector][0] = 0xa0;
        card->sectors[sector][127] = checksum(card->sectors[sector]);
    }
    memcpy(card->sectors[0], "MC", 2);
    card->sectors[0][127] = checksum(card->sectors[0]);
    for (int sector = 16; sector < 36; sector++) {
        memset(card->sectors[sector], 0xff, 4);
        card->sectors[sector][127] = checksum(card->sectors[sector]);
    }
}

static CardResult begin(void *context) {
    MemoryCard *card = context;
    card->begins++;
    if (card->fail_begin) return card->failure;
    if (!card->present) return CARD_MISSING;
    assert(!card->begun);
    card->begun = 1;
    return CARD_OK;
}

static void end(void *context) {
    MemoryCard *card = context;
    assert(card->begun);
    card->begun = 0;
    card->ends++;
}

static CardResult read_sector(void *context, unsigned sector, uint8_t data[128]) {
    MemoryCard *card = context;
    assert(card->begun && sector < SECTORS);
    card->reads++;
    if (card->fail_read_at > 0 && card->reads == (unsigned)card->fail_read_at)
        return card->failure;
    if ((int)sector == card->fail_read_sector && !card->fail_read_skip--)
        return card->failure;
    memcpy(data, card->sectors[sector], 128);
    return CARD_OK;
}

static CardResult write_sector(void *context, unsigned sector, const uint8_t data[128]) {
    MemoryCard *card = context;
    assert(card->begun && sector < SECTORS);
    card->writes++;
    if ((int)sector == card->fail_write_sector && !card->fail_write_skip--)
        return card->failure;
    memcpy(card->sectors[sector], data, 128);
    if (card->mutate) {
        card->mutate->bpm++;
        card->mutate = NULL;
    }
    return CARD_OK;
}

static uint32_t card_fingerprint(const MemoryCard *card) {
    uint32_t value = 2166136261u;
    for (int sector = 0; sector < SECTORS; sector++)
        for (int byte = 0; byte < 128; byte++)
            value = (value ^ card->sectors[sector][byte]) * 16777619u;
    return value;
}

static CardBackend backend(MemoryCard *card) {
    CardBackend result = {card, begin, end, read_sector, write_sector};
    return result;
}

static Score make_score(int bpm) {
    Score score;
    score_init(&score);
    score.bpm = bpm;
    assert(score_create(&score, 0, 0, 4) == SCORE_OK);
    assert(score_place(&score, 1, 0, TILE_NOTE) == SCORE_OK);
    return score;
}

static StorageResult restart_load(MemoryCard *card, int slot, Score *score) {
    Storage storage;
    storage_init(&storage, backend(card));
    StorageResult result = storage_load(&storage, slot);
    if (result == STORAGE_SAVED) *score = storage.incoming;
    assert(!card->begun);
    return result;
}

static void initial_save(MemoryCard *card, int slot, int bpm) {
    Storage storage;
    Score score = make_score(bpm);
    storage_init(&storage, backend(card));
    assert(storage_save(&storage, slot, &score) == STORAGE_SAVED);
    assert(!card->begun);
}

static void metadata_failures(void) {
    for (int changed = 0; changed < 2; changed++) {
        for (int read = 1; read <= 37; read++) {
            MemoryCard card;
            Storage storage;
            Score replacement = make_score(202), recovered;
            initialize_card(&card);
            initial_save(&card, 1, 101);
            uint32_t before = card_fingerprint(&card);
            unsigned ends = (unsigned)card.ends;
            card.reads = card.writes = 0;
            card.fail_read_at = read;
            card.failure = changed ? CARD_CHANGED : CARD_IO;
            storage_init(&storage, backend(&card));
            assert(storage_save(&storage, 1, &replacement) ==
                (changed ? STORAGE_CHANGED : STORAGE_IO));
            assert(!card.begun && (unsigned)card.ends == ends + 1);
            assert(card.writes == 0 && card_fingerprint(&card) == before);
            card.fail_read_at = 0;
            card.failure = CARD_IO;
            assert(restart_load(&card, 1, &recovered) == STORAGE_SAVED);
            assert(recovered.bpm == 101);
        }
    }

    MemoryCard card;
    Storage storage;
    Score score = make_score(120);
    initialize_card(&card);
    card.fail_begin = 1;
    card.failure = CARD_CHANGED;
    storage_init(&storage, backend(&card));
    assert(storage_save(&storage, 1, &score) == STORAGE_CHANGED);
    assert(card.begins == 1 && card.ends == 0 && !card.begun);
    assert(card.reads == 0 && card.writes == 0);
}

static void remapping(void) {
    MemoryCard card;
    Storage storage;
    Score score = make_score(123), recovered;

    initialize_card(&card);
    set_remap(&card, 0, 64);
    storage_init(&storage, backend(&card));
    assert(storage_save(&storage, 1, &score) == STORAGE_SAVED);
    assert(!memcmp(card.sectors[36], "SC", 2));
    assert(memcmp(card.sectors[64], "SC", 2));
    assert(restart_load(&card, 1, &recovered) == STORAGE_SAVED);
    assert(recovered.bpm == 123);

    initialize_card(&card);
    memcpy(card.sectors[36], card.sectors[1], 128);
    set_remap(&card, 0, 1);
    storage_init(&storage, backend(&card));
    assert(storage_save(&storage, 1, &score) == STORAGE_SAVED);
    assert(card.sectors[1][0] == 0xa0);
    assert(card.sectors[36][0] == 0x51);
    assert(restart_load(&card, 1, &recovered) == STORAGE_SAVED);
    assert(recovered.bpm == 123);

    for (int malformed = 0; malformed < 2; malformed++) {
        initialize_card(&card);
        set_remap(&card, 0, malformed ? 16 : 64);
        if (!malformed) set_remap(&card, 1, 64);
        uint32_t before = card_fingerprint(&card);
        card.reads = card.writes = 0;
        storage_init(&storage, backend(&card));
        assert(storage_save(&storage, 1, &score) == STORAGE_CARD_DAMAGED);
        assert(card.writes == 0 && card_fingerprint(&card) == before);
        assert(!card.begun);
    }
}

static void interrupted_payload(void) {
    for (int sector = 128; sector < 192; sector++) {
        MemoryCard card;
        initialize_card(&card);
        initial_save(&card, 1, 101);
        Storage storage;
        Score replacement = make_score(202), recovered;
        storage_init(&storage, backend(&card));
        card.fail_write_sector = sector;
        assert(storage_save(&storage, 1, &replacement) == STORAGE_IO);
        card.fail_write_sector = -1;
        assert(restart_load(&card, 1, &recovered) == STORAGE_SAVED);
        assert(recovered.bpm == 101);
    }
    for (int sector = 128; sector < 192; sector++) {
        MemoryCard card;
        initialize_card(&card);
        initial_save(&card, 1, 101);
        Storage storage;
        Score replacement = make_score(202), recovered;
        storage_init(&storage, backend(&card));
        card.fail_read_sector = sector;
        assert(storage_save(&storage, 1, &replacement) == STORAGE_IO);
        card.fail_read_sector = -1;
        assert(restart_load(&card, 1, &recovered) == STORAGE_SAVED);
        assert(recovered.bpm == 101);
    }
}

static void commit_and_cleanup(void) {
    MemoryCard card;
    Storage storage;
    Score old = make_score(101), replacement = make_score(202), recovered;

    initialize_card(&card);
    initial_save(&card, 1, 101);
    storage_init(&storage, backend(&card));
    card.fail_write_sector = 2;
    assert(storage_save(&storage, 1, &replacement) == STORAGE_IO);
    card.fail_write_sector = -1;
    assert(restart_load(&card, 1, &recovered) == STORAGE_SAVED && recovered.bpm == old.bpm);

    initialize_card(&card);
    initial_save(&card, 1, 101);
    storage_init(&storage, backend(&card));
    card.fail_read_sector = 2;
    card.fail_read_skip = 1;
    assert(storage_save(&storage, 1, &replacement) == STORAGE_IO);
    card.fail_read_sector = -1;
    assert(restart_load(&card, 1, &recovered) == STORAGE_SAVED && recovered.bpm == replacement.bpm);

    initialize_card(&card);
    initial_save(&card, 1, 101);
    storage_init(&storage, backend(&card));
    card.fail_write_sector = 1;
    assert(storage_save(&storage, 1, &replacement) == STORAGE_CLEANUP);
    card.fail_write_sector = -1;
    assert(restart_load(&card, 1, &recovered) == STORAGE_SAVED && recovered.bpm == replacement.bpm);
    storage_init(&storage, backend(&card));
    assert(storage_save(&storage, 1, &replacement) == STORAGE_SAVED);

    initialize_card(&card);
    initial_save(&card, 1, 101);
    storage_init(&storage, backend(&card));
    card.fail_read_sector = 1;
    card.fail_read_skip = 1;
    assert(storage_save(&storage, 1, &replacement) == STORAGE_CLEANUP);
    card.fail_read_sector = -1;
    assert(restart_load(&card, 1, &recovered) == STORAGE_SAVED && recovered.bpm == replacement.bpm);

    initialize_card(&card);
    initial_save(&card, 1, 101);
    storage_init(&storage, backend(&card));
    card.fail_write_sector = 130;
    card.failure = CARD_CHANGED;
    assert(storage_save(&storage, 1, &replacement) == STORAGE_CHANGED);
    assert(!card.begun);
    card.fail_write_sector = -1;
    card.failure = CARD_IO;
    assert(restart_load(&card, 1, &recovered) == STORAGE_SAVED && recovered.bpm == 101);
}

static void corrupt_latest_falls_back(void) {
    MemoryCard card;
    Storage storage;
    Score replacement = make_score(202), recovered;
    initialize_card(&card);
    initial_save(&card, 1, 101);
    storage_init(&storage, backend(&card));
    card.fail_write_sector = 1;
    assert(storage_save(&storage, 1, &replacement) == STORAGE_CLEANUP);
    card.fail_write_sector = -1;
    card.sectors[128 + 4][42] ^= 1;
    assert(restart_load(&card, 1, &recovered) == STORAGE_SAVED);
    assert(recovered.bpm == 101);
}

static void cleanup_retry_discovery(void) {
    MemoryCard card;
    Storage storage;
    Score replacement = make_score(202), recovered;
    initialize_card(&card);
    initial_save(&card, 1, 101);
    storage_init(&storage, backend(&card));
    card.fail_write_sector = 1;
    assert(storage_save(&storage, 1, &replacement) == STORAGE_CLEANUP);
    card.fail_write_sector = -1;

    uint32_t before = card_fingerprint(&card);
    card.reads = card.writes = 0;
    storage_init(&storage, backend(&card));
    card.fail_read_sector = 64;
    assert(storage_save(&storage, 1, &replacement) == STORAGE_IO);
    assert(card.writes == 0 && card_fingerprint(&card) == before && !card.begun);
    card.fail_read_sector = -1;
    assert(restart_load(&card, 1, &recovered) == STORAGE_SAVED);
    assert(recovered.bpm == 202);

    storage_init(&storage, backend(&card));
    assert(storage_save(&storage, 1, &replacement) == STORAGE_SAVED);
    assert(card.sectors[1][0] == 0x51);
    assert((card.sectors[2][0] & 0xf0) == 0xa0);
    assert(restart_load(&card, 1, &recovered) == STORAGE_SAVED);
    assert(recovered.bpm == 202);
}

static void directory_damage_and_unrelated_files(void) {
    MemoryCard card;
    Storage storage;
    Score score = make_score(130), recovered;
    initialize_card(&card);
    initial_save(&card, 1, 120);
    card.sectors[15][10] = 'X';
    storage_init(&storage, backend(&card));
    assert(storage_refresh(&storage, 1) == STORAGE_SAVED);
    assert(storage_save(&storage, 1, &score) == STORAGE_CARD_DAMAGED);
    assert(restart_load(&card, 1, &recovered) == STORAGE_SAVED && recovered.bpm == 120);

    initialize_card(&card);
    initial_save(&card, 1, 120);
    uint8_t unrelated[128] = {0};
    unrelated[0] = 0x51;
    unrelated[5] = 0x20;
    unrelated[8] = unrelated[9] = 255;
    memcpy(unrelated + 10, "BASLUS-OTHER-SAVE", 17);
    unrelated[127] = checksum(unrelated);
    memcpy(card.sectors[8], unrelated, 128);
    storage_init(&storage, backend(&card));
    assert(storage_save(&storage, 1, &score) == STORAGE_SAVED);
    assert(!memcmp(card.sectors[8], unrelated, 128));
}

static void full_card_and_replacement(void) {
    MemoryCard card;
    Storage storage;
    Score score = make_score(140), recovered;
    initialize_card(&card);
    for (int block = 1; block < 16; block++) {
        uint8_t *entry = card.sectors[block];
        memset(entry, 0, 128);
        entry[0] = 0x51;
        entry[5] = 0x20;
        entry[8] = entry[9] = 255;
        snprintf((char *)entry + 10, 21, "OTHERFILE%011d", block);
        entry[127] = checksum(entry);
    }
    storage_init(&storage, backend(&card));
    assert(storage_save(&storage, 1, &score) == STORAGE_NO_SPACE);

    initialize_card(&card);
    initial_save(&card, 1, 120);
    for (int block = 2; block < 16; block++) {
        uint8_t *entry = card.sectors[block];
        memset(entry, 0, 128);
        entry[0] = 0x51;
        entry[5] = 0x20;
        entry[8] = entry[9] = 255;
        snprintf((char *)entry + 10, 21, "OTHERFILE%011d", block);
        entry[127] = checksum(entry);
    }
    storage_init(&storage, backend(&card));
    assert(storage_save(&storage, 1, &score) == STORAGE_NO_SPACE);
    assert(restart_load(&card, 1, &recovered) == STORAGE_SAVED && recovered.bpm == 120);

    initialize_card(&card);
    storage_init(&storage, backend(&card));
    card.present = 0;
    assert(storage_save(&storage, 1, &score) == STORAGE_NO_CARD);
    card.present = 1;
    assert(storage_save(&storage, 1, &score) == STORAGE_SAVED);
}

static void newer_and_immutable_snapshot(void) {
    MemoryCard card;
    Storage storage;
    Score original = make_score(120), recovered;
    initialize_card(&card);
    storage_init(&storage, backend(&card));
    card.mutate = &original;
    assert(storage_save(&storage, 1, &original) == STORAGE_SAVED);
    assert(original.bpm == 121);
    assert(restart_load(&card, 1, &recovered) == STORAGE_SAVED && recovered.bpm == 120);

    /* A committed unsupported envelope at the newest generation blocks both
       fallback and overwrite, preserving its possible musical meaning. */
    uint8_t block[SCORE_FILE_BYTES];
    for (int i = 0; i < 64; i++) memcpy(block + i * 128, card.sectors[64 + i], 128);
    uint8_t *envelope = block + SCORE_FILE_WRAPPER;
    envelope[4] = 2;
    size_t payload = (size_t)envelope[12] | (size_t)envelope[13] << 8 |
        (size_t)envelope[14] << 16 | (size_t)envelope[15] << 24;
    put32(envelope + 20, crc32(envelope, 32 + payload));
    for (int i = 0; i < 64; i++) memcpy(card.sectors[64 + i], block + i * 128, 128);
    storage_init(&storage, backend(&card));
    assert(storage_refresh(&storage, 1) == STORAGE_NEWER);
    assert(storage_load(&storage, 1) == STORAGE_NEWER);
    assert(storage_save(&storage, 1, &original) == STORAGE_NEWER);
}

int main(void) {
    metadata_failures();
    remapping();
    interrupted_payload();
    commit_and_cleanup();
    corrupt_latest_falls_back();
    cleanup_retry_discovery();
    directory_damage_and_unrelated_files();
    full_card_and_replacement();
    newer_and_immutable_snapshot();
    puts("storage tests passed");
}
