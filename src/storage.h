/*
 * storage.h - Transactional score persistence on a memory card
 *
 * A Storage instance owns fixed buffers and uses one CardBackend session per
 * operation. Save, readback and cleanup preserve the previous valid file.
 */

#ifndef STORAGE_H
#define STORAGE_H

#include "card.h"
#include "score_format.h"

// Logical score slots supported by the card filename scheme.
#define STORAGE_SLOTS 15

// User-facing outcomes, including card errors and cleanup failures.
typedef enum
{
    STORAGE_UNKNOWN,
    STORAGE_EMPTY,
    STORAGE_SAVED,
    STORAGE_BUSY,
    STORAGE_CORRUPT,
    STORAGE_NEWER,
    STORAGE_NO_CARD,
    STORAGE_TIMEOUT,
    STORAGE_CHANGED,
    STORAGE_IO,
    STORAGE_UNFORMATTED,
    STORAGE_CARD_DAMAGED,
    STORAGE_NO_SPACE,
    STORAGE_SCORE_FULL,
    STORAGE_CLEANUP,
    STORAGE_GENERATION_FULL
} StorageResult;

// Scratch buffers, directory inventory and staged incoming score.
typedef struct
{
    CardBackend card;
    StorageResult slots[STORAGE_SLOTS];
    int free_blocks, file_count;
    // The save buffer remains immutable through write, readback and cleanup.
    // Incoming belongs to the replacement request until acknowledgement.
    uint8_t save[SCORE_FILE_BYTES], readback[SCORE_FILE_BYTES];
    CardFile files[15];
    Score incoming;
} Storage;

/* Initializes caller-owned `storage` with a complete `backend` callback table. */
void storage_init(Storage* storage, CardBackend backend);
/* Discovers card status and refreshes `slot` in 1..STORAGE_SLOTS. */
StorageResult storage_refresh(Storage* storage, int slot);
/*
 * Saves the valid `score` snapshot to `slot` in 1..STORAGE_SLOTS and verifies
 * the new file before retiring old generations.
 */
StorageResult storage_save(Storage* storage, int slot, const Score* score);
/* Loads `slot` in 1..STORAGE_SLOTS into `storage->incoming` on success. */
StorageResult storage_load(Storage* storage, int slot);
/* Returns the UI message for a valid `result` in StorageResult. */
const char* storage_message(StorageResult result);

#endif // STORAGE_H
