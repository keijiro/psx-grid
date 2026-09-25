/*
 * kv.c - Line-oriented key=value parser
 *
 * Implementation notes:
 *
 * The parser never writes to the input. Names and values are returned as
 * spans into the caller's line, so the caller decides whether a copy is
 * needed at all. The cost is that spans are not NUL-terminated;
 * kv_span_equals() and the value parsers exist so that callers rarely need
 * to copy one just to inspect it.
 *
 * Input is assumed to be ASCII. Characters are classified with explicit
 * ranges rather than <ctype.h>, whose results depend on the current locale
 * and are undefined for negative char values.
 *
 * The grammar has no quoting and no escapes, so a value cannot contain '#'
 * or ';'. Adding quoting later only touches the value scan in
 * kv_parse_line().
 */

#include "kv.h"

#include <assert.h>
#include <string.h>

#define COUNT_OF(a) (sizeof(a) / sizeof((a)[0]))

// Character classes of the line grammar.
typedef enum
{
    CHAR_END,
    CHAR_SPACE,
    CHAR_COMMENT,
    CHAR_EQUALS,
    CHAR_NAME,
    CHAR_OTHER
} CharClass;

// Maps a spelling accepted by kv_parse_bool() to its value.
typedef struct
{
    const char* word;
    bool value;
} BoolWord;

/*
 * Spellings accepted by kv_parse_bool().
 *
 * Settings files are edited by hand as often as they are written by tools,
 * so the common spellings are all accepted rather than just "true" and
 * "false".
 */
static const BoolWord bool_words[] =
{
    {"true", true}, {"on", true}, {"yes", true},
    {"false", false}, {"off", false}, {"no", false}
};

static CharClass classify(char c)
{
    switch (c)
    {
    case '\0':
    case '\n':
        return CHAR_END;
    case ' ':
    case '\t':
    case '\r': // Lets CRLF files be split on '\n' alone.
        return CHAR_SPACE;
    case '#':
    case ';':
        return CHAR_COMMENT;
    case '=':
        return CHAR_EQUALS;
    case '_':
    case '.':
    case '-':
        return CHAR_NAME;
    default:
        break;
    }

    if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')) return CHAR_NAME;
    if (c >= '0' && c <= '9') return CHAR_NAME;

    return CHAR_OTHER;
}

static const char* skip_space(const char* p)
{
    while (classify(*p) == CHAR_SPACE) p++;
    return p;
}

/*
 * Returns the value of a digit in any base up to 16, or UINT32_MAX for any
 * other character, so that a single `>= base` test rejects both invalid
 * characters and digits too large for the base.
 */
static uint32_t digit_value(char c)
{
    if (c >= '0' && c <= '9') return (uint32_t)(c - '0');
    if (c >= 'a' && c <= 'f') return (uint32_t)(c - 'a' + 10);
    if (c >= 'A' && c <= 'F') return (uint32_t)(c - 'A' + 10);
    return UINT32_MAX;
}

KvResult kv_parse_line(const char* line, KvPair* out)
{
    assert(line != NULL);
    assert(out != NULL);

    const char* p = skip_space(line);

    CharClass first = classify(*p);
    if (first == CHAR_END || first == CHAR_COMMENT) return KV_EMPTY;
    if (first != CHAR_NAME) return KV_ERROR_NAME;

    const char* name = p;
    while (classify(*p) == CHAR_NAME) p++;
    size_t name_len = (size_t)(p - name);
    if (name_len > KV_NAME_MAX) return KV_ERROR_LENGTH;

    p = skip_space(p);
    if (classify(*p) != CHAR_EQUALS) return KV_ERROR_EQUALS;
    p = skip_space(p + 1);

    // Tracking the end of the last visible character trims trailing
    // whitespace in the same pass that finds the comment.
    const char* value = p;
    const char* end = p;
    for (;; p++)
    {
        CharClass c = classify(*p);
        if (c == CHAR_END || c == CHAR_COMMENT) break;
        if (c != CHAR_SPACE) end = p + 1;
    }

    *out = (KvPair){{name, name_len}, {value, (size_t)(end - value)}};
    return KV_OK;
}

bool kv_span_equals(KvSpan s, const char* str)
{
    assert(str != NULL);

    size_t len = strlen(str);
    return s.len == len && memcmp(s.ptr, str, len) == 0;
}

bool kv_parse_bool(KvSpan s, bool* out)
{
    assert(out != NULL);

    for (size_t i = 0; i < COUNT_OF(bool_words); i++)
    {
        if (kv_span_equals(s, bool_words[i].word))
        {
            *out = bool_words[i].value;
            return true;
        }
    }

    return false;
}

bool kv_parse_uint(KvSpan s, uint32_t* out)
{
    assert(out != NULL);

    if (s.len == 0) return false;

    uint32_t base;
    size_t i;
    if (s.len > 2 && s.ptr[0] == '0' && (s.ptr[1] == 'x' || s.ptr[1] == 'X'))
    {
        base = 16u;
        i = 2;
    }
    else
    {
        base = 10u;
        i = 0;
    }

    uint32_t v = 0;
    for (; i < s.len; i++)
    {
        uint32_t d = digit_value(s.ptr[i]);
        if (d >= base) return false;

        // Testing before the update keeps the arithmetic itself in range,
        // so the check needs no wider type.
        if (v > (UINT32_MAX - d) / base) return false;
        v = v * base + d;
    }

    *out = v;
    return true;
}
