#ifndef CARD_H
#define CARD_H
#include <stdint.h>
typedef enum { CARD_OK, CARD_MISSING, CARD_TIMEOUT, CARD_CHANGED, CARD_IO, CARD_UNFORMATTED, CARD_DAMAGED } CardResult;
// Operations are synchronous on the main thread, with bounded backend waits.
// A successful begin owns the bus until end, including all failure paths.
// Backends must abort on a detected card replacement during that interval.
typedef struct {
    void *context;
    CardResult (*begin)(void *);
    void (*end)(void *);
    CardResult (*read)(void *,unsigned sector,uint8_t data[128]);
    CardResult (*write)(void *,unsigned sector,const uint8_t data[128]);
} CardBackend;
CardBackend card_platform_backend(void);
#endif
