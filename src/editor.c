/*
 * editor.c - Grid editor interaction and menus
 *
 * Implementation notes:
 *
 * This layer maps normalized input to score operations and retains
 * presentation state; score validation remains in the model.
 */

#include "editor.h"

#include <string.h>
#include <stdio.h>

static int clamp(int x, int min, int max)
{
    return x < min ? min : x > max ? max : x;
}

void editor_init(Editor* e)
{
    memset(e, 0, sizeof(*e));
    score_init(&e->score);
    e->x = e->y = 1;
    e->message = "";
    e->storage_slot = 1;
    e->card_free = -1;
    e->free_bytes = SCORE_FILE_BYTES - (int)score_format_measure(&e->score);
    e->last_tile = e->tile_candidate = TILE_NOTE;
    e->last_note = score_default(TILE_NOTE);
}

int editor_menu(const Editor* e, EditorAction items[EDITOR_MENU_ITEMS])
{
    Cell c = score_resolve(&e->score, e->x, e->y);
    int n = 0;
    if (c.kind == CELL_EMPTY)
        items[n++] = ACTION_CREATE;
    if (c.kind == CELL_STEP || c.kind == CELL_END) {
        items[n++] = ACTION_PLACE;
        items[n++] = ACTION_PASTE;
    }
    if (c.kind == CELL_TILE) {
        TileKind k = e->score.tiles[c.tile].value.kind;
        if (k == TILE_NOTE) {
            items[n++] = ACTION_PITCH;
            items[n++] = ACTION_DURATION;
        }
        if (k == TILE_CYCLE) {
            items[n++] = ACTION_PERIOD;
            items[n++] = ACTION_PATTERN;
        }
        if (k == TILE_RELATIVE) {
            items[n++] = ACTION_LOCK_ATTACK_ENABLE;
            items[n++] = ACTION_LOCK_ATTACK;
            items[n++] = ACTION_LOCK_RELEASE_ENABLE;
            items[n++] = ACTION_LOCK_RELEASE;
        }
        if (k == TILE_PROBABILITY)
            items[n++] = ACTION_CHANCE;
        items[n++] = ACTION_COPY;
        items[n++] = ACTION_REMOVE;
    }
    if (c.kind == CELL_HEAD) {
        items[n++] = ACTION_LENGTH;
        if (!e->score.lanes[c.lane].source) {
            items[n++] = ACTION_DIVISION;
            items[n++] = ACTION_CHANNEL;
            items[n++] = ACTION_SOUND;
        }
        items[n++] = ACTION_DELETE;
    }
    return n;
}

const char* editor_action_label(EditorAction a)
{
    static const char* labels[] = {"NEW LANE",
                                   "CREATE TILE",
                                   "DELETE TILE",
                                   "LENGTH",
                                   "DELETE LANE",
                                   "COPY STACK",
                                   "PASTE STACK",
                                   "PITCH",
                                   "NOTE LENGTH",
                                   "PERIOD",
                                   "PATTERN",
                                   "CHANCE",
                                   "DIVISION",
                                   "SOUND",
                                   "CHANNEL",
                                   "ATTACK ENABLE",
                                   "RELEASE ENABLE",
                                   "ATTACK OFFSET",
                                   "RELEASE OFFSET"};
    return labels[a];
}

/*
 * Setting row IDs live above context-action IDs so the shared value editor
 * can dispatch either menu without ambiguous cases.
 */
enum {
    ROW_BPM = 100,
    ROW_REVERB,
    ROW_SLOT,
    ROW_CHECK,
    ROW_SAVE,
    ROW_LOAD,
    ROW_SIZE,
    ROW_AMOUNT,
    ROW_WAVE1 = 200,
    ROW_WAVE2,
    ROW_AMP_ATTACK,
    ROW_AMP_RELEASE,
    ROW_MIX_ATTACK,
    ROW_MIX_RELEASE,
    ROW_SWEEP,
    ROW_DECAY,
    ROW_SEND
};

static EditorRow
row(EditorRowKind kind, int id, const char* label, int min, int max, int fine, int coarse)
{
    return (EditorRow){kind, id, min, max, fine, coarse, label};
}

int editor_rows(const Editor* e, EditorRow rows[EDITOR_ROWS])
{
    int n = 0;
    if (e->mode == EDIT_MAIN) {
        rows[n++] = row(ROW_VALUE, ROW_BPM, "BPM", SCORE_MIN_BPM, SCORE_MAX_BPM, 1, 10);
        rows[n++] = row(ROW_SUBMENU, ROW_REVERB, "REVERB", 0, 0, 0, 0);
        rows[n++] = row(ROW_VALUE, ROW_SLOT, "SLOT", 1, STORAGE_SLOTS, 1, 1);
        rows[n++] = row(ROW_ACTION, ROW_CHECK, "CHECK CARD", 0, 0, 0, 0);
        rows[n++] = row(ROW_ACTION, ROW_SAVE, "SAVE", 0, 0, 0, 0);
        rows[n++] = row(ROW_ACTION, ROW_LOAD, "LOAD", 0, 0, 0, 0);
    } else if (e->mode == EDIT_REVERB) {
        rows[n++] = row(ROW_VALUE, ROW_SIZE, "SIZE", 0, 2, 1, 1);
        rows[n++] = row(ROW_VALUE, ROW_AMOUNT, "AMOUNT", 0, 100, 1, 10);
    } else if (e->mode == EDIT_SOUND) {
        rows[n++] = row(ROW_HEADING, 0, "WAVEFORM", 0, 0, 0, 0);
        rows[n++] = row(ROW_VALUE, ROW_WAVE1, "WAVE 1", 0, WAVE_COUNT - 1, 1, 1);
        rows[n++] = row(ROW_VALUE, ROW_WAVE2, "WAVE 2", 0, WAVE_COUNT - 1, 1, 1);
        rows[n++] = row(ROW_HEADING, 0, "AMP", 0, 0, 0, 0);
        rows[n++] = row(ROW_VALUE, ROW_AMP_ATTACK, "ATTACK", 0, SOUND_MAX_MS, 1, 100);
        rows[n++] = row(ROW_VALUE, ROW_AMP_RELEASE, "RELEASE", 0, SOUND_MAX_MS, 1, 100);
        rows[n++] = row(ROW_HEADING, 0, "MIX", 0, 0, 0, 0);
        rows[n++] = row(ROW_VALUE, ROW_MIX_ATTACK, "ATTACK", 0, SOUND_MAX_MIX_MS, 1, 100);
        rows[n++] = row(ROW_VALUE, ROW_MIX_RELEASE, "RELEASE", 0, SOUND_MAX_MIX_MS, 1, 100);
        rows[n++] = row(ROW_HEADING, 0, "PITCH", 0, 0, 0, 0);
        rows[n++] = row(ROW_VALUE, ROW_SWEEP, "SWEEP", -SOUND_MAX_SWEEP, SOUND_MAX_SWEEP, 1, 12);
        rows[n++] = row(ROW_VALUE, ROW_DECAY, "DECAY", 0, SOUND_MAX_DECAY_MS, 1, 100);
        rows[n++] = row(ROW_VALUE, ROW_SEND, "REVERB", 0, 1, 1, 1);
    } else if (e->mode == EDIT_MENU) {
        EditorAction actions[EDITOR_MENU_ITEMS];
        int count = editor_menu(e, actions);
        for (int i = 0; i < count; i++) {
            EditorAction a = actions[i];
            EditorRowKind kind = ROW_ACTION;
            int min = 0, max = 0, fine = 0, coarse = 0;
            if (a == ACTION_SOUND || a == ACTION_PLACE || a == ACTION_PATTERN)
                kind = ROW_SUBMENU;
            else if (a == ACTION_LENGTH || a == ACTION_DIVISION || a == ACTION_CHANNEL ||
                     a == ACTION_PITCH || a == ACTION_DURATION || a == ACTION_PERIOD ||
                     a == ACTION_CHANCE || a == ACTION_LOCK_ATTACK_ENABLE ||
                     a == ACTION_LOCK_RELEASE_ENABLE || a == ACTION_LOCK_ATTACK ||
                     a == ACTION_LOCK_RELEASE)
                kind = ROW_VALUE;
            switch (a) {
            case ACTION_LENGTH:
                min = 1;
                max = 64;
                fine = coarse = 1;
                break;
            case ACTION_DIVISION:
                min = 0;
                max = SCORE_DIVISIONS - 1;
                fine = coarse = 1;
                break;
            case ACTION_CHANNEL:
                min = 0;
                max = SCORE_CHANNELS - 1;
                fine = coarse = 1;
                break;
            case ACTION_PITCH:
                min = 0;
                max = 108;
                fine = 1;
                coarse = 12;
                break;
            case ACTION_DURATION:
                min = 5;
                max = 1280;
                fine = 1;
                coarse = 20;
                break;
            case ACTION_PERIOD:
                min = 2;
                max = 32;
                fine = coarse = 1;
                break;
            case ACTION_CHANCE:
                min = 0;
                max = 100;
                fine = 1;
                coarse = 10;
                break;
            case ACTION_LOCK_ATTACK_ENABLE:
            case ACTION_LOCK_RELEASE_ENABLE:
                min = 0;
                max = 1;
                fine = coarse = 1;
                break;
            case ACTION_LOCK_ATTACK:
            case ACTION_LOCK_RELEASE:
                min = -SOUND_MAX_MS;
                max = SOUND_MAX_MS;
                fine = 1;
                coarse = 100;
                break;
            default:
                break;
            }
            rows[n++] = row(kind, a, editor_action_label(a), min, max, fine, coarse);
        }
    }
    return n;
}

void editor_row_value(const Editor* e, int id, char* b, int size)
{
    const SoundSettings* s = &e->score.sounds[e->sound_channel];
    TileValue v = e->target ? e->score.tiles[e->target].value : e->value;
    b[0] = 0;
    switch (id) {
    case ROW_BPM:
        snprintf(b, size, "%d", e->score.bpm);
        break;
    case ROW_SLOT:
        snprintf(b, size, "%02d", e->storage_slot);
        break;
    case ROW_SAVE:
        snprintf(b, size, "FREE %d B", e->free_bytes);
        break;
    case ROW_SIZE:
        snprintf(b, size, "%s", score_reverb_size(e->score.reverb.size));
        break;
    case ROW_AMOUNT:
        snprintf(b, size, "%d%%", e->score.reverb.amount);
        break;
    case ROW_WAVE1:
        snprintf(b, size, "%s", score_wave_name(s->wave_a));
        break;
    case ROW_WAVE2:
        snprintf(b, size, "%s", score_wave_name(s->wave_b));
        break;
    case ROW_AMP_ATTACK:
        snprintf(b, size, "%d MS", s->attack);
        break;
    case ROW_AMP_RELEASE:
        snprintf(b, size, "%d MS", s->release);
        break;
    case ROW_MIX_ATTACK:
        snprintf(b, size, "%d MS", s->mix_attack);
        break;
    case ROW_MIX_RELEASE:
        snprintf(b, size, "%d MS", s->mix_release);
        break;
    case ROW_SWEEP:
        snprintf(b, size, "%+d ST", s->sweep);
        break;
    case ROW_DECAY:
        snprintf(b, size, "%d MS", s->decay);
        break;
    case ROW_SEND:
        snprintf(b, size, "%s", s->reverb ? "ON" : "OFF");
        break;
    case ACTION_LENGTH:
        snprintf(b, size, "%d", e->score.lanes[e->lane].length);
        break;
    case ACTION_DIVISION:
        snprintf(b, size, "1/%d", score_division(&e->score, e->lane));
        break;
    case ACTION_CHANNEL:
        snprintf(b, size, "%d", score_channel(&e->score, e->lane) + 1);
        break;
    case ACTION_PITCH:
        snprintf(b, size, "%s%d", score_note_name(v.pitch), v.pitch / 12);
        break;
    case ACTION_DURATION:
        snprintf(b, size, "%d.%02d", v.length / 20, v.length % 20 * 5);
        break;
    case ACTION_PERIOD:
        snprintf(b, size, "%d", v.period);
        break;
    case ACTION_CHANCE:
        snprintf(b, size, "%d%%", v.chance);
        break;
    case ACTION_LOCK_ATTACK_ENABLE:
        snprintf(b, size, "%s", v.lock_mask & LOCK_ATTACK ? "ON" : "OFF");
        break;
    case ACTION_LOCK_RELEASE_ENABLE:
        snprintf(b, size, "%s", v.lock_mask & LOCK_RELEASE ? "ON" : "OFF");
        break;
    case ACTION_LOCK_ATTACK:
        snprintf(b, size, "%+d MS", v.attack);
        break;
    case ACTION_LOCK_RELEASE:
        snprintf(b, size, "%+d MS", v.release);
        break;
    default:
        break;
    }
}

static void cancel_move(Editor* e)
{
    e->x = e->source_x;
    e->y = e->source_y;
    e->mode = EDIT_PLANE;
    e->gesture = 0;
    e->message = "CANCELLED";
}

static void finish(Editor* e, ScoreResult r)
{
    e->message = score_message(r);
    if (!r)
        e->mode = EDIT_PLANE;
}

static void enter_context(Editor* e)
{
    Cell c = score_resolve(&e->score, e->x, e->y);
    e->lane = c.lane;
    e->target = c.tile;
    if (c.tile)
        e->value = e->score.tiles[c.tile].value;
    e->mode = EDIT_MENU;
    e->selected = 0;
    e->message = "";
}

/*
 * Skips headings during navigation without allowing the selection to escape
 * the current menu.
 */
static int selected_row(const EditorRow* rows, int count, int selected, int direction)
{
    int next = selected;
    for (;;) {
        int candidate = clamp(next + direction, 0, count - 1);
        if (candidate == next)
            return selected;
        next = candidate;
        if (rows[next].kind != ROW_HEADING)
            return next;
    }
}

/*
 * Routes a bounded row adjustment through score validators; a busy load
 * permits slot selection but blocks score edits.
 */
static void adjust(Editor* e, EditorRow r, int direction, int coarse)
{
    if (r.kind != ROW_VALUE || !direction)
        return;
    int step = coarse ? r.coarse : r.fine;
    int current = 0;
    TileValue v = {0};
    SoundSettings sound = {0};
    ReverbSettings reverb = {0};
    if (r.id >= ROW_WAVE1)
        sound = e->score.sounds[e->sound_channel];
    if (r.id == ROW_SIZE || r.id == ROW_AMOUNT)
        reverb = e->score.reverb;
    if (r.id < ACTION_LOCK_RELEASE + 1)
        v = e->score.tiles[e->target].value;
    switch (r.id) {
    case ROW_BPM:
        current = e->score.bpm;
        break;
    case ROW_SLOT:
        current = e->storage_slot;
        break;
    case ROW_SIZE:
        current = reverb.size;
        break;
    case ROW_AMOUNT:
        current = reverb.amount;
        break;
    case ROW_WAVE1:
        current = sound.wave_a;
        break;
    case ROW_WAVE2:
        current = sound.wave_b;
        break;
    case ROW_AMP_ATTACK:
        current = sound.attack;
        break;
    case ROW_AMP_RELEASE:
        current = sound.release;
        break;
    case ROW_MIX_ATTACK:
        current = sound.mix_attack;
        break;
    case ROW_MIX_RELEASE:
        current = sound.mix_release;
        break;
    case ROW_SWEEP:
        current = sound.sweep;
        break;
    case ROW_DECAY:
        current = sound.decay;
        break;
    case ROW_SEND:
        current = sound.reverb;
        break;
    case ACTION_LENGTH:
        current = e->score.lanes[e->lane].length;
        break;
    case ACTION_DIVISION:
        while (current < SCORE_DIVISIONS - 1 &&
               score_divisions[current] != score_division(&e->score, e->lane))
            current++;
        break;
    case ACTION_CHANNEL:
        current = score_channel(&e->score, e->lane);
        break;
    case ACTION_PITCH:
        current = v.pitch;
        break;
    case ACTION_DURATION:
        current = v.length;
        break;
    case ACTION_PERIOD:
        current = v.period;
        break;
    case ACTION_CHANCE:
        current = v.chance;
        break;
    case ACTION_LOCK_ATTACK_ENABLE:
        current = !!(v.lock_mask & LOCK_ATTACK);
        break;
    case ACTION_LOCK_RELEASE_ENABLE:
        current = !!(v.lock_mask & LOCK_RELEASE);
        break;
    case ACTION_LOCK_ATTACK:
        current = v.attack;
        break;
    case ACTION_LOCK_RELEASE:
        current = v.release;
        break;
    default:
        return;
    }
    int candidate = clamp(current + direction * step, r.min, r.max);
    if (candidate == current)
        return;
    if (e->load_busy && r.id != ROW_SLOT)
        return;
    ScoreResult result = SCORE_OK;
    switch (r.id) {
    case ROW_BPM:
        result = score_set_bpm(&e->score, candidate);
        break;
    case ROW_SLOT:
        e->storage_slot = candidate;
        if (!e->load_busy)
            e->message = "";
        return;
    case ROW_SIZE:
        reverb.size = candidate;
        result = score_set_reverb(&e->score, reverb);
        break;
    case ROW_AMOUNT:
        reverb.amount = candidate;
        result = score_set_reverb(&e->score, reverb);
        break;
    case ROW_WAVE1:
        sound.wave_a = candidate;
        break;
    case ROW_WAVE2:
        sound.wave_b = candidate;
        break;
    case ROW_AMP_ATTACK:
        sound.attack = candidate;
        break;
    case ROW_AMP_RELEASE:
        sound.release = candidate;
        break;
    case ROW_MIX_ATTACK:
        sound.mix_attack = candidate;
        break;
    case ROW_MIX_RELEASE:
        sound.mix_release = candidate;
        break;
    case ROW_SWEEP:
        sound.sweep = candidate;
        break;
    case ROW_DECAY:
        sound.decay = candidate;
        break;
    case ROW_SEND:
        sound.reverb = candidate;
        break;
    case ACTION_LENGTH:
        result = score_resize(&e->score, e->lane, candidate);
        break;
    case ACTION_DIVISION:
        result = score_set_division(&e->score, e->lane, score_divisions[candidate]);
        break;
    case ACTION_CHANNEL:
        result = score_set_channel(&e->score, e->lane, candidate);
        break;
    case ACTION_PITCH:
        v.pitch = candidate;
        break;
    case ACTION_DURATION:
        v.length = candidate;
        break;
    case ACTION_PERIOD:
        v.period = candidate;
        break;
    case ACTION_CHANCE:
        v.chance = candidate;
        break;
    case ACTION_LOCK_ATTACK_ENABLE:
        if (candidate)
            v.lock_mask |= LOCK_ATTACK;
        else {
            v.lock_mask &= ~LOCK_ATTACK;
            v.attack = 0;
        }
        break;
    case ACTION_LOCK_RELEASE_ENABLE:
        if (candidate)
            v.lock_mask |= LOCK_RELEASE;
        else {
            v.lock_mask &= ~LOCK_RELEASE;
            v.release = 0;
        }
        break;
    case ACTION_LOCK_ATTACK:
        if (!(v.lock_mask & LOCK_ATTACK))
            return;
        v.attack = candidate;
        break;
    case ACTION_LOCK_RELEASE:
        if (!(v.lock_mask & LOCK_RELEASE))
            return;
        v.release = candidate;
        break;
    }
    if (r.id >= ROW_WAVE1)
        result = score_set_sound(&e->score, e->sound_channel, sound);
    if (r.id == ACTION_PITCH || r.id == ACTION_DURATION || r.id == ACTION_PERIOD ||
        r.id == ACTION_CHANCE || r.id == ACTION_LOCK_ATTACK_ENABLE ||
        r.id == ACTION_LOCK_RELEASE_ENABLE || r.id == ACTION_LOCK_ATTACK ||
        r.id == ACTION_LOCK_RELEASE) {
        result = score_edit(&e->score, e->target, v);
        if (!result && v.kind == TILE_NOTE)
            e->last_note = v;
    }
    e->message = score_message(result);
}

/*
 * Defers card actions to the main loop and retains parent selection when
 * entering a nested editor view.
 */
static void activate(Editor* e, EditorRow r)
{
    if (e->load_busy && r.id != ROW_REVERB && r.id != ROW_SLOT)
        return;
    switch (r.id) {
    case ROW_REVERB:
        e->mode = EDIT_REVERB;
        e->parent_selected = e->selected;
        e->selected = 0;
        break;
    case ROW_CHECK:
        e->storage_request = STORAGE_ACTION_CHECK;
        break;
    case ROW_SAVE:
        e->storage_request = STORAGE_ACTION_SAVE;
        break;
    case ROW_LOAD:
        e->storage_request = STORAGE_ACTION_LOAD;
        break;
    case ACTION_CREATE:
        finish(e, score_create(&e->score, e->x, e->y, SCORE_INITIAL_LENGTH));
        break;
    case ACTION_PLACE:
        e->tile_candidate = e->last_tile;
        e->parent_selected = e->selected;
        e->mode = EDIT_PICKER;
        break;
    case ACTION_PASTE:
        finish(e, score_paste(&e->score, e->x, e->y, &e->clipboard));
        break;
    case ACTION_COPY:
        score_copy(&e->score, e->x, e->y, &e->clipboard);
        e->mode = EDIT_PLANE;
        break;
    case ACTION_REMOVE:
        if (e->score.tiles[e->target].value.kind == TILE_JUMP) {
            e->parent_selected = e->selected;
            e->mode = EDIT_DELETE;
        } else
            finish(e, score_remove(&e->score, e->x, e->y));
        break;
    case ACTION_DELETE:
        e->parent_selected = e->selected;
        e->mode = EDIT_DELETE;
        break;
    case ACTION_SOUND:
        e->sound_channel = score_channel(&e->score, e->lane);
        e->parent_selected = e->selected;
        e->selected = 1;
        e->mode = EDIT_SOUND;
        break;
    case ACTION_PATTERN:
        e->value = e->score.tiles[e->target].value;
        e->pattern_cursor = 0;
        e->parent_selected = e->selected;
        e->mode = EDIT_PATTERN;
        break;
    default:
        break;
    }
}

/*
 * Prioritizes disconnection and mode exits before gesture or row handling so
 * one input frame has one effect.
 */
static void update(Editor* e, InputFrame f)
{
    if (!f.connected) {
        if (e->gesture || e->mode == EDIT_MOVE)
            cancel_move(e);
        return;
    }
    if (f.select) {
        int close = e->mode == EDIT_MAIN;
        if (e->gesture || e->mode == EDIT_MOVE)
            cancel_move(e);
        e->gesture = 0;
        e->mode = close ? EDIT_PLANE : EDIT_MAIN;
        e->selected = e->load_busy ? 2 : 0;
        e->message = "";
        return;
    }
    if (e->mode == EDIT_PLANE || e->mode == EDIT_MOVE) {
        if (f.cross && e->mode == EDIT_PLANE && !e->load_busy) {
            e->gesture = 1;
            e->directed = 0;
            e->source_x = e->x;
            e->source_y = e->y;
        }
        if (f.circle && e->gesture) {
            cancel_move(e);
            return;
        }
        if (e->gesture && (f.dx || f.dy)) {
            e->directed = 1;
            Cell c = score_at(&e->score, e->source_x, e->source_y);
            if (c.kind == CELL_TILE || c.kind == CELL_HEAD)
                e->mode = EDIT_MOVE;
        }
        e->x = clamp(e->x + f.dx, 0, SCORE_WIDTH - 1);
        e->y = clamp(e->y + f.dy, 0, SCORE_HEIGHT - 1);
        if (f.dx || f.dy)
            e->message = "";
        if (f.cross_released && e->gesture) {
            e->gesture = 0;
            if (e->mode == EDIT_MOVE) {
                ScoreResult r = score_apply_move(
                    &e->score, score_plan_move(&e->score, e->source_x, e->source_y, e->x, e->y));
                if (r) {
                    e->x = e->source_x;
                    e->y = e->source_y;
                }
                e->mode = EDIT_PLANE;
                e->message = score_message(r);
            } else if (!e->directed)
                enter_context(e);
        }
        return;
    }
    e->gesture = 0;
    if (f.circle) {
        if (e->mode == EDIT_MAIN || e->mode == EDIT_MENU)
            e->mode = EDIT_PLANE;
        else if (e->mode == EDIT_REVERB) {
            e->mode = EDIT_MAIN;
            e->selected = e->parent_selected;
        } else {
            e->mode = EDIT_MENU;
            e->selected = e->parent_selected;
        }
        e->message = "";
        return;
    }
    if (e->mode == EDIT_PICKER) {
        e->tile_candidate = (TileKind)clamp(e->tile_candidate + f.row_dy, 1, TILE_KIND_COUNT - 1);
        if (f.cross && !e->load_busy) {
            TileValue v =
                e->tile_candidate == TILE_NOTE ? e->last_note : score_default(e->tile_candidate);
            ScoreResult r = score_place_value(&e->score, e->x, e->y, v);
            if (!r) {
                e->last_tile = e->tile_candidate;
                if (v.kind == TILE_NOTE)
                    e->last_note = v;
            }
            finish(e, r);
        }
        return;
    }
    if (e->mode == EDIT_DELETE) {
        if (f.cross && !e->load_busy)
            finish(e,
                   e->target ? score_remove(&e->score, e->x, e->y)
                             : score_delete(&e->score, e->lane));
        return;
    }
    if (e->mode == EDIT_PATTERN) {
        int p = e->pattern_cursor, period = e->score.tiles[e->target].value.period;
        p = clamp(p + f.dx + f.dy * 8, 0, period - 1);
        e->pattern_cursor = p;
        if (f.cross && !e->load_busy) {
            TileValue v = e->score.tiles[e->target].value;
            v.pattern ^= (uint32_t)1 << p;
            ScoreResult r = score_edit(&e->score, e->target, v);
            if (!r)
                e->value = v;
            e->message = score_message(r);
        }
        return;
    }
    EditorRow rows[EDITOR_ROWS];
    int count = editor_rows(e, rows);
    if (!count)
        return;
    if (e->selected < 0 || e->selected >= count || rows[e->selected].kind == ROW_HEADING)
        e->selected = selected_row(rows, count, 0, 1);
    if (f.row_dy) {
        e->selected = selected_row(rows, count, e->selected, f.row_dy);
        return; // A row move never also changes a value.
    }
    if (f.value_dir)
        adjust(e, rows[e->selected], f.value_dir, f.value_coarse);
    if (f.cross && rows[e->selected].kind != ROW_VALUE)
        activate(e, rows[e->selected]);
}

void editor_refresh_capacity(Editor* e)
{
    if (e->capacity_revision == e->score.revision)
        return;
    e->capacity_revision = e->score.revision;
    e->free_bytes = SCORE_FILE_BYTES - (int)score_format_measure(&e->score);
}

void editor_update(Editor* e, InputFrame f)
{
    update(e, f);
    editor_refresh_capacity(e);
}
