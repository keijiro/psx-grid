#ifndef CARD_H
#define CARD_H
#include <stdint.h>

typedef enum {
    CARD_OK,
    CARD_END,
    CARD_MISSING,
    CARD_TIMEOUT,
    CARD_CHANGED,
    CARD_IO,
    CARD_UNFORMATTED,
    CARD_DAMAGED
} CardResult;

typedef struct {
    char name[21];
    int size;
} CardFile;

// The caller owns one complete session. A failed begin releases everything it
// acquired; a successful begin has exactly one end. Names exclude "bu00:".
// The backend owns allocation, directory updates and bad-sector remapping.
typedef struct {
    void *context;
    CardResult (*begin)(void *);
    void (*end)(void *);
    CardResult (*check)(void *);
    CardResult (*list)(void *, int index, CardFile *);
    CardResult (*open_read)(void *, const char *name);
    CardResult (*open_create)(void *, const char *name);
    CardResult (*read)(void *, uint8_t *data, int size);
    CardResult (*write)(void *, const uint8_t *data, int size);
    CardResult (*close)(void *);
    CardResult (*erase)(void *, const char *name);
} CardBackend;

CardBackend card_platform_backend(void);
#endif
