/*
 * score_format.h - Fixed-size memory-card score serialization
 *
 * Encoding and decoding use caller-owned buffers. The wrapper and payload
 * have explicit sizes so card I/O never depends on host structure layout.
 * No codec entry point allocates memory.
 */

#ifndef SCORE_FORMAT_H
#define SCORE_FORMAT_H

#include "score.h"

#include <stddef.h>
#include <stdint.h>

// One complete card file and its console-facing metadata wrapper.
#define SCORE_FILE_BYTES 8192
#define SCORE_FILE_WRAPPER 512

// Decode and encode outcomes; FORMAT_NEWER preserves forward compatibility.
typedef enum
{
    FORMAT_OK,                      // The file was accepted.
    FORMAT_CORRUPT,                 // The bytes violate the format.
    FORMAT_NEWER,                   // A newer schema requires another reader.
    FORMAT_FULL                     // The score exceeds the fixed file.
} FormatResult;

/*
 * Returns file bytes needed for a valid `score`, including its wrapper.
 * The caller validates the score before measuring it.
 * `score` must not be NULL.
 */
size_t score_format_measure(const Score* score);
/*
 * Encodes `score` into `block` for a 1-based slot in 1..15 and a nonzero
 * generation. Returns FORMAT_FULL if the score exceeds the fixed file size,
 * or FORMAT_CORRUPT for invalid content, slot or generation. Either failure
 * leaves `block` unchanged.
 * `score` and `block` must not be NULL.
 */
FormatResult score_format_encode(const Score* score,
                                 uint8_t block[SCORE_FILE_BYTES], int slot,
                                 uint32_t generation);
/*
 * Stamps an owned, successfully encoded buffer with a nonzero generation after
 * directory discovery and before any file write. Musical content remains the
 * captured score. `generation` must be nonzero.
 * `block` must not be NULL.
 */
void score_format_set_generation(uint8_t block[SCORE_FILE_BYTES],
                                 uint32_t generation);
/*
 * Decodes `size` bytes from `block` into caller-owned `staging`. A failed
 * decode may change staging, so publish it only after FORMAT_OK. `slot` and
 * `generation` may be NULL; non-NULL outputs are valid only on FORMAT_OK.
 * `block` and `staging` must not be NULL.
 */
FormatResult score_format_decode(const uint8_t* block, size_t size,
                                 Score* staging, int* slot,
                                 uint32_t* generation);

#endif // SCORE_FORMAT_H
