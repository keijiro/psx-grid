/*
 * score.c - Editable score model and geometry validation
 *
 * Implementation notes:
 *
 * Public edits keep the grid geometry, branch ancestry and fixed save-file
 * capacity consistent as one model boundary.
 */

#include "score.h"

#include <string.h>

#include "score_format.h"

const int score_divisions[] =
{
    1, 2, 3, 4, 6, 8, 12, 16, 24, 32, 48, 64
};
/*
 * A single scratch score makes multi-object edits atomic without heap
 * allocation or large console-stack frames. Model calls are synchronous and
 * non-reentrant; occupied is the scratch geometry map used by validation.
 */
static Score scratch;
static uint8_t occupied[SCORE_HEIGHT][SCORE_WIDTH];

static int valid(const Score* s, int i)
{
    return i >= 0 && i < SCORE_LANES && s->lanes[i].active;
}

void score_init(Score* s)
{
    memset(s, 0, sizeof(*s));
    for (int i = 0; i < SCORE_CHANNELS; i++) s->sounds[i] = SOUND_DEFAULT;
    s->bpm = 120;
    s->reverb = REVERB_DEFAULT;
}

TileValue score_default(TileKind k)
{
    TileValue v = {k, 48, 20, 4, 50, 1, 0, 0, 0};
    return v;
}

const char* score_note_name(int p)
{
    static const char* n[] =
    {
        "C",  "C#", "D",  "D#", "E",  "F",
        "F#", "G",  "G#", "A",  "A#", "B"
    };
    return n[p % 12];
}

/*
 * Rejects tile settings that would make playback arithmetic or saved-file
 * interpretation invalid.
 */
static int value_valid(TileValue v)
{
    if (v.kind <= TILE_NONE || v.kind >= TILE_KIND_COUNT) return 0;
    if (v.pitch < 0 || v.pitch > 108 || v.length < 5 || v.length > 1280)
    {
        return 0;
    }
    if (v.period < 2 || v.period > 32 || v.chance < 0 || v.chance > 100)
    {
        return 0;
    }
    if (v.lock_mask < 0 || v.lock_mask > 3) return 0;
    if (v.attack < -SOUND_MAX_MS || v.attack > SOUND_MAX_MS) return 0;
    if (v.release < -SOUND_MAX_MS || v.release > SOUND_MAX_MS) return 0;
    return ((v.lock_mask & LOCK_ATTACK) || !v.attack) &&
           ((v.lock_mask & LOCK_RELEASE) || !v.release);
}

ScoreResult score_edit(Score* s, TileId id, TileValue v)
{
    if (!id || id > SCORE_TILE_CAPACITY || s->tiles[id].value.kind != v.kind ||
        !value_valid(v))
    {
        return SCORE_INVALID;
    }
    s->tiles[id].value = v;
    s->revision++;
    return SCORE_OK;
}

/*
 * Follows live stack links to recover the lane owning a branch source tile
 * without a second mutable parent index.
 */
static int owner(const Score* s, TileId id)
{
    for (int i = 0; i < SCORE_LANES; i++)
    {
        if (valid(s, i))
        {
            for (int j = 0; j < s->lanes[i].length; j++)
            {
                for (TileId t = s->lanes[i].tiles[j]; t; t = s->tiles[t].next)
                {
                    if (t == id) return i;
                }
            }
        }
    }
    return -1;
}

int score_division(const Score* s, int i)
{
    for (int n = 0; n < SCORE_LANES && valid(s, i); n++)
    {
        if (!s->lanes[i].source) return s->lanes[i].division;
        i = owner(s, s->lanes[i].source);
    }
    return 16;
}

int score_channel(const Score* s, int i)
{
    for (int n = 0; n < SCORE_LANES && valid(s, i); n++)
    {
        if (!s->lanes[i].source) return s->lanes[i].channel;
        i = owner(s, s->lanes[i].source);
    }
    return -1;
}

ScoreResult score_set_channel(Score* s, int i, int channel)
{
    if (!valid(s, i) || s->lanes[i].source || channel < 0 ||
        channel >= SCORE_CHANNELS)
    {
        return SCORE_INVALID;
    }
    s->lanes[i].channel = channel;
    s->revision++;
    return SCORE_OK;
}

ScoreResult score_set_division(Score* s, int i, int d)
{
    if (!valid(s, i) || s->lanes[i].source) return SCORE_INVALID;
    for (int j = 0; j < SCORE_DIVISIONS; j++)
    {
        if (d == score_divisions[j])
        {
            s->lanes[i].division = d;
            s->revision++;
            return SCORE_OK;
        }
    }
    return SCORE_INVALID;
}

Cell score_at(const Score* s, int x, int y)
{
    Cell c = {CELL_EMPTY, -1, -1, 0, 0};
    for (int i = 0; i < SCORE_LANES; i++)
    {
        const Lane* l = &s->lanes[i];
        if (!l->active || x < l->x || x > l->x + l->length + 1 || y < l->y)
        {
            continue;
        }
        int step = x - l->x - 1;
        int depth = y - l->y;
        if (!depth && (step == -1 || step == l->length))
        {
            return (Cell){step == -1 ? CELL_HEAD : CELL_END, i, step, 0, 0};
        }
        if (step < 0 || step >= l->length) continue;
        TileId t = l->tiles[step];
        for (int d = 0; t && d < depth; d++) t = s->tiles[t].next;
        if (t || !depth)
        {
            return (Cell){t ? CELL_TILE : CELL_STEP, i, step, depth, t};
        }
    }
    return c;
}

Cell score_resolve(const Score* s, int x, int y)
{
    Cell c = score_at(s, x, y);
    if (c.kind != CELL_EMPTY) return c;
    if (y > 0)
    {
        c = score_at(s, x, y - 1);
        if (c.kind == CELL_TILE)
        {
            c.kind = CELL_STEP;
            c.depth++;
            c.tile = 0;
            return c;
        }
    }
    return (Cell){CELL_EMPTY, -1, -1, 0, 0};
}

static ScoreResult mark(int x, int y)
{
    if (x < 0 || x >= SCORE_WIDTH || y < 0 || y >= SCORE_HEIGHT)
    {
        return SCORE_BOUNDS;
    }
    if (occupied[y][x]) return SCORE_COLLISION;
    occupied[y][x] = 1;
    return SCORE_OK;
}

/*
 * Checks rendered geometry and branch ancestry after all links are known to be
 * safe to traverse.
 */
static ScoreResult validate(const Score* s)
{
    memset(occupied, 0, sizeof(occupied));
    for (int i = 0; i < SCORE_LANES; i++)
    {
        if (valid(s, i))
        {
            const Lane* l = &s->lanes[i];
            if (l->length < 1 || l->length > SCORE_STEPS) return SCORE_BOUNDS;
            for (int j = -1; j <= l->length; j++)
            {
                ScoreResult r = mark(l->x + j + 1, l->y);
                if (r) return r;
                if (j < 0 || j == l->length) continue;
                int d = 0;
                for (TileId t = l->tiles[j]; t; t = s->tiles[t].next)
                {
                    if (d++)
                    {
                        r = mark(l->x + j + 1, l->y + d - 1);
                        if (r) return r;
                    }
                }
            }
            int parent = i;
            for (int n = 0; s->lanes[parent].source; n++)
            {
                parent = owner(s, s->lanes[parent].source);
                if (parent < 0) return SCORE_INVALID;
                if (parent == i || n >= SCORE_LANES) return SCORE_CYCLE;
            }
        }
    }
    return SCORE_OK;
}

/*
 * Applies both grid geometry and fixed save-file capacity before a staged edit
 * may commit.
 */
static ScoreResult admission(const Score* s)
{
    ScoreResult r = validate(s);
    if (r) return r;
    return score_format_measure(s) > SCORE_FILE_BYTES ? SCORE_FULL : SCORE_OK;
}

ScoreResult score_validate_import(const Score* s)
{
    uint8_t seen[SCORE_TILE_CAPACITY + 1] =
    {
        0
    };
    // Establish finite, uniquely owned links before the geometry validator or
    // ancestry lookup can traverse untrusted input. The codec builds links, but
    // this boundary also protects future importers from pool corruption.
    for (int i = 0; i < SCORE_LANES; i++)
    {
        if (s->lanes[i].active)
        {
            const Lane* l = &s->lanes[i];
            if (l->length < 1 || l->length > SCORE_STEPS || l->x < 0 ||
                l->x >= SCORE_WIDTH || l->y < 0 || l->y >= SCORE_HEIGHT)
            {
                return SCORE_INVALID;
            }
            if (l->source > SCORE_TILE_CAPACITY ||
                (l->source ? l->channel != -1
                           : l->channel < 0 || l->channel >= SCORE_CHANNELS))
            {
                return SCORE_INVALID;
            }
            int division = 0;
            for (int d = 0; d < SCORE_DIVISIONS; d++)
            {
                if (l->division == score_divisions[d]) division = 1;
            }
            if (!division) return SCORE_INVALID;
            for (int j = 0; j < SCORE_STEPS; j++)
            {
                if (j >= l->length && l->tiles[j]) return SCORE_INVALID;
                int depth = 0;
                for (TileId t = l->tiles[j]; t;)
                {
                    if (t > SCORE_TILE_CAPACITY || seen[t] ||
                        ++depth > SCORE_HEIGHT - l->y)
                    {
                        return SCORE_INVALID;
                    }
                    seen[t] = 1;
                    const Tile* v = &s->tiles[t];
                    if (!value_valid(v->value)) return SCORE_INVALID;
                    if (v->value.kind == TILE_JUMP &&
                        (!valid(s, v->branch) ||
                         s->lanes[v->branch].source != t))
                    {
                        return SCORE_INVALID;
                    }
                    t = v->next;
                }
            }
        }
    }
    for (int i = 0; i < SCORE_LANES; i++)
    {
        if (s->lanes[i].active && s->lanes[i].source)
        {
            TileId t = s->lanes[i].source;
            if (!seen[t] || s->tiles[t].value.kind != TILE_JUMP ||
                s->tiles[t].branch != i)
            {
                return SCORE_INVALID;
            }
        }
    }
    for (int t = 1; t <= SCORE_TILE_CAPACITY; t++)
    {
        if (!!s->tiles[t].value.kind != !!seen[t]) return SCORE_INVALID;
    }
    return validate(s);
}

/*
 * Publishes the shared scratch score only after admission, preserving the
 * caller score on failure.
 */
static ScoreResult commit(Score* s)
{
    ScoreResult r = admission(&scratch);
    if (!r)
    {
        scratch.revision = s->revision + 1;
        *s = scratch;
    }
    return r;
}

/*
 * Assigns a fresh birth generation even when a previously occupied lane slot
 * is reused.
 */
static int new_lane(Score* s, int x, int y, int n, TileId source)
{
    if (s->generation == UINT32_MAX) return -1;
    for (int i = 0; i < SCORE_LANES; i++)
    {
        if (!valid(s, i))
        {
            Lane* l = &s->lanes[i];
            memset(l, 0, sizeof(*l));
            s->lane_generation[i] = ++s->generation;
            l->active = 1;
            l->x = x;
            l->y = y;
            l->length = n;
            l->division = 16;
            l->channel = source ? -1 : 0;
            l->source = source;
            return i;
        }
    }
    return -1;
}

ScoreResult score_can_create(const Score* s, int x, int y, int n)
{
    scratch = *s;
    if (new_lane(&scratch, x, y, n, 0) < 0) return SCORE_FULL;
    return admission(&scratch);
}

ScoreResult score_create(Score* s, int x, int y, int n)
{
    ScoreResult r = score_can_create(s, x, y, n);
    if (!r)
    {
        scratch.revision = s->revision + 1;
        *s = scratch;
    }
    return r;
}

ScoreResult score_can_resize(const Score* s, int i, int n)
{
    if (!valid(s, i)) return SCORE_INVALID;
    if (n < 1 || n > SCORE_STEPS) return SCORE_BOUNDS;
    for (int j = n; j < s->lanes[i].length; j++)
    {
        if (s->lanes[i].tiles[j]) return SCORE_TILES;
    }
    scratch = *s;
    scratch.lanes[i].length = n;
    return admission(&scratch);
}

ScoreResult score_resize(Score* s, int i, int n)
{
    ScoreResult r = score_can_resize(s, i, n);
    if (!r)
    {
        scratch.revision = s->revision + 1;
        *s = scratch;
    }
    return r;
}

/*
 * Returns the owning link for a stack depth, allowing insertion and removal
 * without a predecessor search.
 */
static TileId* link_at(Score* s, Cell c)
{
    TileId* p = &s->lanes[c.lane].tiles[c.step];
    for (int d = 0; d < c.depth && *p; d++) p = &s->tiles[*p].next;
    return p;
}

static void erase_lane(Score* s, int i);

/*
 * Deletes the branch owned by a jump before recycling its tile ID.
 */
static void erase_tile(Score* s, TileId id)
{
    Tile* t = &s->tiles[id];
    if (t->value.kind == TILE_JUMP) erase_lane(s, t->branch);
    memset(t, 0, sizeof(*t));
}

/*
 * Recursively clears dependent branches so no live jump points into a released
 * lane.
 */
static void erase_lane(Score* s, int i)
{
    Lane* l = &s->lanes[i];
    for (int j = 0; j < l->length; j++)
    {
        TileId next;
        for (TileId t = l->tiles[j]; t; t = next)
        {
            next = s->tiles[t].next;
            erase_tile(s, t);
        }
    }
    memset(l, 0, sizeof(*l));
}

ScoreResult score_delete(Score* s, int i)
{
    if (!valid(s, i)) return SCORE_INVALID;
    TileId source = s->lanes[i].source;
    if (source)
    {
        int p = owner(s, source);
        for (int j = 0; j < s->lanes[p].length; j++)
        {
            for (TileId* t = &s->lanes[p].tiles[j]; *t; t = &s->tiles[*t].next)
            {
                if (*t == source)
                {
                    *t = s->tiles[source].next;
                    erase_tile(s, source);
                    s->revision++;
                    return SCORE_OK;
                }
            }
        }
    }
    erase_lane(s, i);
    s->revision++;
    return SCORE_OK;
}

ScoreResult score_remove(Score* s, int x, int y)
{
    Cell c = score_at(s, x, y);
    if (c.kind != CELL_TILE) return SCORE_INVALID;
    TileId* p = link_at(s, c);
    *p = s->tiles[c.tile].next;
    erase_tile(s, c.tile);
    s->revision++;
    return SCORE_OK;
}

/*
 * Allocates a tile in staged state and searches for an unobstructed branch
 * location when the tile is a jump.
 */
static ScoreResult insert(Score* s, Cell c, TileValue v)
{
    if (!value_valid(v) || (c.kind != CELL_STEP && c.kind != CELL_END))
    {
        return SCORE_INVALID;
    }
    Lane* l = &s->lanes[c.lane];
    if (c.kind == CELL_END)
    {
        if (l->length == SCORE_STEPS) return SCORE_BOUNDS;
        l->length++;
    }
    TileId id = 1;
    while (id <= SCORE_TILE_CAPACITY && s->tiles[id].value.kind) id++;
    if (id > SCORE_TILE_CAPACITY || s->generation == UINT32_MAX)
    {
        return SCORE_FULL;
    }
    s->tile_generation[id] = ++s->generation;
    TileId* p = link_at(s, c);
    s->tiles[id] = (Tile){v, *p, -1};
    *p = id;
    if (v.kind == TILE_JUMP)
    {
        // Keep one clear row below every existing stack so the branch head
        // cannot look like another tile in an unrelated chord.
        int bottom = 0;
        for (int i = 0; i < SCORE_LANES; i++)
        {
            if (valid(s, i))
            {
                if (s->lanes[i].y > bottom) bottom = s->lanes[i].y;
                for (int j = 0; j < s->lanes[i].length; j++)
                {
                    int y = s->lanes[i].y;
                    for (TileId t = s->lanes[i].tiles[j]; t;
                         t = s->tiles[t].next, y++)
                    {
                        if (y > bottom) bottom = y;
                    }
                }
            }
        }
        int b = new_lane(s, 0, bottom + 2, 4, id);
        if (b < 0) return SCORE_FULL;
        s->tiles[id].branch = b;
        for (int y = bottom + 2; y < SCORE_HEIGHT; y++)
        {
            for (int x = 0; x < SCORE_WIDTH - 5; x++)
            {
                s->lanes[b].x = x;
                s->lanes[b].y = y;
                if (validate(s) == SCORE_OK) return SCORE_OK;
            }
        }
        return SCORE_BOUNDS;
    }
    return SCORE_OK;
}

ScoreResult score_place_value(Score* s, int x, int y, TileValue v)
{
    scratch = *s;
    ScoreResult r = insert(&scratch, score_resolve(s, x, y), v);
    return r ? r : commit(s);
}

ScoreResult score_place(Score* s, int x, int y, TileKind k)
{
    return score_place_value(s, x, y, score_default(k));
}

void score_copy(const Score* s, int x, int y, Clipboard* b)
{
    Cell c = score_at(s, x, y);
    if (c.kind != CELL_TILE) return;
    Clipboard copy = {0};
    for (TileId t = c.tile; t; t = s->tiles[t].next)
    {
        if (s->tiles[t].value.kind != TILE_JUMP)
        {
            copy.values[copy.count++] = s->tiles[t].value;
        }
    }
    if (copy.count) *b = copy;
}

ScoreResult score_paste(Score* s, int x, int y, const Clipboard* b)
{
    if (b->count < 1 || b->count > SCORE_HEIGHT) return SCORE_INVALID;
    scratch = *s;
    for (int i = 0; i < b->count; i++)
    {
        if (b->values[i].kind == TILE_JUMP) return SCORE_INVALID;
        ScoreResult r =
            insert(&scratch, score_resolve(&scratch, x, y + i), b->values[i]);
        if (r) return r;
    }
    return commit(s);
}

/*
 * Stages lane or stack movement and validates the final geometry before
 * returning a preview result.
 */
static ScoreResult move(const Score* s, int sx, int sy, int x, int y)
{
    Cell a = score_at(s, sx, sy);
    Cell b = score_resolve(s, x, y);
    scratch = *s;
    if (a.kind != CELL_HEAD && a.kind != CELL_TILE) return SCORE_INVALID;
    if (sx == x && sy == y) return SCORE_OK;
    if (a.kind == CELL_HEAD)
    {
        scratch.lanes[a.lane].x = x;
        scratch.lanes[a.lane].y = y;
        return admission(&scratch);
    }
    if (b.kind != CELL_TILE && b.kind != CELL_STEP && b.kind != CELL_END)
    {
        return SCORE_INVALID;
    }
    TileId* from = link_at(&scratch, a);
    TileId tail = a.tile;
    int same = a.lane == b.lane && a.step == b.step;
    // Within a stack the target depth is the final index after extraction.
    // Across steps the complete suffix is detached and inserted as one chain.
    if (same)
    {
        *from = scratch.tiles[tail].next;
    }
    else
    {
        *from = 0;
        while (scratch.tiles[tail].next) tail = scratch.tiles[tail].next;
    }
    if (b.kind == CELL_END)
    {
        if (scratch.lanes[b.lane].length == SCORE_STEPS) return SCORE_BOUNDS;
        scratch.lanes[b.lane].length++;
    }
    TileId* to = link_at(&scratch, b);
    scratch.tiles[tail].next = *to;
    *to = a.tile;
    return admission(&scratch);
}

MovePlan score_plan_move(const Score* s, int sx, int sy, int x, int y)
{
    return (MovePlan){sx, sy, x, y, move(s, sx, sy, x, y)};
}

ScoreResult score_apply_move(Score* s, MovePlan p)
{
    if (p.sx == p.x && p.sy == p.y) return move(s, p.sx, p.sy, p.x, p.y);
    ScoreResult r = move(s, p.sx, p.sy, p.x, p.y);
    if (!r)
    {
        scratch.revision = s->revision + 1;
        *s = scratch;
    }
    return r;
}

const char* score_tile_label(TileKind k)
{
    static const char* v[] =
    {
        "EMPTY",      "NOTE",
        "CYCLE GATE", "PROBABILITY GATE",
        "JUMP",       "RELATIVE LOCK"
    };
    return k >= 0 && k < TILE_KIND_COUNT ? v[k] : "?";
}

const char* score_message(ScoreResult r)
{
    static const char* v[] =
    {
        "",
        "LIMIT / EDGE",
        "COLLISION",
        "SCORE FULL",
        "REMOVE TRAILING TILES FIRST",
        "INVALID CELL",
        "BRANCH CYCLE"
    };
    return v[r];
}

const char* score_wave_name(int wave)
{
    static const char* names[] =
    {
        "SINE", "TRIANGLE", "SAW", "SQUARE", "NOISE"
    };
    return wave >= 0 && wave < WAVE_COUNT ? names[wave] : "?";
}

const char* score_reverb_size(int size)
{
    static const char* names[] =
    {
        "SMALL", "MEDIUM", "LARGE"
    };
    return size >= 0 && size < 3 ? names[size] : "?";
}

ScoreResult score_set_bpm(Score* s, int bpm)
{
    if (bpm < SCORE_MIN_BPM || bpm > SCORE_MAX_BPM) return SCORE_INVALID;
    s->bpm = bpm;
    s->revision++;
    return SCORE_OK;
}

ScoreResult score_set_reverb(Score* s, ReverbSettings reverb)
{
    if (reverb.size < 0 || reverb.size > 2 || reverb.amount < 0 ||
        reverb.amount > 100)
    {
        return SCORE_INVALID;
    }
    s->reverb = reverb;
    s->revision++;
    return SCORE_OK;
}

ScoreResult score_set_sound(Score* s, int channel, SoundSettings sound)
{
    if (channel < 0 || channel >= SCORE_CHANNELS) return SCORE_INVALID;
    if (sound.reverb < 0 || sound.reverb > 1) return SCORE_INVALID;
    if (sound.attack < 0 || sound.attack > SOUND_MAX_MS || sound.release < 0 ||
        sound.release > SOUND_MAX_MS)
    {
        return SCORE_INVALID;
    }
    if (sound.wave_a < 0 || sound.wave_a >= WAVE_COUNT || sound.wave_b < 0 ||
        sound.wave_b >= WAVE_COUNT || sound.mix_attack < 0 ||
        sound.mix_attack > SOUND_MAX_MIX_MS || sound.mix_release < 0 ||
        sound.mix_release > SOUND_MAX_MIX_MS ||
        sound.sweep < -SOUND_MAX_SWEEP || sound.sweep > SOUND_MAX_SWEEP ||
        sound.decay < 0 || sound.decay > SOUND_MAX_DECAY_MS)
    {
        return SCORE_INVALID;
    }
    s->sounds[channel] = sound;
    s->revision++;
    return SCORE_OK;
}
