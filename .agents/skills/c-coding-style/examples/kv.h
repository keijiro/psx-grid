/*
 * kv.h - Line-oriented key=value parser
 *
 * Parses settings lines of the form
 *
 *   name = value   # comment
 *
 * one line at a time. A name consists of ASCII letters, digits, '_', '.',
 * and '-'. The value is everything after '=' up to a '#' or ';' comment or
 * the end of the line, with surrounding whitespace removed; it may be empty.
 *
 * The parser holds no state and never allocates, so it is safe to call
 * from multiple threads.
 */

#ifndef KV_H
#define KV_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/*
 * Usage
 *
 *   KvPair pair;
 *   KvResult r = kv_parse_line(line, &pair);
 *
 *   if (kv_is_error(r))
 *   {
 *       report_error(line_number, r);
 *   }
 *   else if (r == KV_OK && kv_span_equals(pair.name, "volume"))
 *   {
 *       kv_parse_uint(pair.value, &volume);
 *   }
 */

// Longest accepted name, so that a copy fits a 32-byte buffer with its NUL.
#define KV_NAME_MAX 31

// Outcome of parsing one line. Errors sort after the non-error results.
typedef enum
{
    KV_OK,           // A name and a value were found.
    KV_EMPTY,        // Blank or comment-only line.
    KV_ERROR_NAME,   // The line does not start with a valid name.
    KV_ERROR_LENGTH, // The name is longer than KV_NAME_MAX.
    KV_ERROR_EQUALS  // The name is not followed by '='.
} KvResult;

// A range of characters inside the caller's line; not NUL-terminated.
typedef struct
{
    const char* ptr;
    size_t len;
} KvSpan;

// One parsed line. Both spans point into the line that was parsed.
typedef struct
{
    KvSpan name;
    KvSpan value;
} KvPair;

/*
 * Returns true if `r` reports a malformed line.
 */
static inline bool kv_is_error(KvResult r) { return r >= KV_ERROR_NAME; }

/*
 * Parses one NUL-terminated line. A trailing newline, as left by fgets(),
 * ends the line just like the terminator.
 *
 * On KV_OK, stores the name and value in `out` as spans into `line`, which
 * stay valid as long as `line` does. On any other result, `out` is left
 * unchanged.
 *
 * `line` and `out` must not be NULL.
 */
KvResult kv_parse_line(const char* line, KvPair* out);

/*
 * Returns true if the span holds exactly the characters of `str`.
 *
 * `str` must not be NULL.
 */
bool kv_span_equals(KvSpan s, const char* str);

/*
 * Parses a boolean value: "true", "on", or "yes" for true, and "false",
 * "off", or "no" for false. Matching is case-sensitive.
 *
 * Returns false and leaves `out` unchanged if the span is none of these.
 * `out` must not be NULL.
 */
bool kv_parse_bool(KvSpan s, bool* out);

/*
 * Parses an unsigned integer, either decimal or hexadecimal with a "0x"
 * prefix. Signs and embedded whitespace are not accepted.
 *
 * Returns false and leaves `out` unchanged if the span is malformed or the
 * value does not fit in 32 bits. `out` must not be NULL.
 */
bool kv_parse_uint(KvSpan s, uint32_t* out);

#endif // KV_H
