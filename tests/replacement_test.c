/*
 * replacement_test.c - Host sequencer replacement regression tests
 *
 * Implementation notes:
 *
 * A recording sink checks complete-slice adoption, note ownership and
 * replacement timing without hardware callbacks.
 */

#include "value_api.h"

#include "sequencer.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

typedef enum
{
    EVENT_ON,                       // A traced note started.
    EVENT_OFF,                      // A traced note was released.
    EVENT_STOP                      // The traced transport stopped.
} EventKind;

typedef struct
{
    EventKind kind;
    AudioTime at;
    int pitch;
    SoundSettings sound;
    uint32_t token;
} Event;

static Event events[512];
static int event_count;
static int slot;
static uint32_t serial;

static uint32_t note(void* context, AudioTime at, int pitch,
                     SoundSettings sound)
{
    (void)context;
    assert(event_count < (int)(sizeof(events) / sizeof(events[0])));
    uint32_t token = (++serial << 5) | (slot++ % SEQUENCER_VOICES);
    events[event_count++] = (Event){EVENT_ON, at, pitch, sound, token};
    return token;
}

static void off(void* context, AudioTime at, uint32_t token)
{
    (void)context;
    assert(event_count < (int)(sizeof(events) / sizeof(events[0])));
    events[event_count++] =
        (Event){.kind = EVENT_OFF, .at = at, .token = token};
}

static void stop(void* context, AudioTime at)
{
    (void)context;
    assert(event_count < (int)(sizeof(events) / sizeof(events[0])));
    events[event_count++] = (Event){.kind = EVENT_STOP, .at = at};
}

static NoteSink trace = {NULL, note, off, stop, NULL};

static void reset_trace(void)
{
    event_count = slot = 0;
}

static void lane(Score* score, int x, int y, int length, int division,
                 int channel, int pitch)
{
    assert(!score_create(score, x, y, length));
    int id = test_score_at(score, x, y).lane;
    assert(id >= 0);
    assert(!score_set_division(score, id, division));
    assert(!score_set_channel(score, id, channel));
    TileValue value = test_score_default(TILE_NOTE);
    value.pitch = pitch;
    assert(!test_score_place_value(score, x + 1, y, value));
}

static int count(EventKind kind, AudioTime at, int pitch)
{
    int result = 0;
    for (int i = 0; i < event_count; i++)
    {
        if (events[i].kind == kind && events[i].at == at &&
            (kind != EVENT_ON || events[i].pitch == pitch))
        {
            result++;
        }
    }
    return result;
}

static const Event* find_note(AudioTime at, int pitch)
{
    for (int i = 0; i < event_count; i++)
    {
        if (events[i].kind == EVENT_ON && events[i].at == at &&
            events[i].pitch == pitch)
        {
            return &events[i];
        }
    }
    return NULL;
}

static int count_off(uint32_t token, AudioTime before_or_at)
{
    int result = 0;
    for (int i = 0; i < event_count; i++)
    {
        if (events[i].kind == EVENT_OFF && events[i].token == token &&
            events[i].at <= before_or_at)
        {
            result++;
        }
    }
    return result;
}

static int count_off_at(uint32_t token, AudioTime at)
{
    int result = 0;
    for (int i = 0; i < event_count; i++)
    {
        if (events[i].kind == EVENT_OFF && events[i].token == token &&
            events[i].at == at)
        {
            result++;
        }
    }
    return result;
}

static Sequencer* service_until(Sequencer* active, Sequencer* expected,
                                AudioTime now)
{
    for (int i = 0; i < 16 && active != expected; i++)
    {
        active = sequencer_service(active, now);
    }
    return active;
}

/*
 * Checks that the selected master lap controls takeover even when other lanes
 * use different divisions.
 */
static void mixed_divisions_and_master(void)
{
    Score old;
    Score incoming;
    Sequencer current;
    Sequencer prepared;
    score_init(&old);
    // The upper lane executes first, but Channel 1 below it owns the seam.
    lane(&old, 0, 0, 1, 32, 1, 40);
    lane(&old, 0, 4, 2, 16, 0, 50);
    score_init(&incoming);
    lane(&incoming, 0, 0, 1, 16, 0, 70);
    lane(&incoming, 0, 4, 1, 32, 1, 80);
    reset_trace();
    sequencer_start(&current, &old, trace, 0);
    sequencer_service(&current, 0);
    sequencer_start(&prepared, &incoming, trace, 0);
    assert(sequencer_replace(&current, &prepared, 1));
    assert(!sequencer_replace(&current, &prepared, 1));
    AudioTime half = SEQUENCER_HZ / 16;
    AudioTime step = SEQUENCER_HZ / 8;
    AudioTime seam = 2 * step;
    Sequencer* active = sequencer_service(&current, half);
    active = sequencer_service(active, step);
    active = sequencer_service(active, step + half);
    active = service_until(active, &prepared, seam);
    assert(active == &prepared);
    assert(count(EVENT_ON, half, 40) == 1 && count(EVENT_ON, step, 40) == 1);
    assert(count(EVENT_ON, seam, 40) == 0 && count(EVENT_ON, seam, 50) == 0);
    assert(count(EVENT_ON, seam, 70) == 1 && count(EVENT_ON, seam, 80) == 1);
}

/*
 * Checks branch traversal at the seam and carries outstanding gate-offs into
 * the new state.
 */
static void branch_lap_and_gate_transfer(void)
{
    Score old;
    Score incoming;
    Sequencer current;
    Sequencer prepared;
    score_init(&old);
    assert(!score_create(&old, 0, 0, 1));
    TileValue jump = test_score_default(TILE_JUMP);
    assert(!test_score_place_value(&old, 1, 0, jump));
    int branch = old.tiles[test_score_at(&old, 1, 0).tile].branch;
    assert(!score_resize(&old, branch, 2));
    Lane* b = &old.lanes[branch];
    TileValue held = test_score_default(TILE_RELATIVE);
    held.lock_mask = LOCK_ATTACK;
    held.attack = 100;
    assert(!test_score_place_value(&old, b->x + 1, b->y, held));
    TileValue long_note = test_score_default(TILE_NOTE);
    long_note.pitch = 60;
    long_note.length = 60;
    assert(!test_score_place_value(&old, b->x + 2, b->y, long_note));
    score_init(&incoming);
    lane(&incoming, 0, 0, 1, 16, 0, 72);
    reset_trace();
    sequencer_start(&current, &old, trace, 0);
    sequencer_service(&current, 0);
    sequencer_start(&prepared, &incoming, trace, 0);
    assert(sequencer_replace(&current, &prepared, 1));
    AudioTime step = SEQUENCER_HZ / 8;
    AudioTime seam = 3 * step;
    Sequencer* active = sequencer_service(&current, step);
    active = sequencer_service(active, 2 * step);
    const Event* outgoing = find_note(2 * step, 60);
    assert(outgoing);
    uint32_t outgoing_token = outgoing->token;
    active = service_until(active, &prepared, seam);
    assert(active == &prepared && count(EVENT_ON, seam, 72) == 1);
    // The incoming runner starts clean, while the outgoing long gate remains.
    assert(active->runners[active->master].held_count == 0);
    AudioTime gate = 5 * step;
    assert(count_off(outgoing_token, gate - 1) == 0);
    active = sequencer_service(active, gate);
    assert(active == &prepared && count_off_at(outgoing_token, gate) == 1);
}

/*
 * Exercises replacement after a gate suppresses or redirects a branch visit.
 */
static void conditional_branch_seams(void)
{
    AudioTime step = SEQUENCER_HZ / 8;
    for (int taken = 0; taken <= 1; taken++)
    {
        Score old;
        Score incoming;
        Sequencer current;
        Sequencer prepared;
        score_init(&old);
        assert(!score_create(&old, 0, 0, 1));
        TileValue gate = test_score_default(TILE_CYCLE);
        gate.period = 2;
        gate.pattern = taken ? 1 : 2;
        assert(!test_score_place_value(&old, 1, 0, gate));
        assert(!score_place(&old, 1, 1, TILE_JUMP));
        TileId gate_id = test_score_at(&old, 1, 0).tile;
        TileId jump_id = old.tiles[gate_id].next;
        assert(old.tiles[jump_id].value.kind == TILE_JUMP);
        int branch = old.tiles[jump_id].branch;
        assert(!score_resize(&old, branch, 2));
        TileValue branch_note = test_score_default(TILE_NOTE);
        branch_note.pitch = 61;
        assert(!test_score_place_value(&old, old.lanes[branch].x + 2,
                                  old.lanes[branch].y, branch_note));

        score_init(&incoming);
        lane(&incoming, 0, 0, 1, 16, 0, 73);
        reset_trace();
        sequencer_start(&current, &old, trace, 0);
        sequencer_start(&prepared, &incoming, trace, 0);
        assert(sequencer_replace(&current, &prepared, 0));
        Sequencer* active = sequencer_service(&current, 0);
        assert(active == &current && count(EVENT_ON, 0, 73) == 0);
        if (taken)
        {
            active = sequencer_service(active, step);
            active = sequencer_service(active, 2 * step);
            assert(active == &current && count(EVENT_ON, 2 * step, 61) == 1);
            assert(count(EVENT_ON, step, 73) == 0 &&
                   count(EVENT_ON, 2 * step, 73) == 0);
        }
        AudioTime seam = taken ? 3 * step : step;
        active = sequencer_service(active, seam);
        assert(active == &prepared && count(EVENT_ON, seam, 73) == 1);
    }
}

/*
 * Checks immediate adoption when no active lap can establish a future seam.
 */
static void empty_and_stop_adoption(void)
{
    Score empty;
    Score incoming;
    Sequencer current;
    Sequencer prepared;
    score_init(&empty);
    score_init(&incoming);
    lane(&incoming, 0, 0, 1, 16, 0, 65);
    reset_trace();
    sequencer_start(&current, &empty, trace, 100);
    sequencer_start(&prepared, &incoming, trace, 0);
    assert(sequencer_replace(&current, &prepared, 100));
    Sequencer* active = sequencer_service(&current, 100);
    assert(active == &prepared && count(EVENT_ON, 100, 65) == 1);

    Score old;
    Sequencer stopped;
    Sequencer next;
    score_init(&old);
    lane(&old, 0, 0, 4, 16, 0, 48);
    reset_trace();
    sequencer_start(&stopped, &old, trace, 0);
    sequencer_service(&stopped, 0);
    sequencer_start(&next, &incoming, trace, 0);
    assert(sequencer_replace(&stopped, &next, 1));
    active = sequencer_stop(&stopped, 10);
    assert(active == &next && !active->playing &&
           count(EVENT_STOP, 10, 0) == 1);
    sequencer_service(active, 10);
    assert(count(EVENT_ON, 10, 65) == 0);
}

/*
 * Checks that replacement waits for a dense slice to finish after its lap
 * boundary is discovered.
 */
static void split_slice_discovers_seam(void)
{
    Score old;
    Score incoming;
    Sequencer current;
    Sequencer prepared;
    score_init(&old);
    assert(!score_create(&old, 0, 0, 1));
    TileValue held = test_score_default(TILE_RELATIVE);
    held.lock_mask = LOCK_ATTACK;
    held.attack = 1;
    for (int y = 0; y < 40; y++) assert(!test_score_place_value(&old, 1, y, held));
    score_init(&incoming);
    lane(&incoming, 0, 0, 1, 16, 0, 90);
    reset_trace();
    sequencer_start(&current, &old, trace, 0);
    sequencer_service(&current, 0);
    assert(current.slicing);
    sequencer_start(&prepared, &incoming, trace, 0);
    assert(sequencer_replace(&current, &prepared, 1));
    Sequencer* active = sequencer_service(&current, SEQUENCER_HZ / 8);
    assert(active == &prepared && count(EVENT_ON, SEQUENCER_HZ / 8, 90) == 1);
}

/*
 * Checks takeover before a pending lane can emit a note from the old snapshot.
 */
static void replacement_preempts_pending_lane(void)
{
    Score old;
    Score published;
    Score incoming;
    Sequencer current;
    Sequencer prepared;
    score_init(&old);
    lane(&old, 0, 0, 2, 16, 0, 41);
    reset_trace();
    sequencer_start(&current, &old, trace, 0);
    sequencer_service(&current, 0);

    published = old;
    lane(&published, 0, 4, 1, 16, 1, 52);
    assert(sequencer_resync(&current, &published, 1));
    int pending = -1;
    for (int i = 0; i < SCORE_LANES; i++)
    {
        if (current.runners[i].active && i != current.master) pending = i;
    }
    assert(pending >= 0 && current.runners[pending].next == UINT64_MAX);

    score_init(&incoming);
    lane(&incoming, 0, 0, 1, 16, 0, 76);
    incoming.sounds[0].attack = 321;
    sequencer_start(&prepared, &incoming, trace, 0);
    assert(sequencer_replace(&current, &prepared, 1));
    AudioTime step = SEQUENCER_HZ / 8;
    AudioTime seam = 2 * step;
    Sequencer* active = sequencer_service(&current, step);
    active = service_until(active, &prepared, seam);
    const Event* adopted = find_note(seam, 76);
    assert(active == &prepared && adopted && adopted->sound.attack == 321);
    assert(count(EVENT_ON, seam, 52) == 0);
}

int main(void)
{
    mixed_divisions_and_master();
    branch_lap_and_gate_transfer();
    conditional_branch_seams();
    empty_and_stop_adoption();
    split_slice_discovers_seam();
    replacement_preempts_pending_lane();
    puts("PASS: replacement seams, branches, gates, pending lanes, stop and "
         "split slices");
}
