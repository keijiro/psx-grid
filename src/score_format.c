/*
 * score_format.c - Versioned fixed-size score file codec
 *
 * Implementation notes:
 *
 * Wire fields are emitted explicitly in little-endian order; measurement
 * and encoding share the same traversal to enforce capacity.
 */

#include "score_format.h"

#include <string.h>

enum
{
    HEADER = 32,
    CHUNK = 12,
    GLOBAL = 1,
    SOUNDS = 2,
    LANES = 3,
    STEPS = 4
};

/*
 * Wire waveform tags are explicit so changing the in-memory enum cannot
 * silently reinterpret an existing score file.
 */
static int wave_tag(int wave)
{
    switch (wave)
    {
    case WAVE_SINE:
        return 0;
    case WAVE_TRIANGLE:
        return 1;
    case WAVE_SAW:
        return 2;
    case WAVE_SQUARE:
        return 3;
    case WAVE_NOISE:
        return 4;
    default:
        return -1;
    }
}

/* Returns -1 for an unknown tag so the decoder can report a newer format. */
static int wave_value(unsigned tag)
{
    switch (tag)
    {
    case 0:
        return WAVE_SINE;
    case 1:
        return WAVE_TRIANGLE;
    case 2:
        return WAVE_SAW;
    case 3:
        return WAVE_SQUARE;
    case 4:
        return WAVE_NOISE;
    default:
        return -1;
    }
}

/* Reads an n-byte little-endian integer without host alignment assumptions. */
static uint32_t get(const uint8_t* p, int n)
{
    uint32_t v = 0;
    for (int i = 0; i < n; i++) v |= (uint32_t)p[i] << (8 * i);
    return v;
}

/* Writes an n-byte little-endian integer without host layout assumptions. */
static void put(uint8_t* p, uint32_t v, int n)
{
    for (int i = 0; i < n; i++) p[i] = (uint8_t)(v >> (8 * i));
}

static int zero(const uint8_t* p, size_t n)
{
    for (size_t i = 0; i < n; i++)
    {
        if (p[i]) return 0;
    }
    return 1;
}

/*
 * Treats the checksum field as zero while covering the versioned header and
 * payload, allowing the stored checksum to verify itself.
 */
static uint32_t checksum(const uint8_t* p, size_t n)
{
    uint32_t crc = UINT32_MAX;
    for (size_t i = 0; i < n; i++)
    {
        crc ^= i >= 20 && i < 24 ? 0 : p[i];
        for (int b = 0; b < 8; b++) crc = (crc >> 1) ^ (0xedb88320u & -(crc & 1));
    }
    return ~crc;
}

// Measurement walks exactly the same fields as encoding, with no output buffer.
// Stable wire tags are explicit here rather than coupled to TileKind ordinals.
typedef struct
{
    uint8_t* data;
    size_t at;
} Writer;

static void emit(Writer* w, uint32_t v, int n)
{
    if (w->data) put(w->data + w->at, v, n);
    w->at += n;
}

static void chunk(Writer* w, int tag, size_t bytes)
{
    emit(w, tag, 2);
    emit(w, 1, 2);
    emit(w, 1, 4);
    emit(w, (uint32_t)bytes, 4);
}

/*
 * Serializes lanes by position and maps pool indices to wire indices so
 * equivalent visible scores have stable output.
 */
static int lane_order(const Score* s, int order[SCORE_LANES], int indices[SCORE_LANES])
{
    int count = 0;
    for (int i = 0; i < SCORE_LANES; i++)
    {
        indices[i] = -1;
        if (!s->lanes[i].active) continue;
        int n = count++;
        while (n && (s->lanes[order[n - 1]].y > s->lanes[i].y ||
                     (s->lanes[order[n - 1]].y == s->lanes[i].y && s->lanes[order[n - 1]].x > s->lanes[i].x)))
        {
            order[n] = order[n - 1];
            n--;
        }
        order[n] = i;
    }
    for (int i = 0; i < count; i++) indices[order[i]] = i;
    return count;
}

/*
 * Emits tagged tile payloads using positional branch indices rather than
 * transient tile pool IDs.
 */
static void steps(Writer* w, const Score* s, const int* order, int count, const int* indices)
{
    for (int i = 0; i < count; i++)
    {
        const Lane* l = &s->lanes[order[i]];
        for (int j = 0; j < l->length; j++)
        {
            int n = 0;
            for (TileId t = l->tiles[j]; t; t = s->tiles[t].next) n++;
            emit(w, n, 1);
            for (TileId t = l->tiles[j]; t; t = s->tiles[t].next)
            {
                TileValue v = s->tiles[t].value;
                switch (v.kind)
                {
                case TILE_NOTE:
                    emit(w, 1, 1);
                    emit(w, 3, 1);
                    emit(w, v.pitch, 1);
                    emit(w, v.length, 2);
                    break;
                case TILE_CYCLE:
                    emit(w, 2, 1);
                    emit(w, 5, 1);
                    emit(w, v.period, 1);
                    emit(w, v.pattern, 4);
                    break;
                case TILE_PROBABILITY:
                    emit(w, 3, 1);
                    emit(w, 1, 1);
                    emit(w, v.chance, 1);
                    break;
                case TILE_JUMP:
                    emit(w, 4, 1);
                    emit(w, 1, 1);
                    emit(w, indices[s->tiles[t].branch], 1);
                    break;
                case TILE_RELATIVE:
                    emit(w, 5, 1);
                    emit(w, 5, 1);
                    emit(w, v.lock_mask, 1);
                    emit(w, v.attack, 2);
                    emit(w, v.release, 2);
                    break;
                default:
                    break;
                }
            }
        }
    }
}

/*
 * Uses a dry Writer pass for the variable-size steps chunk, then emits the
 * same traversal into the output buffer.
 */
static size_t payload(const Score* s, uint8_t* data)
{
    int order[SCORE_LANES], indices[SCORE_LANES];
    int count = lane_order(s, order, indices);
    Writer w = {data, 0};
    chunk(&w, GLOBAL, 8);
    emit(&w, s->bpm, 2);
    emit(&w, s->reverb.size, 1);
    emit(&w, s->reverb.amount, 1);
    emit(&w, 0, 4);
    chunk(&w, SOUNDS, 16 * SCORE_CHANNELS);
    for (int i = 0; i < SCORE_CHANNELS; i++)
    {
        SoundSettings v = s->sounds[i];
        emit(&w, v.attack, 2);
        emit(&w, v.release, 2);
        emit(&w, wave_tag(v.wave_a), 1);
        emit(&w, wave_tag(v.wave_b), 1);
        emit(&w, v.mix_attack, 2);
        emit(&w, v.mix_release, 2);
        emit(&w, v.sweep, 1);
        emit(&w, v.decay, 2);
        emit(&w, v.reverb, 1);
        emit(&w, 0, 2);
    }
    chunk(&w, LANES, count * 12);
    for (int i = 0; i < count; i++)
    {
        const Lane* l = &s->lanes[order[i]];
        emit(&w, l->x, 1);
        emit(&w, l->y, 1);
        emit(&w, l->length, 1);
        emit(&w, l->source ? 0 : l->division, 1);
        emit(&w, l->source ? 255 : l->channel, 1);
        emit(&w, !!l->source, 1);
        emit(&w, 0, 4);
        emit(&w, 0, 2);
    }
    Writer measure = {NULL, 0};
    steps(&measure, s, order, count, indices);
    chunk(&w, STEPS, measure.at);
    steps(&w, s, order, count, indices);
    return w.at;
}

size_t score_format_measure(const Score* s)
{
    return SCORE_FILE_WRAPPER + HEADER + payload(s, NULL);
}

FormatResult score_format_encode(const Score* s, uint8_t block[SCORE_FILE_BYTES], int slot, uint32_t generation)
{
    ScoreResult valid = score_validate_import(s);
    if (valid == SCORE_FULL) return FORMAT_FULL;
    if (valid != SCORE_OK) return FORMAT_CORRUPT;
    size_t used = score_format_measure(s);
    if (used > SCORE_FILE_BYTES) return FORMAT_FULL;
    if (slot < 1 || slot > 15 || !generation) return FORMAT_CORRUPT;
    memset(block, 0, SCORE_FILE_BYTES);
    block[0] = 'S';
    block[1] = 'C';
    block[2] = 0x11;
    block[3] = 1;
    // Full-width Shift-JIS ASCII is understood by the console card manager.
    const char title[] = "JACQUARD 00";
    for (unsigned i = 0; i < sizeof(title) - 1; i++)
    {
        int c = title[i];
        if (i == 9) c = '0' + slot / 10;
        if (i == 10) c = '0' + slot % 10;
        unsigned sjis = c == ' ' ? 0x8140 : c >= '0' && c <= '9' ? 0x824f + c - '0' : 0x8260 + c - 'A';
        block[4 + i * 2] = sjis >> 8;
        block[5 + i * 2] = sjis;
    }
    put(block + 98, 0x7fff, 2);
    for (int y = 0; y < 16; y++)
    {
        for (int x = 0; x < 16; x++)
        {
            if (x == 2 || y == 13 || (y == 3 && x > 2 && x < 13) || (x == 12 && y < 14 && y > 2))
            {
                block[128 + y * 8 + x / 2] |= 1 << ((x & 1) * 4);
            }
        }
    }
    uint8_t* h = block + SCORE_FILE_WRAPPER;
    memcpy(h, "JQSC", 4);
    put(h + 4, 1, 2);
    put(h + 6, 1, 2);
    put(h + 8, HEADER, 2);
    put(h + 12, used - SCORE_FILE_WRAPPER - HEADER, 4);
    put(h + 24, generation, 4);
    h[28] = slot;
    payload(s, h + HEADER);
    put(h + 20, checksum(h, used - SCORE_FILE_WRAPPER), 4);
    return FORMAT_OK;
}

void score_format_set_generation(uint8_t block[SCORE_FILE_BYTES], uint32_t generation)
{
    uint8_t* h = block + SCORE_FILE_WRAPPER;
    put(h + 24, generation, 4);
    put(h + 20, checksum(h, HEADER + get(h + 12, 4)), 4);
}

FormatResult score_format_decode(const uint8_t* block, size_t size, Score* s, int* slot, uint32_t* generation)
{
    if (size < SCORE_FILE_WRAPPER + HEADER || size > SCORE_FILE_BYTES || memcmp(block, "SC", 2) || block[2] != 0x11 ||
        block[3] != 1)
        return FORMAT_CORRUPT;
    const uint8_t* h = block + SCORE_FILE_WRAPPER;
    if (memcmp(h, "JQSC", 4)) return FORMAT_CORRUPT;
    size_t header = get(h + 8, 2), bytes = get(h + 12, 4);
    if (header < HEADER || header > size - SCORE_FILE_WRAPPER || bytes > size - SCORE_FILE_WRAPPER - header)
    {
        return FORMAT_CORRUPT;
    }
    if (get(h + 20, 4) != checksum(h, header + bytes)) return FORMAT_CORRUPT;
    if (get(h + 4, 2) != 1 || get(h + 6, 2) > 1 || get(h + 16, 4) || header != HEADER) return FORMAT_NEWER;
    if (!zero(h + 10, 2) || !zero(h + 29, 3) || h[28] < 1 || h[28] > 15 || !get(h + 24, 4)) return FORMAT_CORRUPT;
    const uint8_t* chunks[5] = {0};
    size_t lengths[5] = {0};
    unsigned seen = 0;
    for (size_t at = header; at < header + bytes;)
    {
        if (header + bytes - at < CHUNK) return FORMAT_CORRUPT;
        const uint8_t* c = h + at;
        unsigned tag = get(c, 2), schema = get(c + 2, 2), flags = get(c + 4, 4);
        size_t n = get(c + 8, 4);
        at += CHUNK;
        if (n > header + bytes - at) return FORMAT_CORRUPT;
        if (flags & ~1u) return FORMAT_NEWER;
        if (tag >= GLOBAL && tag <= STEPS)
        {
            if (seen & (1u << tag)) return FORMAT_CORRUPT;
            if (schema != 1 || flags != 1) return FORMAT_NEWER;
            seen |= 1u << tag;
            chunks[tag] = h + at;
            lengths[tag] = n;
        }
        else if (flags & 1) return FORMAT_NEWER;
        at += n;
    }
    if (seen != 30 || lengths[GLOBAL] != 8 || lengths[SOUNDS] != 128 || lengths[LANES] % 12 ||
        lengths[LANES] / 12 > SCORE_LANES)
        return FORMAT_CORRUPT;
    score_init(s);
    const uint8_t* p = chunks[GLOBAL];
    if (!zero(p + 4, 4) || score_set_bpm(s, get(p, 2)) || score_set_reverb(s, (ReverbSettings){p[2], p[3]}))
    {
        return FORMAT_CORRUPT;
    }
    for (int i = 0; i < SCORE_CHANNELS; i++)
    {
        p = chunks[SOUNDS] + 16 * i;
        if (wave_value(p[4]) < 0 || wave_value(p[5]) < 0) return FORMAT_NEWER;
        SoundSettings v = {(int)get(p, 2),   (int)get(p + 2, 2),  wave_value(p[4]),
                           wave_value(p[5]), (int)get(p + 6, 2),  (int)get(p + 8, 2),
                           (int8_t)p[10],    (int)get(p + 11, 2), p[13]};
        if (!zero(p + 14, 2) || score_set_sound(s, i, v)) return FORMAT_CORRUPT;
    }
    int count = lengths[LANES] / 12, roles[SCORE_LANES] = {0};
    for (int i = 0; i < count; i++)
    {
        p = chunks[LANES] + 12 * i;
        if (!zero(p + 6, 6) || p[5] > 1) return FORMAT_CORRUPT;
        if ((p[5] && (p[3] || p[4] != 255)) || (!p[5] && p[4] >= SCORE_CHANNELS)) return FORMAT_CORRUPT;
        if (!p[2] || p[2] > SCORE_STEPS || p[0] + p[2] + 1 >= SCORE_WIDTH || p[1] >= SCORE_HEIGHT)
        {
            return FORMAT_CORRUPT;
        }
        if (i && (p[1] < s->lanes[i - 1].y || (p[1] == s->lanes[i - 1].y && p[0] <= s->lanes[i - 1].x)))
        {
            return FORMAT_CORRUPT;
        }
        Lane* l = &s->lanes[i];
        l->active = 1;
        l->x = p[0];
        l->y = p[1];
        l->length = p[2];
        l->division = p[5] ? 16 : p[3];
        l->channel = p[5] ? -1 : p[4];
        roles[i] = p[5];
        if (!p[5] && score_set_division(s, i, p[3])) return FORMAT_CORRUPT;
    }
    p = chunks[STEPS];
    const uint8_t* end = p + lengths[STEPS];
    TileId next = 1;
    for (int i = 0; i < count; i++)
    {
        for (int j = 0; j < s->lanes[i].length; j++)
        {
            if (p == end) return FORMAT_CORRUPT;
            int n = *p++;
            if (n > SCORE_HEIGHT - s->lanes[i].y) return FORMAT_CORRUPT;
            TileId* link = &s->lanes[i].tiles[j];
            for (int k = 0; k < n; k++)
            {
                if (end - p < 2 || next > SCORE_TILE_CAPACITY) return FORMAT_CORRUPT;
                int tag = *p++, len = *p++;
                if (end - p < len) return FORMAT_CORRUPT;
                TileKind kind;
                int expected;
                switch (tag)
                {
                case 1:
                    kind = TILE_NOTE;
                    expected = 3;
                    break;
                case 2:
                    kind = TILE_CYCLE;
                    expected = 5;
                    break;
                case 3:
                    kind = TILE_PROBABILITY;
                    expected = 1;
                    break;
                case 4:
                    kind = TILE_JUMP;
                    expected = 1;
                    break;
                case 5:
                    kind = TILE_RELATIVE;
                    expected = 5;
                    break;
                default:
                    return FORMAT_NEWER;
                }
                if (len != expected) return FORMAT_CORRUPT;
                TileId id = next++;
                Tile* t = &s->tiles[id];
                t->value = score_default(kind);
                t->branch = -1;
                *link = id;
                link = &t->next;
                switch (kind)
                {
                case TILE_NOTE:
                    t->value.pitch = p[0];
                    t->value.length = get(p + 1, 2);
                    break;
                case TILE_CYCLE:
                    t->value.period = p[0];
                    t->value.pattern = get(p + 1, 4);
                    break;
                case TILE_PROBABILITY:
                    t->value.chance = p[0];
                    break;
                case TILE_JUMP:
                    if (p[0] >= count || !roles[p[0]] || s->lanes[p[0]].source) return FORMAT_CORRUPT;
                    t->branch = p[0];
                    s->lanes[p[0]].source = id;
                    break;
                case TILE_RELATIVE:
                    t->value.lock_mask = p[0];
                    t->value.attack = (int16_t)get(p + 1, 2);
                    t->value.release = (int16_t)get(p + 3, 2);
                    break;
                default:
                    break;
                }
                p += len;
            }
        }
    }
    if (p != end) return FORMAT_CORRUPT;
    for (int i = 0; i < count; i++)
    {
        if (roles[i] != !!s->lanes[i].source) return FORMAT_CORRUPT;
    }
    if (score_validate_import(s) != SCORE_OK || score_format_measure(s) > SCORE_FILE_BYTES) return FORMAT_CORRUPT;
    s->generation = 0;
    for (int i = 0; i < count; i++) s->lane_generation[i] = ++s->generation;
    for (TileId t = 1; t < next; t++) s->tile_generation[t] = ++s->generation;
    s->revision = 1;
    if (slot) *slot = h[28];
    if (generation) *generation = get(h + 24, 4);
    return FORMAT_OK;
}
