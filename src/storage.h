#ifndef STORAGE_H
#define STORAGE_H
#include "card.h"
#include "score_format.h"
#define STORAGE_SLOTS 15
typedef enum { STORAGE_UNKNOWN, STORAGE_EMPTY, STORAGE_SAVED, STORAGE_BUSY, STORAGE_CORRUPT, STORAGE_NEWER,
    STORAGE_NO_CARD, STORAGE_TIMEOUT, STORAGE_CHANGED, STORAGE_IO, STORAGE_UNFORMATTED, STORAGE_CARD_DAMAGED,
    STORAGE_NO_SPACE, STORAGE_SCORE_FULL, STORAGE_CLEANUP, STORAGE_GENERATION_FULL } StorageResult;
typedef struct {
    CardBackend card;
    StorageResult slots[STORAGE_SLOTS];
    int free_blocks, directory_ready;
    // The save buffer remains immutable through write, readback and cleanup.
    // Incoming is staging, then belongs to the replacement request until ack.
    uint8_t save[SCORE_FILE_BYTES], readback[SCORE_FILE_BYTES], directory[16][128];
    unsigned broken[20];
    Score incoming;
} Storage;
void storage_init(Storage *storage,CardBackend backend);
StorageResult storage_refresh(Storage *storage,int slot);
StorageResult storage_save(Storage *storage,int slot,const Score *score);
StorageResult storage_load(Storage *storage,int slot);
const char *storage_message(StorageResult result);
#endif
