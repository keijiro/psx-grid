#include "score.h"
#include <string.h>
void score_init(Score *s) { memset(s, 0, sizeof(*s)); }
static int valid(const Score *s, int i) { return i >= 0 && i < SCORE_LANES && s->lanes[i].active; }
Cell score_at(const Score *s, int x, int y) {
    Cell c = {CELL_EMPTY, -1, -1};
    for (int i = 0; i < SCORE_LANES; i++) {
        const Lane *l = &s->lanes[i];
        if (!l->active || y != l->y || x < l->x || x > l->x + l->length + 1) continue;
        c.lane = i;
        c.step = x - l->x - 1;
        c.kind = x == l->x ? CELL_HEAD : c.step == l->length ? CELL_END :
            l->tiles[c.step] ? CELL_TILE : CELL_STEP;
        break;
    }
    return c;
}
static ScoreResult room(const Score *s, int x, int y, int n, int skip) {
    if (n < 1 || n > SCORE_STEPS || x < 0 || x >= SCORE_WIDTH ||
        y < 0 || y >= SCORE_HEIGHT || n + 1 >= SCORE_WIDTH - x) return SCORE_BOUNDS;
    for (int i = 0; i < SCORE_LANES; i++) {
        const Lane *l = &s->lanes[i];
        if (i != skip && l->active && l->y == y &&
            x <= l->x + l->length + 1 && x + n + 1 >= l->x) return SCORE_COLLISION;
    }
    return SCORE_OK;
}
ScoreResult score_can_create(const Score *s, int x, int y, int n) {
    ScoreResult r = room(s, x, y, n, -1);
    if (r != SCORE_OK) return r;
    for (int i = 0; i < SCORE_LANES; i++) if (!s->lanes[i].active) return SCORE_OK;
    return SCORE_FULL;
}
ScoreResult score_create(Score *s, int x, int y, int n) {
    ScoreResult r = score_can_create(s, x, y, n);
    if (r != SCORE_OK) return r;
    for (int i = 0; i < SCORE_LANES; i++) if (!s->lanes[i].active) {
        Lane *l = &s->lanes[i];
        memset(l, 0, sizeof(*l));
        l->active = 1; l->x = x; l->y = y; l->length = n;
        break;
    }
    return SCORE_OK;
}
ScoreResult score_can_resize(const Score *s, int i, int n) {
    if (!valid(s, i)) return SCORE_INVALID;
    const Lane *l = &s->lanes[i];
    ScoreResult r = room(s, l->x, l->y, n, i);
    if (r != SCORE_OK) return r;
    for (int j = n; j < l->length; j++) if (l->tiles[j]) return SCORE_TILES;
    return SCORE_OK;
}
ScoreResult score_resize(Score *s, int i, int n) {
    ScoreResult r = score_can_resize(s, i, n);
    if (r == SCORE_OK) s->lanes[i].length = n;
    return r;
}
ScoreResult score_delete(Score *s, int i) {
    if (!valid(s, i)) return SCORE_INVALID;
    memset(&s->lanes[i], 0, sizeof(Lane));
    return SCORE_OK;
}
ScoreResult score_place(Score *s, int x, int y, TileKind kind) {
    Cell c = score_at(s, x, y);
    if (kind <= TILE_NONE || kind >= TILE_KIND_COUNT || c.kind != CELL_STEP) return SCORE_INVALID;
    s->lanes[c.lane].tiles[c.step] = kind;
    return SCORE_OK;
}
ScoreResult score_remove(Score *s, int x, int y) {
    Cell c = score_at(s, x, y);
    if (c.kind != CELL_TILE) return SCORE_INVALID;
    s->lanes[c.lane].tiles[c.step] = TILE_NONE;
    return SCORE_OK;
}
const char *score_tile_label(TileKind kind) {
    static const char *labels[] = {"EMPTY", "NOTE", "ABSOLUTE PARAMETER", "RELATIVE PARAMETER",
        "CYCLE GATE", "PROBABILITY GATE", "JUMP"};
    return kind >= TILE_NONE && kind < TILE_KIND_COUNT ? labels[kind] : "?";
}
const char *score_message(ScoreResult r) {
    static const char *messages[] = {"", "LIMIT / EDGE: MOVE OR ADJUST", "LANE COLLISION: MOVE OR ADJUST",
        "ALL 16 LANES USED", "REMOVE TRAILING TILES FIRST", "INVALID CELL"};
    return messages[r];
}
