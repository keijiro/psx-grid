#ifndef SCORE_FORMAT_H
#define SCORE_FORMAT_H
#include "score.h"
#include <stddef.h>
#define SCORE_FILE_BYTES 8192
#define SCORE_FILE_WRAPPER 512
typedef enum { FORMAT_OK, FORMAT_CORRUPT, FORMAT_NEWER, FORMAT_FULL } FormatResult;
// Staging belongs exclusively to the caller. A failed decode may modify it;
// publish it only after FORMAT_OK. No codec entry point allocates memory.
size_t score_format_measure(const Score *score);
FormatResult score_format_encode(const Score *score, uint8_t block[SCORE_FILE_BYTES], int slot, uint32_t generation);
// Stamp an owned, successfully encoded buffer after directory discovery and
// before any file write. Musical content remains the originally captured score.
void score_format_set_generation(uint8_t block[SCORE_FILE_BYTES], uint32_t generation);
FormatResult score_format_decode(const uint8_t *block, size_t size, Score *staging, int *slot, uint32_t *generation);
#endif
