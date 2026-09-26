/*
 * storage.h - Transactional score persistence on a memory card
 *
 * A caller-owned Storage instance keeps fixed buffers and uses one CardBackend
 * session per operation. Main-thread save, readback and cleanup preserve the
 * previous valid file; callers serialize access to the instance.
 */

#ifndef STORAGE_H
#define STORAGE_H

#include "storage/card.h"
#include "score_format.h"

#include <stdint.h>

// Logical score slots supported by the card filename scheme.
#define STORAGE_SLOTS 15

// User-facing outcomes, including card errors and cleanup failures.
typedef enum
{
    STORAGE_UNKNOWN,                // The slot has not been inspected.
    STORAGE_EMPTY,                  // The slot has no score.
    STORAGE_SAVED,                  // The save completed and was verified.
    STORAGE_BUSY,                   // The operation cannot start yet.
    STORAGE_CORRUPT,                // A score file failed validation.
    STORAGE_NEWER,                  // A newer score format was found.
    STORAGE_NO_CARD,                // No card is present.
    STORAGE_TIMEOUT,                // The card operation timed out.
    STORAGE_CHANGED,                // The card changed during I/O.
    STORAGE_IO,                     // A card operation failed.
    STORAGE_UNFORMATTED,            // The card lacks a filesystem.
    STORAGE_CARD_DAMAGED,           // The card is damaged.
    STORAGE_NO_SPACE,               // The card has insufficient space.
    STORAGE_SCORE_FULL,             // The score exceeds one card file.
    STORAGE_CLEANUP,                // Retiring an old file failed.
    STORAGE_GENERATION_FULL         // The revision counter cannot advance.
} StorageResult;

// Scratch buffers, directory inventory and staged incoming score.
typedef struct
{
    CardBackend card;
    StorageResult slots[STORAGE_SLOTS];
    int free_blocks;
    int file_count;
    // The save buffer remains immutable through write, readback and cleanup.
    // Incoming belongs to the replacement request until acknowledgement.
    uint8_t save[SCORE_FILE_BYTES];
    uint8_t readback[SCORE_FILE_BYTES];
    CardFile files[15];
    Score incoming;
} Storage;

/*
 * Initializes caller-owned `storage` with a complete `backend` callback table.
 * `storage` and `backend` must not be NULL or overlap.
 */
void storage_init(Storage* storage, const CardBackend* backend);
/*
 * Discovers card status and refreshes `slot` in 1..STORAGE_SLOTS. Returns a
 * StorageResult and records the same result for the slot.
 * `storage` must not be NULL.
 */
StorageResult storage_refresh(Storage* storage, int slot);
/*
 * Saves the valid `score` snapshot to `slot` in 1..STORAGE_SLOTS and verifies
 * the new file before retiring old generations. Returns the card or format
 * failure while preserving the last verified generation.
 * `storage` and `score` must not be NULL.
 */
StorageResult storage_save(Storage* storage, int slot, const Score* score);
/*
 * Loads `slot` in 1..STORAGE_SLOTS into `storage->incoming` on success.
 * On failure, `incoming` must not be published.
 * `storage` must not be NULL.
 */
StorageResult storage_load(Storage* storage, int slot);
/*
 * Returns the UI message for a valid `result` in StorageResult.
 */
const char* storage_message(StorageResult result);

#endif // STORAGE_H
