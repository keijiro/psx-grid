/*
 * card.h - Memory-card session interface
 *
 * Storage uses a CardBackend session to separate card filesystem ownership
 * and BIOS event handling from save-file discovery and transactional updates.
 * The backend owns any filesystem allocation; one main-thread session may be
 * active at a time.
 */

#ifndef CARD_H
#define CARD_H

#include <stdint.h>

// Card and filesystem outcomes, including media changes during a session.
typedef enum
{
    CARD_OK,                        // The operation completed.
    CARD_END,                       // Directory enumeration reached its end.
    CARD_MISSING,                   // No card is present.
    CARD_TIMEOUT,                   // The BIOS request timed out.
    CARD_CHANGED,                   // The card changed during the session.
    CARD_IO,                        // A filesystem operation failed.
    CARD_UNFORMATTED,               // The card lacks a filesystem.
    CARD_DAMAGED                    // The card cannot be used safely.
} CardResult;

// One directory entry with a card-local name and byte size.
typedef struct
{
    char name[21];
    int size;
} CardFile;

// The caller owns one complete session. A failed begin releases everything it
// acquired; a successful begin has exactly one end. Names exclude "bu00:". The
// backend owns allocation, directory updates and bad-sector remapping.
typedef struct
{
    void* context;
    CardResult (*begin)(void*);
    void (*end)(void*);
    CardResult (*check)(void*);
    CardResult (*list)(void*, int index, CardFile*);
    CardResult (*open_read)(void*, const char* name);
    CardResult (*open_create)(void*, const char* name);
    CardResult (*read)(void*, uint8_t* data, int size);
    CardResult (*write)(void*, const uint8_t* data, int size);
    CardResult (*close)(void*);
    CardResult (*erase)(void*, const char* name);
} CardBackend;

/*
 * Returns the BIOS-backed implementation; one session may be active at a time.
 */
CardBackend card_platform_backend(void);

#endif // CARD_H
