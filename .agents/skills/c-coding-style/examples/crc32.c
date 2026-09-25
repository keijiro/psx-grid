/*
 * crc32.c - CRC-32 checksum calculation
 *
 * Implementation notes:
 *
 * The classic table-driven CRC-32 uses a 256-entry table (1 KiB) and
 * processes one byte per lookup. This implementation instead uses a
 * 16-entry table (64 bytes) and processes one nibble per lookup. It is
 * slower, but the table is small enough to be written out as a literal,
 * which removes the need for runtime initialization and keeps the code
 * easy to audit.
 *
 * If throughput becomes a concern, switch to a 256-entry table (or a
 * slicing-by-N variant) without changing the public interface.
 */

#include "crc32.h"

#include <assert.h>
#include <string.h>

/*
 * Lookup table for the reflected polynomial 0xEDB88320.
 *
 * Entry n is the CRC register obtained by shifting the 4-bit value n
 * through the polynomial division four times. Each entry can be
 * regenerated with:
 *
 *   uint32_t c = n;
 *   for (int k = 0; k < 4; k++) c = (c >> 1) ^ (c & 1 ? 0xEDB88320u : 0);
 */
static const uint32_t nibble_table[16] =
{
    0x00000000u, 0x1DB71064u, 0x3B6E20C8u, 0x26D930ACu,
    0x76DC4190u, 0x6B6B51F4u, 0x4DB26158u, 0x5005713Cu,
    0xEDB88320u, 0xF00F9344u, 0xD6D6A3E8u, 0xCB61B38Cu,
    0x9B64C2B0u, 0x86D3D2D4u, 0xA00AE278u, 0xBDBDF21Cu
};

/*
 * Feeds a single byte into the CRC register.
 *
 * Because the CRC is reflected (LSB first), the incoming byte is XORed
 * into the low end of the register, and the register shifts right.
 */
static uint32_t step(uint32_t c, uint8_t b)
{
    c ^= b;

    // Low nibble, then high nibble.
    c = (c >> 4) ^ nibble_table[c & 0xF];
    c = (c >> 4) ^ nibble_table[c & 0xF];

    return c;
}

uint32_t crc32_update(uint32_t crc, const void* data, size_t size)
{
    // A NULL buffer is a caller bug unless there is nothing to read.
    assert(data != NULL || size == 0);

    // Accessing the buffer through an unsigned char type is always valid,
    // regardless of the type of the original object.
    const uint8_t* p = data;
    for (size_t i = 0; i < size; i++) crc = step(crc, p[i]);

    return crc;
}

uint32_t crc32_bytes(const void* data, size_t size)
{
    return crc32_final(crc32_update(CRC32_INIT, data, size));
}

uint32_t crc32_string(const char* str)
{
    assert(str != NULL);

    // Measuring the length first lets us reuse the buffer path as is.
    // The extra pass over the string is negligible next to the CRC itself.
    return crc32_bytes(str, strlen(str));
}
