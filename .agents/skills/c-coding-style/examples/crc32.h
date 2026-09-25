/*
 * crc32.h - CRC-32 checksum calculation
 *
 * Computes the CRC-32 used by IEEE 802.3, zlib, PNG, and gzip
 * (reflected polynomial 0xEDB88320, initial value and final XOR 0xFFFFFFFF).
 * The check value for the ASCII string "123456789" is 0xCBF43926.
 *
 * All functions are pure and hold no global mutable state, so they are
 * safe to call from multiple threads.
 */

#ifndef CRC32_H
#define CRC32_H

#include <stddef.h>
#include <stdint.h>

/*
 * Usage
 *
 * One-shot:
 *   uint32_t crc = crc32_string("hello");
 *
 * Incremental (e.g. for data that arrives in chunks):
 *   uint32_t crc = CRC32_INIT;
 *   crc = crc32_update(crc, buf1, len1);
 *   crc = crc32_update(crc, buf2, len2);
 *   crc = crc32_final(crc);
 */

// Initial register value for incremental calculation.
#define CRC32_INIT 0xFFFFFFFFu

/*
 * Feeds `size` bytes from `data` into a running CRC register and returns
 * the updated register. The returned value is an intermediate state; pass
 * it to crc32_final() to obtain the checksum.
 *
 * `data` may be NULL only when `size` is zero.
 */
uint32_t crc32_update(uint32_t crc, const void* data, size_t size);

/*
 * Converts a running CRC register into the final checksum.
 */
static inline uint32_t crc32_final(uint32_t crc) { return crc ^ 0xFFFFFFFFu; }

/*
 * Returns the CRC-32 of a byte buffer.
 *
 * `data` may be NULL only when `size` is zero.
 */
uint32_t crc32_bytes(const void* data, size_t size);

/*
 * Returns the CRC-32 of a NUL-terminated string. The terminator itself is
 * not included in the calculation.
 *
 * `str` must not be NULL.
 */
uint32_t crc32_string(const char* str);

#endif // CRC32_H
