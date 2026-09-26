/*
 * storage_test.c - Transactional storage regression tests
 *
 * Implementation notes:
 *
 * An in-memory card backend injects failures during discovery, readback and
 * cleanup to check durable slot behavior.
 */

#include "value_api.h"

#include "storage.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

// One card directory entry and its complete one-block save payload.
typedef struct
{
    CardFile info;
    uint8_t data[SCORE_FILE_BYTES];
} File;
/*
 * Failure switches and counters keep each injected fault local to a card
 * operation, while `mutate` probes whether save captured its input snapshot.
 */
typedef struct
{
    File files[15];
    int count;
    int begun;
    int ends;
    int handle;
    int creating;
    int fail_begin;
    int fail_check;
    int fail_create;
    int fail_read;
    int fail_write;
    int fail_close;
    int fail_erase;
    int short_read;
    int short_write;
    int reads;
    int fail_read_at;
    int short_read_at;
    int lists;
    int fail_list_at;
    CardResult failure;
    Score* mutate;
} MemoryCard;

static CardResult begin(void* context)
{
    MemoryCard* c = context;
    if (c->fail_begin) return c->failure;
    assert(!c->begun);
    c->begun = 1;
    c->handle = -1;
    return CARD_OK;
}

static void end(void* context)
{
    MemoryCard* c = context;
    assert(c->begun && c->handle == -1);
    c->begun = 0;
    c->ends++;
}

static CardResult check(void* context)
{
    MemoryCard* c = context;
    assert(c->begun);
    return c->fail_check ? c->failure : CARD_OK;
}

/*
 * Injects directory failures and preserves card enumeration order for
 * generation tie checks.
 */
static CardResult list(void* context, int index, CardFile* file)
{
    MemoryCard* c = context;
    assert(c->begun && c->handle == -1);
    c->lists++;

    if (c->lists == c->fail_list_at) return c->failure;
    if (index >= c->count) return CARD_END;
    *file = c->files[index].info;
    return CARD_OK;
}

static CardResult open_read(void* context, const char* name)
{
    MemoryCard* c = context;
    assert(c->begun && c->handle == -1);
    for (int i = 0; i < c->count; i++)
    {
        if (!strcmp(c->files[i].info.name, name))
        {
            c->handle = i;
            c->creating = 0;
            return CARD_OK;
        }
    }
    return CARD_IO;
}

/*
 * Enforces the card's 15-file ceiling if storage tries to create past it.
 */
static CardResult open_create(void* context, const char* name)
{
    MemoryCard* c = context;
    assert(c->begun && c->handle == -1);
    if (c->fail_create) return c->failure;
    if (c->count == 15) return CARD_IO;

    for (int i = 0; i < c->count; i++)
    {
        assert(strcmp(c->files[i].info.name, name));
    }

    File* f = &c->files[c->count];
    memset(f, 0, sizeof(*f));
    strcpy(f->info.name, name);
    f->info.size = SCORE_FILE_BYTES;
    c->handle = c->count++;
    c->creating = 1;
    return CARD_OK;
}

/*
 * Injects transfer failures and short reads at chosen calls to test readback
 * handling.
 */
static CardResult read_file(void* context, uint8_t* data, int size)
{
    MemoryCard* c = context;
    assert(c->begun && c->handle >= 0 && !c->creating &&
           size == SCORE_FILE_BYTES);
    c->reads++;
    if (c->fail_read || c->reads == c->fail_read_at) return c->failure;

    int short_read = c->short_read || c->reads == c->short_read_at;
    memcpy(data, c->files[c->handle].data, short_read ? size / 2 : size);
    return short_read ? CARD_IO : CARD_OK;
}

/*
 * Injects write failures and short writes without changing the previous valid
 * file.
 */
static CardResult write_file(void* context, const uint8_t* data, int size)
{
    MemoryCard* c = context;
    assert(c->begun && c->handle >= 0 && c->creating &&
           size == SCORE_FILE_BYTES);
    if (c->fail_write) return c->failure;
    memcpy(c->files[c->handle].data, data, c->short_write ? size / 16 : size);
    if (c->mutate)
    {
        c->mutate->bpm++;
        c->mutate = NULL;
    }
    return c->short_write ? CARD_IO : CARD_OK;
}

static CardResult close_file(void* context)
{
    MemoryCard* c = context;
    assert(c->begun && c->handle >= 0);
    int creating = c->creating;
    c->handle = -1;
    return c->fail_close && creating ? c->failure : CARD_OK;
}

static CardResult erase_file(void* context, const char* name)
{
    MemoryCard* c = context;
    assert(c->begun && c->handle == -1);
    if (c->fail_erase) return c->failure;

    for (int i = 0; i < c->count; i++)
    {
        if (!strcmp(c->files[i].info.name, name))
        {
            memmove(&c->files[i], &c->files[i + 1],
                    (c->count - i - 1) * sizeof(File));
            c->count--;
            return CARD_OK;
        }
    }
    return CARD_IO;
}

static CardBackend backend(MemoryCard* c)
{
    return (CardBackend){c,          begin,      end,         check,
                         list,       open_read,  open_create, read_file,
                         write_file, close_file, erase_file};
}

static Score make_score(int bpm)
{
    Score s;
    score_init(&s);
    s.bpm = bpm;
    assert(score_create(&s, 0, 0, 4) == SCORE_OK);
    assert(score_place(&s, 1, 0, TILE_NOTE) == SCORE_OK);
    return s;
}

static void put32(uint8_t* p, uint32_t v)
{
    for (int i = 0; i < 4; i++) p[i] = (uint8_t)(v >> (8 * i));
}

static uint32_t checksum(const uint8_t* p, size_t n)
{
    uint32_t crc = UINT32_MAX;
    for (size_t i = 0; i < n; i++)
    {
        crc ^= i >= 20 && i < 24 ? 0 : p[i];
        for (int b = 0; b < 8; b++)
        {
            crc = (crc >> 1) ^ (0xedb88320u & -(crc & 1));
        }
    }
    return ~crc;
}

/*
 * Changes a valid fixture to a newer format version and repairs its checksum.
 */
static void set_newer(File* file)
{
    uint8_t* header = file->data + SCORE_FILE_WRAPPER;
    header[4] = 2;
    uint32_t bytes = (uint32_t)header[12] | (uint32_t)header[13] << 8 |
                     (uint32_t)header[14] << 16 | (uint32_t)header[15] << 24;
    put32(header + 20, checksum(header, 32 + bytes));
}

static StorageResult load(MemoryCard* c, Score* score)
{
    Storage s;
    test_storage_init(&s, backend(c));
    StorageResult r = storage_load(&s, 1);
    if (r == STORAGE_SAVED) *score = s.incoming;
    assert(!c->begun);
    return r;
}

static void save(MemoryCard* c, int bpm)
{
    Storage s;
    Score score = make_score(bpm);
    test_storage_init(&s, backend(c));
    assert(storage_save(&s, 1, &score) == STORAGE_SAVED);
    assert(!c->begun);
}

/*
 * Checks save and load through valid generations after an earlier file remains
 * present.
 */
static void basic_recovery(void)
{
    MemoryCard c = {0};
    Score recovered;
    c.failure = CARD_IO;
    save(&c, 101);
    assert(c.count == 1 &&
           !strcmp(c.files[0].info.name, "BIJACQUARD0100000001"));
    assert(load(&c, &recovered) == STORAGE_SAVED && recovered.bpm == 101);
    save(&c, 202);
    assert(c.count == 1 &&
           !strcmp(c.files[0].info.name, "BIJACQUARD0100000002"));
    assert(load(&c, &recovered) == STORAGE_SAVED && recovered.bpm == 202);
    c.files[0].data[600] ^= 1;
    assert(load(&c, &recovered) == STORAGE_CORRUPT);
}

/*
 * Injects write failures before verification to confirm the previous
 * generation remains loadable.
 */
static void write_failures(void)
{
    for (int failure = 0; failure < 4; failure++)
    {
        MemoryCard c = {0};
        Storage s;
        Score score = make_score(202);
        Score recovered;
        c.failure = CARD_IO;
        save(&c, 101);
        c.fail_create = failure == 0;
        c.fail_write = failure == 1;
        c.short_write = failure == 2;
        c.fail_close = failure == 3;
        test_storage_init(&s, backend(&c));
        assert(storage_save(&s, 1, &score) == STORAGE_IO);
        c.fail_create = c.fail_write = c.short_write = c.fail_close = 0;
        assert(load(&c, &recovered) == STORAGE_SAVED &&
               recovered.bpm == (failure == 3 ? 202 : 101));
        assert(!c.begun);
    }
}

/*
 * Injects readback failures so an unverified new file cannot replace the valid
 * one.
 */
static void readback_failures(void)
{
    for (int failure = 0; failure < 2; failure++)
    {
        MemoryCard c = {0};
        Storage s;
        Score score = make_score(202);
        Score recovered;
        c.failure = CARD_IO;
        save(&c, 101);
        c.reads = 0;
        if (failure == 0) c.fail_read_at = 2;
        else c.short_read_at = 2;
        test_storage_init(&s, backend(&c));
        assert(storage_save(&s, 1, &score) == STORAGE_IO);
        assert(c.count == 2 &&
               !strcmp(c.files[0].info.name, "BIJACQUARD0100000001"));
        c.fail_read_at = c.short_read_at = 0;
        assert(load(&c, &recovered) == STORAGE_SAVED && recovered.bpm == 202);
        assert(!c.begun);
    }
}

/*
 * Checks full-card behavior and cleanup only after a new generation is
 * verified.
 */
static void capacity_and_cleanup(void)
{
    MemoryCard c = {0};
    Storage s;
    Score score = make_score(202);
    Score recovered;
    c.failure = CARD_IO;
    save(&c, 101);
    for (int i = 1; i < 15; i++)
    {
        File* f = &c.files[c.count++];
        snprintf(f->info.name, sizeof(f->info.name), "OTHER%02d", i);
        f->info.size = SCORE_FILE_BYTES;
        memset(f->data, i, sizeof(f->data));
    }
    File unrelated[14];
    memcpy(unrelated, &c.files[1], sizeof(unrelated));
    test_storage_init(&s, backend(&c));
    assert(storage_save(&s, 1, &score) == STORAGE_NO_SPACE);
    assert(c.count == 15 && load(&c, &recovered) == STORAGE_SAVED &&
           recovered.bpm == 101);
    assert(!memcmp(unrelated, &c.files[1], sizeof(unrelated)));
    c.count = 1;
    c.fail_erase = 1;
    test_storage_init(&s, backend(&c));
    assert(storage_save(&s, 1, &score) == STORAGE_CLEANUP);
    assert(c.count == 2);
    c.fail_erase = 0;
    assert(load(&c, &recovered) == STORAGE_SAVED && recovered.bpm == 202);
    save(&c, 250);
    assert(c.count == 1 && load(&c, &recovered) == STORAGE_SAVED &&
           recovered.bpm == 250);
}

/*
 * Checks corrupt high generations and exhaustion of the generation counter.
 */
static void corrupt_higher_and_overflow(void)
{
    MemoryCard c = {0};
    Storage s;
    Score score = make_score(202);
    Score recovered;
    c.failure = CARD_IO;
    save(&c, 101);
    File* partial = &c.files[c.count++];
    memset(partial, 0, sizeof(*partial));
    strcpy(partial->info.name, "BIJACQUARD0100000009");
    partial->info.size = SCORE_FILE_BYTES;
    assert(load(&c, &recovered) == STORAGE_SAVED && recovered.bpm == 101);
    test_storage_init(&s, backend(&c));
    assert(storage_save(&s, 1, &score) == STORAGE_SAVED);
    assert(!strcmp(c.files[c.count - 1].info.name, "BIJACQUARD010000000A"));
    strcpy(c.files[c.count - 1].info.name, "BIJACQUARD01FFFFFFFF");
    test_storage_init(&s, backend(&c));
    assert(storage_save(&s, 1, &score) == STORAGE_GENERATION_FULL);
}

/*
 * Checks newer-version precedence and deterministic selection among equal
 * generations.
 */
static void newer_and_generation_ties(void)
{
    MemoryCard c = {0};
    Storage s;
    Score recovered;
    Score newer = make_score(202);
    c.failure = CARD_IO;
    save(&c, 101);
    File* file = &c.files[c.count++];
    memset(file, 0, sizeof(*file));
    strcpy(file->info.name, "BIJACQUARD0100000002");
    file->info.size = SCORE_FILE_BYTES;
    assert(score_format_encode(&newer, file->data, 1, 2) == FORMAT_OK);
    set_newer(file);
    test_storage_init(&s, backend(&c));
    assert(storage_refresh(&s, 1) == STORAGE_NEWER);
    assert(storage_load(&s, 1) == STORAGE_NEWER);
    assert(storage_save(&s, 1, &newer) == STORAGE_NEWER);
    assert(c.count == 2 && !c.begun && c.ends == 4);

    c.count = 0;
    save(&c, 101);
    c.files[1] = c.files[0];
    c.count = 2;
    Score alternate = make_score(202);
    assert(score_format_encode(&alternate, c.files[1].data, 1, 1) == FORMAT_OK);
    assert(load(&c, &recovered) == STORAGE_SAVED && recovered.bpm == 101);
}

/*
 * Checks malformed directory entries and backend results before slot
 * discovery.
 */
static void directory_and_result_errors(void)
{
    static const struct
    {
        CardResult card;
        StorageResult storage;
    } cases[] =
    {
        {
            CARD_MISSING, STORAGE_NO_CARD
        },
        {
            CARD_TIMEOUT, STORAGE_TIMEOUT
        },
        {
            CARD_CHANGED, STORAGE_CHANGED
        },
        {
            CARD_UNFORMATTED, STORAGE_UNFORMATTED
        },
        {
            CARD_DAMAGED, STORAGE_CARD_DAMAGED
        },
        {
            CARD_IO, STORAGE_IO
        }
    };
    Score score = make_score(120);
    for (unsigned i = 0; i < sizeof(cases) / sizeof(cases[0]); i++)
    {
        MemoryCard c = {.fail_begin = 1, .failure = cases[i].card};
        Storage s;
        test_storage_init(&s, backend(&c));
        assert(storage_save(&s, 1, &score) == cases[i].storage);
        assert(!c.begun && !c.ends);
    }
    MemoryCard c = {.failure = CARD_CHANGED, .fail_list_at = 1};
    Storage s;
    test_storage_init(&s, backend(&c));
    assert(storage_refresh(&s, 1) == STORAGE_CHANGED && !c.begun &&
           c.ends == 1);
    memset(&c, 0, sizeof(c));
    c.count = 1;
    strcpy(c.files[0].info.name, "OTHER");
    c.files[0].info.size = SCORE_FILE_BYTES + 1;
    test_storage_init(&s, backend(&c));
    assert(storage_refresh(&s, 1) == STORAGE_CARD_DAMAGED && !c.begun &&
           c.ends == 1);
}

/*
 * Injects media changes at I/O boundaries so a mixed-card operation cannot
 * succeed.
 */
static void card_change_during_operations(void)
{
    for (int operation = 0; operation < 4; operation++)
    {
        MemoryCard c = {0};
        Storage s;
        Score score = make_score(202);
        Score recovered;
        c.failure = CARD_IO;
        save(&c, 101);
        c.failure = CARD_CHANGED;
        if (operation == 0)
        {
            c.reads = 0;
            c.fail_read_at = 1;
        }
        if (operation == 1) c.fail_write = 1;
        if (operation == 2) c.fail_close = 1;
        if (operation == 3) c.fail_erase = 1;
        int ends = c.ends;
        test_storage_init(&s, backend(&c));
        assert(storage_save(&s, 1, &score) == STORAGE_CHANGED);
        assert(!c.begun && c.ends == ends + 1);
        c.fail_read_at = c.fail_write = c.fail_close = c.fail_erase = 0;
        assert(load(&c, &recovered) == STORAGE_SAVED);
        assert(recovered.bpm == (operation >= 2 ? 202 : 101));
    }
}

/*
 * Checks that failed session entry and teardown do not leave a usable stale
 * handle.
 */
static void session_failures(void)
{
    MemoryCard c = {0};
    Storage s;
    Score score = make_score(202);
    c.failure = CARD_CHANGED;
    c.fail_begin = 1;
    test_storage_init(&s, backend(&c));
    assert(storage_save(&s, 1, &score) == STORAGE_CHANGED && !c.begun &&
           !c.ends);
    c.fail_begin = 0;
    c.fail_check = 1;
    assert(storage_save(&s, 1, &score) == STORAGE_CHANGED && !c.begun &&
           c.ends == 1);
}

int main(void)
{
    basic_recovery();
    write_failures();
    readback_failures();
    capacity_and_cleanup();
    corrupt_higher_and_overflow();
    newer_and_generation_ties();
    directory_and_result_errors();
    card_change_during_operations();
    session_failures();
    puts("storage_test: passed");
    return 0;
}
