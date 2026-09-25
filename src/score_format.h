/*
 * score_format.h - Fixed-size memory-card score serialization
 *
 * Encoding and decoding use caller-owned buffers. The wrapper and payload
 * have explicit sizes so card I/O never depends on host structure layout.
 */

#ifndef SCORE_FORMAT_H
#define SCORE_FORMAT_H

#include "score.h"

#include <stddef.h>

// One complete card file and its console-facing metadata wrapper.
#define SCORE_FILE_BYTES 8192
#define SCORE_FILE_WRAPPER 512

// Decode and encode outcomes; FORMAT_NEWER preserves forward compatibility.
typedef enum { FORMAT_OK, FORMAT_CORRUPT, FORMAT_NEWER, FORMAT_FULL } FormatResult;

// Staging belongs exclusively to the caller. A failed decode may modify it;
// publish it only after FORMAT_OK. No codec entry point allocates memory.
/* Returns file bytes needed for a valid `score`, including its wrapper. */
size_t score_format_measure(const Score* score);
/*
 * Encodes `score` into `block` for a 1-based slot in 1..15 and a nonzero
 * generation. FORMAT_FULL means the valid score exceeds the fixed file size.
 */
FormatResult score_format_encode(const Score* score,
                                 uint8_t block[SCORE_FILE_BYTES],
                                 int slot,
                                 uint32_t generation);
/*
 * Stamps an owned, successfully encoded buffer with a nonzero generation after
 * directory discovery and before any file write. Musical content remains the
 * captured score.
 */
void score_format_set_generation(uint8_t block[SCORE_FILE_BYTES], uint32_t generation);
/*
 * Decodes `size` bytes from `block` into caller-owned staging. A failed decode
 * may change staging. `slot` and `generation` may be NULL; non-NULL outputs
 * are valid only on FORMAT_OK.
 */
FormatResult score_format_decode(
    const uint8_t* block, size_t size, Score* staging, int* slot, uint32_t* generation);

#endif // SCORE_FORMAT_H
