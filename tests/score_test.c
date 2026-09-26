/*
 * score_test.c - Score model and editor regression tests
 *
 * Implementation notes:
 *
 * State snapshots verify failed edits preserve the model while geometry,
 * branches and commands are exercised.
 */

#include "editor.h"

#include <assert.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

static Score s;
static Score before;
static Editor e;

static void snapshot(void)
{
    before = s;
}

static void unchanged(ScoreResult r)
{
    assert(r != SCORE_OK);
    assert(!memcmp(&s, &before, sizeof(s)));
}

static TileId id(int x, int y)
{
    return score_at(&s, x, y).tile;
}

static void base(void)
{
    score_init(&s);
    assert(!score_create(&s, 0, 0, 4));
}

/*
 * Checks geometry, branch ancestry and failure atomicity through the public
 * score API.
 */
static void model(void)
{
    base();
    assert(!score_place(&s, 1, 0, TILE_CYCLE));
    assert(!score_place(&s, 1, 1, TILE_NOTE));
    assert(!score_place(&s, 1, 2, TILE_NOTE));
    TileId gate = id(1, 0);
    TileId a = id(1, 1);
    TileId b = id(1, 2);
    assert(score_at(&s, 1, 2).depth == 2 && a != b);
    TileValue v = s.tiles[a].value;
    assert(v.pitch == 48 && v.length == 20);
    v.pitch = 108;
    v.length = 1280;
    assert(!score_edit(&s, a, v));
    snapshot();
    v.pitch = 109;
    unchanged(score_edit(&s, a, v));
    v.pitch = 0;
    v.length = 4;
    unchanged(score_edit(&s, a, v));
    v.length = 5;
    assert(!score_edit(&s, a, v));
    v = s.tiles[gate].value;
    v.pattern = 0x80000001u;
    v.period = 32;
    assert(!score_edit(&s, gate, v));
    v.period = 2;
    assert(!score_edit(&s, gate, v));
    assert(s.tiles[gate].value.pattern == 0x80000001u);
    snapshot();
    v.period = 1;
    unchanged(score_edit(&s, gate, v));
    v.period = 33;
    unchanged(score_edit(&s, gate, v));
    assert(!score_place(&s, 2, 0, TILE_PROBABILITY));
    TileId prob = id(2, 0);
    v = s.tiles[prob].value;
    v.chance = 0;
    assert(!score_edit(&s, prob, v));
    v.chance = 100;
    assert(!score_edit(&s, prob, v));
    snapshot();
    v.chance = 101;
    unchanged(score_edit(&s, prob, v));
    v.chance = -1;
    unchanged(score_edit(&s, prob, v));
    snapshot();
    unchanged(score_place(&s, 1, 0, TILE_NOTE));
    unchanged(score_resize(&s, 0, 1));
    MovePlan plan = score_plan_move(&s, 1, 1, 1, 2);
    assert(!memcmp(&s, &before, sizeof(s)));
    assert(!score_apply_move(&s, plan));
    assert(id(1, 1) == b && id(1, 2) == a);
    assert(!score_apply_move(&s, score_plan_move(&s, 1, 2, 1, 0)));
    assert(id(1, 0) == a && id(1, 1) == gate);
    assert(!score_remove(&s, 1, 1));
    assert(id(1, 1) == b && !s.tiles[gate].value.kind);
    assert(!score_create(&s, 10, 0, 4));
    assert(!score_apply_move(&s, score_plan_move(&s, 1, 0, 11, 0)));
    assert(!id(1, 0) && id(11, 0) == a && id(11, 1) == b);
    assert(!score_place(&s, 1, 0, TILE_NOTE));
    TileId top = id(1, 0);
    assert(!score_apply_move(&s, score_plan_move(&s, 11, 0, 1, 0)));
    assert(id(1, 0) == a && id(1, 1) == b && id(1, 2) == top);
    Clipboard clip = {0};
    score_copy(&s, 1, 0, &clip);
    assert(clip.count == 3);
    assert(!score_paste(&s, 3, 0, &clip));
    assert(id(3, 0) != a);
    v = s.tiles[a].value;
    v.pitch = 70;
    assert(!score_edit(&s, a, v));
    assert(s.tiles[id(3, 0)].value.pitch == 0);
    assert(!score_place(&s, 5, 0, TILE_NOTE));
    assert(s.lanes[0].length == 5);
    snapshot();
    unchanged(score_apply_move(&s, score_plan_move(&s, 0, 0, 10, 0)));
    unchanged(score_apply_move(&s, score_plan_move(&s, 0, 0, 127, 63)));
    assert(!score_apply_move(&s, score_plan_move(&s, 0, 0, 0, 0)));
    assert(!memcmp(&s, &before, sizeof(s)));
    assert(!score_apply_move(&s, score_plan_move(&s, 0, 0, 0, 10)));
    assert(id(1, 10) == a);
    snapshot();
    unchanged(score_create(&s, 1, 11, 4));
    assert(!score_set_division(&s, 0, 3));
    snapshot();
    unchanged(score_set_division(&s, 0, 5));
    base();
    assert(!score_place(&s, 1, 0, TILE_JUMP));
    TileId jump = id(1, 0);
    int branch = s.tiles[jump].branch;
    assert(s.lanes[branch].length == 4 && s.lanes[branch].source == jump);
    int bx = s.lanes[branch].x;
    int by = s.lanes[branch].y;
    assert(!score_set_division(&s, 0, 24));
    assert(score_division(&s, branch) == 24);
    snapshot();
    unchanged(score_set_division(&s, branch, 8));
    assert(!score_place(&s, bx + 1, by, TILE_JUMP));
    int child = s.tiles[id(bx + 1, by)].branch;
    snapshot();
    unchanged(score_apply_move(&s, score_plan_move(&s, 1, 0, bx + 2, by)));
    unchanged(score_apply_move(
        &s, score_plan_move(&s, 1, 0, s.lanes[child].x + 1, s.lanes[child].y)));
    Clipboard old = clip;
    score_copy(&s, 1, 0, &clip);
    assert(!memcmp(&clip, &old, sizeof(clip)));
    assert(!score_place(&s, 1, 1, TILE_NOTE));
    score_copy(&s, 1, 0, &clip);
    assert(clip.count == 1 && clip.values[0].kind == TILE_NOTE);
    assert(!score_apply_move(&s, score_plan_move(&s, bx, by, 20, 10)));
    assert(s.lanes[child].x == 0 && s.lanes[0].x == 0);
    assert(!score_delete(&s, branch));
    assert(!s.lanes[branch].active && !s.lanes[child].active &&
           s.tiles[id(1, 0)].value.kind == TILE_NOTE);
    assert(!score_place(&s, 2, 0, TILE_JUMP));
    branch = s.tiles[id(2, 0)].branch;
    assert(!score_remove(&s, 2, 0));
    assert(!s.lanes[branch].active);
    base();
    for (int i = 1; i < 16; i++) assert(!score_create(&s, 0, i * 2, 4));
    snapshot();
    unchanged(score_place(&s, 1, 0, TILE_JUMP));
    unchanged(score_create(&s, 20, 0, 4));
    score_init(&s);
    assert(!score_create(&s, 0, 63, 4));
    snapshot();
    unchanged(score_place(&s, 1, 63, TILE_JUMP));
    assert(!score_place(&s, 1, 63, TILE_NOTE));
    snapshot();
    unchanged(score_place(&s, 1, 64, TILE_NOTE));
    // Normal edits now fill the serialized budget before the runtime pool. Two
    // lanes plus 1474 Notes use 8190 bytes. Replacing one with a Cycle reaches
    // the exact boundary and still permits same-size edits/deletion.
    score_init(&s);
    assert(!score_create(&s, 0, 0, 64));
    assert(!score_create(&s, 70, 0, 4));
    for (int n = 0; n < 1474; n++)
    {
        assert(!score_place(&s, n / 64 + 1, n % 64, TILE_NOTE));
    }
    assert(score_format_measure(&s) == 8190);
    assert(!score_remove(&s, 24, 1));
    assert(!score_place(&s, 24, 1, TILE_CYCLE));
    assert(score_format_measure(&s) == SCORE_FILE_BYTES);
    TileId boundary = id(24, 1);
    v = s.tiles[boundary].value;
    v.pattern = 0;
    assert(!score_edit(&s, boundary, v));
    assert(score_format_measure(&s) == SCORE_FILE_BYTES);
    snapshot();
    unchanged(score_resize(&s, 1, 5));
    snapshot();
    unchanged(score_apply_move(&s, score_plan_move(&s, 24, 0, 75, 0)));
    assert(s.lanes[1].length == 4 && id(24, 0));
    Score available = s;
    assert(!score_remove(&available, 24, 1));
    assert(score_format_measure(&available) == 8185);
    MovePlan grow = score_plan_move(&available, 24, 0, 75, 0);
    assert(grow.result == SCORE_OK);
    assert(!score_apply_move(&available, grow));
    assert(available.lanes[1].length == 5 && score_at(&available, 75, 0).tile);
    assert(score_format_measure(&available) == 8186);
    snapshot();
    unchanged(score_place(&s, 71, 0, TILE_NOTE));
    unchanged(score_paste(&s, 71, 0, &clip));
    assert(!score_remove(&s, 24, 1));
    assert(score_format_measure(&s) == 8185);
    Clipboard pair = {0};
    score_copy(&s, 1, 62, &pair);
    assert(pair.count == 2);
    snapshot();
    unchanged(score_paste(&s, 71, 0, &pair));
    assert(!score_place(&s, 71, 0, TILE_NOTE));
    assert(score_format_measure(&s) == 8190);
    base();
    size_t before_jump = score_format_measure(&s);
    assert(!score_place(&s, 1, 0, TILE_JUMP));
    assert(score_format_measure(&s) == before_jump + 19);
    Score without_jump = s;
    assert(!score_remove(&without_jump, 1, 0));
    assert(score_format_measure(&without_jump) == before_jump);
    branch = s.tiles[id(1, 0)].branch;
    assert(
        !score_place(&s, s.lanes[branch].x + 1, s.lanes[branch].y, TILE_JUMP));
    assert(!score_delete(&s, 0));
    assert(score_format_measure(&s) == 728);
    for (int i = 0; i < SCORE_LANES; i++) assert(!s.lanes[i].active);
    for (int i = 1; i <= SCORE_TILE_CAPACITY; i++)
    {
        assert(!s.tiles[i].value.kind);
    }
    for (int i = 0; i < 1000; i++)
    {
        assert(!score_create(&s, 0, 0, 4));
        assert(!score_place(&s, 1, 0, TILE_NOTE));
        assert(s.tiles[id(1, 0)].value.pitch == 48);
        assert(!score_delete(&s, 0));
    }
    base();
    assert(!score_place(&s, 1, 0, TILE_NOTE));
    assert(!score_place(&s, 1, 1, TILE_NOTE));
    score_copy(&s, 1, 0, &clip);
    assert(!score_create(&s, 10, 63, 4));
    snapshot();
    unchanged(score_paste(&s, 11, 63, &clip));
    assert(!score_create(&s, 6, 0, 4));
    snapshot();
    unchanged(score_place(&s, 5, 0, TILE_NOTE));
}

/*
 * Checks that recycled lane and tile IDs receive new births for live playback
 * reconciliation.
 */
static void generations(void)
{
    base();
    uint32_t lane = s.lane_generation[0];
    assert(lane);
    assert(!score_place(&s, 1, 0, TILE_NOTE));
    TileId tile = id(1, 0);
    uint32_t birth = s.tile_generation[tile];
    assert(birth > lane);
    TileValue value = s.tiles[tile].value;
    value.pitch = 60;
    assert(!score_edit(&s, tile, value));
    assert(!score_apply_move(&s, score_plan_move(&s, 1, 0, 2, 0)));
    assert(!score_apply_move(&s, score_plan_move(&s, 0, 0, 0, 4)));
    assert(s.tile_generation[tile] == birth && s.lane_generation[0] == lane);
    assert(!score_remove(&s, 2, 4));
    assert(!score_place(&s, 2, 4, TILE_NOTE));
    assert(id(2, 4) == tile && s.tile_generation[tile] > birth);
    birth = s.tile_generation[tile];
    assert(!score_delete(&s, 0));
    assert(!score_create(&s, 0, 0, 4));
    assert(s.lane_generation[0] > birth);
    assert(!score_place(&s, 1, 0, TILE_NOTE));
    assert(id(1, 0) == tile && s.tile_generation[tile] > birth);

    // Failed compound edits must roll back births as well as geometry. A jump
    // needs two births, so exhausting the second must not consume one.
    base();
    s.generation = UINT32_MAX - 1;
    snapshot();
    unchanged(score_place(&s, 1, 0, TILE_JUMP));
    assert(!score_place(&s, 1, 0, TILE_NOTE));
    assert(s.tile_generation[id(1, 0)] == UINT32_MAX);
    snapshot();
    unchanged(score_place(&s, 2, 0, TILE_NOTE));
    unchanged(score_create(&s, 10, 0, 4));
    value = s.tiles[id(1, 0)].value;
    value.pitch = 72;
    assert(!score_edit(&s, id(1, 0), value));
    assert(!score_apply_move(&s, score_plan_move(&s, 1, 0, 2, 0)));
    assert(!score_remove(&s, 2, 0));
    snapshot();
    unchanged(score_place(&s, 2, 0, TILE_NOTE));
}

static Input input;

static void frame(int connected, int held)
{
    editor_update(&e, input_update(&input, connected, (uint16_t)held));
}

static void tap(int key)
{
    frame(1, key);
    frame(1, 0);
}

static void context(void)
{
    tap(INPUT_CROSS);
    assert(e.mode == EDIT_MENU);
}

static void select_action(EditorAction action)
{
    EditorRow rows[EDITOR_ROWS];
    int n = editor_rows(&e, rows);
    int target = -1;
    for (int i = 0; i < n; i++)
    {
        if (rows[i].id == (int)action) target = i;
    }
    assert(target >= 0);
    while (e.selected < target) tap(INPUT_DOWN);
    while (e.selected > target) tap(INPUT_UP);
    assert(e.selected == target);
}

static void action(EditorAction a)
{
    context();
    select_action(a);
    tap(INPUT_CROSS);
}

static void editor_setup(void)
{
    editor_init(&e);
    input_init(&input);
    frame(1, 0);
}

/*
 * Exercises grid gestures and context actions through normalized controller
 * frames.
 */
static void controls(void)
{
    editor_setup();
    frame(1, INPUT_CROSS);
    assert(e.mode == EDIT_PLANE);
    frame(1, 0);
    assert(e.mode == EDIT_MENU);
    tap(INPUT_CROSS);
    assert(e.score.lanes[0].active && e.mode == EDIT_PLANE);
    tap(INPUT_RIGHT);
    action(ACTION_PLACE);
    assert(e.mode == EDIT_PICKER);
    tap(INPUT_CROSS);
    assert(e.mode == EDIT_PLANE);
    TileId note = score_at(&e.score, e.x, e.y).tile;
    assert(note);
    context();
    select_action(ACTION_PITCH);
    uint32_t revision = e.score.revision;
    tap(INPUT_RIGHT);
    assert(e.score.tiles[note].value.pitch == 49 &&
           e.score.revision == revision + 1);
    tap(INPUT_R1);
    assert(e.score.tiles[note].value.pitch == 61 && e.last_note.pitch == 61);
    tap(INPUT_CROSS);
    assert(e.mode == EDIT_MENU && e.score.tiles[note].value.pitch == 61);
    tap(INPUT_CIRCLE);
    assert(e.mode == EDIT_PLANE);
    context();
    select_action(ACTION_DURATION);
    tap(INPUT_RIGHT);
    tap(INPUT_R1);
    assert(e.score.tiles[note].value.length == 41 && e.last_note.length == 41);
    tap(INPUT_CIRCLE);
    assert(e.mode == EDIT_PLANE);
    tap(INPUT_DOWN);
    action(ACTION_PLACE);
    tap(INPUT_CROSS);
    assert(e.score.tiles[score_at(&e.score, e.x, e.y).tile].value.pitch == 61);
    e.y = 1;
    before = e.score;
    frame(1, INPUT_CROSS | INPUT_RIGHT);
    assert(e.mode == EDIT_MOVE && e.source_x == 2 && e.x == 3);
    assert(!memcmp(&before, &e.score, sizeof(before)));
    frame(1, 0);
    assert(e.mode == EDIT_PLANE && score_at(&e.score, 3, 1).tile == note);
    e.x = 3;
    action(ACTION_COPY);
    assert(e.clipboard.count == 2);
    e.x = 4;
    action(ACTION_PASTE);
    assert(score_at(&e.score, 4, 1).tile != note);
    e.x = 1;
    action(ACTION_DELETE);
    assert(e.mode == EDIT_DELETE);
    tap(INPUT_CIRCLE);
    assert(e.mode == EDIT_MENU && e.score.lanes[0].active);
    assert(e.selected == 4);
    tap(INPUT_CIRCLE);
    assert(e.mode == EDIT_PLANE);
    e.x = 6;
    e.y = 1;
    action(ACTION_PLACE);
    tap(INPUT_DOWN);
    tap(INPUT_CROSS);
    TileId gate = score_at(&e.score, e.x, e.y).tile;
    assert(e.score.tiles[gate].value.kind == TILE_CYCLE);
    TileValue gate_value = e.score.tiles[gate].value;
    gate_value.period = 17;
    gate_value.pattern = 0x80000001u;
    assert(!score_edit(&e.score, gate, gate_value));
    action(ACTION_PATTERN);
    uint32_t old = e.score.tiles[gate].value.pattern;
    revision = e.score.revision;
    tap(INPUT_CROSS);
    assert(e.score.tiles[gate].value.pattern == (old ^ 1u));
    assert(e.score.revision == revision + 1);
    tap(INPUT_RIGHT);
    assert(e.pattern_cursor == 1);
    tap(INPUT_DOWN);
    assert(e.pattern_cursor == 9);
    tap(INPUT_DOWN);
    assert(e.pattern_cursor == 16);
    tap(INPUT_RIGHT);
    assert(e.pattern_cursor == 16);
    tap(INPUT_UP);
    assert(e.pattern_cursor == 8);
    tap(INPUT_LEFT);
    assert(e.pattern_cursor == 7);
    tap(INPUT_CIRCLE);
    assert(e.mode == EDIT_MENU && e.selected == 1);
    tap(INPUT_CIRCLE);
    e.x = 1;
    action(ACTION_DELETE);
    tap(INPUT_CROSS);
    assert(e.mode == EDIT_PLANE && !e.score.lanes[0].active);
}

/*
 * Checks that a failed inline length change preserves both score and editor
 * selection.
 */
static void rejected_inline_resize(void)
{
    editor_setup();
    assert(!score_create(&e.score, 0, 0, 64));
    assert(!score_create(&e.score, 70, 0, 4));
    for (int n = 0; n < 1474; n++)
    {
        assert(!score_place(&e.score, n / 64 + 1, n % 64, TILE_NOTE));
    }
    assert(!score_remove(&e.score, 24, 1));
    assert(!score_place(&e.score, 24, 1, TILE_CYCLE));
    assert(score_format_measure(&e.score) == SCORE_FILE_BYTES);
    e.x = 70;
    e.y = 0;
    context();
    assert(e.selected == 0);
    Score unchanged_score = e.score;
    tap(INPUT_RIGHT);
    assert(!memcmp(&e.score, &unchanged_score, sizeof(e.score)));
    assert(!strcmp(e.message, "SCORE FULL"));
    tap(INPUT_CIRCLE);
    assert(e.mode == EDIT_PLANE);
}

/*
 * Checks sound rows and their value bounds through the editor interaction
 * path.
 */
static void sound_controls(void)
{
    static const struct
    {
        size_t offset;
        int step;
        int min;
        int max;
    } cases[] =
    {
        {
            offsetof(SoundSettings, wave_a), 1, 0, WAVE_COUNT - 1
        },
        {
            offsetof(SoundSettings, wave_b), 1, 0, WAVE_COUNT - 1
        },
        {
            offsetof(SoundSettings, attack), 100, 0, SOUND_MAX_MS
        },
        {
            offsetof(SoundSettings, release), 100, 0, SOUND_MAX_MS
        },
        {
            offsetof(SoundSettings, mix_attack), 100, 0, SOUND_MAX_MIX_MS
        },
        {
            offsetof(SoundSettings, mix_release), 100, 0, SOUND_MAX_MIX_MS
        },
        {
            offsetof(SoundSettings, sweep), 12, -SOUND_MAX_SWEEP,
            SOUND_MAX_SWEEP
        },
        {
            offsetof(SoundSettings, decay), 100, 0, SOUND_MAX_DECAY_MS
        },
        {
            offsetof(SoundSettings, reverb), 1, 0, 1
        }
    };
    for (unsigned i = 0; i < sizeof(cases) / sizeof(*cases); i++)
    {
        editor_setup();
        assert(!score_create(&e.score, 1, 1, 4));
        action(ACTION_SOUND);
        assert(e.mode == EDIT_SOUND && e.selected == 1);
        for (unsigned j = 0; j < i; j++) tap(INPUT_DOWN);
        int* value = (int*)((char*)&e.score.sounds[0] + cases[i].offset);
        int start = *value;
        uint32_t revision = e.score.revision;
        tap(INPUT_R1);
        assert(*value == start + cases[i].step &&
               e.score.revision == revision + 1);
        tap(INPUT_CROSS);
        assert(e.mode == EDIT_SOUND);
        tap(INPUT_CIRCLE);
        assert(e.mode == EDIT_MENU && e.selected == 3 &&
               *value == start + cases[i].step);
        s = e.score;
        for (int boundary = 0; boundary < 2; boundary++)
        {
            SoundSettings invalid = s.sounds[0];
            *(int*)((char*)&invalid + cases[i].offset) =
                boundary ? cases[i].max + 1 : cases[i].min - 1;
            snapshot();
            unchanged(score_set_sound(&s, 0, invalid));
        }
    }
}

/*
 * Checks main-menu tempo, reverb and storage selection controls.
 */
static void main_controls(void)
{
    editor_setup();
    tap(INPUT_SELECT);
    assert(e.mode == EDIT_MAIN && e.selected == 0);
    tap(INPUT_RIGHT);
    tap(INPUT_R1);
    assert(e.score.bpm == 131 && e.score.revision == 2);
    tap(INPUT_CROSS);
    assert(e.mode == EDIT_MAIN);
    tap(INPUT_CIRCLE);
    assert(e.mode == EDIT_PLANE && e.score.bpm == 131);
    tap(INPUT_SELECT);
    tap(INPUT_DOWN);
    tap(INPUT_CROSS);
    assert(e.mode == EDIT_REVERB);
    tap(INPUT_RIGHT);
    assert(e.score.reverb.size == 2);
    tap(INPUT_DOWN);
    tap(INPUT_R1);
    assert(e.score.reverb.amount == 40);
    tap(INPUT_CIRCLE);
    assert(e.mode == EDIT_MAIN && e.selected == 1);
    tap(INPUT_SELECT);
    assert(e.mode == EDIT_PLANE);
    assert(!score_create(&e.score, 1, 1, 4));
    action(ACTION_SOUND);
    for (int j = 0; j < 8; j++) tap(INPUT_DOWN);
    tap(INPUT_RIGHT);
    assert(e.score.sounds[0].reverb);
    tap(INPUT_CIRCLE);
    tap(INPUT_CIRCLE);
    frame(1, INPUT_CROSS);
    frame(1, INPUT_CROSS | INPUT_RIGHT);
    assert(e.mode == EDIT_MOVE);
    frame(1, INPUT_CROSS | INPUT_SELECT);
    assert(e.mode == EDIT_MAIN && !e.gesture && e.x == 1);
    frame(1, 0);
    s = e.score;
    snapshot();
    unchanged(score_set_bpm(&s, 29));
    unchanged(score_set_bpm(&s, 301));
    unchanged(score_set_reverb(&s, (ReverbSettings){3, 30}));
}

/*
 * Checks channel inheritance and editor presentation for root and branch
 * lanes.
 */
static void channels(void)
{
    base();
    SoundSettings initial = SOUND_DEFAULT;
    for (int ch = 0; ch < SCORE_CHANNELS; ch++)
    {
        assert(!memcmp(&s.sounds[ch], &initial, sizeof(initial)));
        assert(!score_set_channel(&s, 0, ch));
        assert(score_channel(&s, 0) == ch);
    }
    snapshot();
    unchanged(score_set_channel(&s, -1, 0));
    unchanged(score_set_channel(&s, SCORE_LANES, 0));
    unchanged(score_set_channel(&s, 1, 0));
    unchanged(score_set_channel(&s, 0, -1));
    unchanged(score_set_channel(&s, 0, SCORE_CHANNELS));
    unchanged(score_set_sound(&s, -1, initial));
    unchanged(score_set_sound(&s, SCORE_CHANNELS, initial));
    assert(score_channel(&s, -1) == -1 &&
           score_channel(&s, SCORE_LANES) == -1 && score_channel(&s, 1) == -1);
    assert(!score_place(&s, 1, 0, TILE_JUMP));
    int branch = s.tiles[id(1, 0)].branch;
    Lane b = s.lanes[branch];
    assert(!score_place(&s, b.x + 1, b.y, TILE_JUMP));
    int child = s.tiles[id(b.x + 1, b.y)].branch;
    assert(score_channel(&s, branch) == 7 && score_channel(&s, child) == 7);
    snapshot();
    unchanged(score_set_channel(&s, branch, 1));
    unchanged(score_set_channel(&s, child, 1));
    assert(!score_create(&s, 40, 0, 4));
    int other = score_at(&s, 40, 0).lane;
    assert(!score_set_channel(&s, other, 2));
    assert(!score_apply_move(&s, score_plan_move(&s, 1, 0, 41, 0)));
    assert(score_channel(&s, branch) == 2 && score_channel(&s, child) == 2);
    SoundSettings custom = initial;
    custom.wave_a = WAVE_NOISE;
    custom.reverb = 1;
    assert(!score_set_sound(&s, 2, custom));
    assert(!score_set_channel(&s, 0, 2));
    assert(!memcmp(&s.sounds[score_channel(&s, 0)],
                   &s.sounds[score_channel(&s, other)], sizeof(custom)));
    assert(!score_delete(&s, other));
    assert(!score_delete(&s, 0));
    assert(!memcmp(&s.sounds[2], &custom, sizeof(custom)));
    assert(!score_create(&s, 0, 0, 4));
    assert(score_channel(&s, 0) == 0);
    assert(!score_set_channel(&s, 0, 2));
    assert(!memcmp(&s.sounds[2], &custom, sizeof(custom)));

    editor_setup();
    assert(!score_create(&e.score, 1, 1, 4));
    assert(!score_create(&e.score, 20, 1, 4));
    context();
    select_action(ACTION_CHANNEL);
    tap(INPUT_RIGHT);
    assert(score_channel(&e.score, 0) == 1);
    tap(INPUT_CIRCLE);
    action(ACTION_SOUND);
    assert(e.sound_channel == 1);
    tap(INPUT_RIGHT);
    assert(e.score.sounds[1].wave_a == WAVE_TRIANGLE);
    assert(!memcmp(&e.score.sounds[0], &initial, sizeof(initial)));
    tap(INPUT_CIRCLE);
    tap(INPUT_CIRCLE);
    e.x = 20;
    action(ACTION_SOUND);
    assert(e.sound_channel == 0);
    tap(INPUT_RIGHT);
    assert(e.score.sounds[0].wave_a == WAVE_TRIANGLE);
}

int main(void)
{
    model();
    generations();
    controls();
    rejected_inline_resize();
    sound_controls();
    main_controls();
    channels();
    puts("PASS: model transactions, inline properties, pattern, deletion and "
         "shared sound");
}
