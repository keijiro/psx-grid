/*
 * audio_test.c - Host audio and sequencer regression tests
 *
 * Implementation notes:
 *
 * A recording sink and fake driver inspect event timing, voice allocation
 * and live editor publication without SPU hardware.
 */

#include "value_api.h"

#include "audio_synth.h"
#include "editor.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static Score score;
static Score snapshot;
static Score updated;
static Sequencer seq;
static Audio* audio;
static Editor editor;

static AudioVoiceView voice(int slot)
{
    AudioVoiceView view;
    audio_voice_view(audio, slot, &view);
    return view;
}

typedef struct
{
    AudioTime at;
    int pitch;
    SoundSettings sound;
} Event;

static Event events[4096];
static AudioTime offs[4096];
static int count;
static int off_count;
static int slot;
static int levels[SEQUENCER_VOICES];
static int pitches[SEQUENCER_VOICES];
static int starts;
static int stops;
static uint32_t last_starts;
static uint32_t last_stops;
static uint32_t last_wet;
static AudioTime dispatch_time;
static uint32_t serial;

static uint32_t note(void* ctx, AudioTime at, int pitch,
                     const SoundSettings* sound)
{
    (void)ctx;
    assert(count < 4096);
    events[count++] = (Event){at, pitch, *sound};
    return (++serial << 5) | (slot++ % SEQUENCER_VOICES);
}

static void off(void* ctx, AudioTime at, uint32_t token)
{
    (void)ctx;
    (void)token;
    offs[off_count++] = at;
}

static void stop(void* ctx, AudioTime at)
{
    (void)ctx;
    (void)at;
}

static NoteSink trace = {NULL, note, off, stop, NULL};

static void base(int length)
{
    score_init(&score);
    assert(!score_create(&score, 0, 0, length));
    count = off_count = slot = 0;
}

static void put(int x, int y, TileValue v)
{
    assert(!test_score_place_value(&score, x, y, v));
}

static TileValue relative(int a, int r)
{
    TileValue v = test_score_default(TILE_RELATIVE);
    v.lock_mask = 3;
    v.attack = a;
    v.release = r;
    return v;
}

static void start(void)
{
    snapshot = score;
    sequencer_start(&seq, &snapshot, trace, 0);
    sequencer_service(&seq, 0);
}

/*
 * Checks step and gate deadlines against exact sequencer tick arithmetic.
 */
static void timing(void)
{
    for (int d = 0; d < SCORE_DIVISIONS; d++)
    {
        base(16);
        assert(!score_set_division(&score, 0, score_divisions[d]));
        for (int i = 1; i <= 16; i++) put(i, 0, test_score_default(TILE_NOTE));
        start();
        uint32_t duration = 2 * SEQUENCER_HZ / score_divisions[d];
        for (int i = 1; i < 2048; i++)
        {
            sequencer_service(&seq, (AudioTime)i * duration);
        }
        assert(count == 2048 && off_count == 2047);
        assert(events[2047].at ==
               (AudioTime)2047 * 2 * SEQUENCER_HZ / score_divisions[d]);
        assert(offs[2046] == events[2047].at);
    }
    base(2);
    TileValue v = test_score_default(TILE_NOTE);
    v.length = 5;
    put(1, 0, v);
    v.length = 45;
    put(1, 1, v);
    start();
    AudioTime duration = SEQUENCER_HZ / 8;
    sequencer_service(&seq, duration / 4);
    assert(off_count == 1 && offs[0] == duration / 4);
    sequencer_service(&seq, duration);
    sequencer_service(&seq, duration * 2);
    sequencer_service(&seq, duration * 9 / 4);
    assert(off_count == 3 && offs[1] == duration * 9 / 4);
    base(1);
    start();
    assert(seq.playing && !count);
    sequencer_stop(&seq, 0);
    assert(!seq.playing);
}

/*
 * Checks lock inheritance and release timing across adjacent steps.
 */
static void locks(void)
{
    base(2);
    put(1, 0, test_score_default(TILE_NOTE));
    put(1, 1, relative(100, -5));
    put(1, 2, test_score_default(TILE_NOTE));
    put(1, 3, relative(16000, 16000));
    put(1, 4, relative(-100, -100));
    put(1, 5, test_score_default(TILE_NOTE));
    start();
    assert(count == 3);
    assert(events[0].sound.attack == 5 && events[0].sound.release == 5);
    assert(events[1].sound.attack == 105 && events[1].sound.release == 0);
    assert(events[2].sound.attack == 15900 && events[2].sound.release == 15900);
    sequencer_service(&seq, SEQUENCER_HZ /
                                4); // Drain two boundaries; no accumulation.
    assert(events[count - 3].sound.attack == 5);
    base(2);
    assert(!score_set_division(&score, 0, 8));
    put(1, 0, relative(100, 200));
    assert(!score_create(&score, 0, 4, 4));
    assert(!score_set_division(&score, 1, 16));
    for (int i = 1; i <= 4; i++) put(i, 4, test_score_default(TILE_NOTE));
    start();
    assert(count == 1 && events[0].sound.attack == 105);
    sequencer_service(&seq, SEQUENCER_HZ / 8);
    assert(events[1].sound.attack == 105);
    sequencer_service(&seq, SEQUENCER_HZ / 4);
    assert(events[2].sound.attack == 5);
    sequencer_service(&seq, 3 * SEQUENCER_HZ / 8);
    assert(events[3].sound.attack == 5);
    sequencer_service(&seq, SEQUENCER_HZ / 2);
    assert(events[4].sound.release == 205);
    // Lower held locks must never affect fresh notes above them.
    base(2);
    put(1, 0, test_score_default(TILE_NOTE));
    put(2, 0, test_score_default(TILE_NOTE));
    assert(!score_create(&score, 0, 4, 1));
    assert(!score_set_division(&score, 1, 8));
    put(1, 4, relative(100, 100));
    start();
    sequencer_service(&seq, SEQUENCER_HZ / 8);
    assert(events[1].sound.attack == 5);
    // A failed gate keeps the locks above it, never those below it.
    base(1);
    put(1, 0, relative(30, 40));
    TileValue gate = test_score_default(TILE_PROBABILITY);
    gate.chance = 0;
    put(1, 1, gate);
    put(1, 2, relative(500, 500));
    put(1, 3, test_score_default(TILE_NOTE));
    assert(!score_create(&score, 0, 6, 1));
    put(1, 6, test_score_default(TILE_NOTE));
    start();
    assert(count == 1 && events[0].sound.attack == 35 &&
           seq.runners[0].held_count == 1);
    uint32_t random = seq.random;
    sequencer_service(&seq, 1);
    assert(seq.random == random);
    // Snapshot values and base are immutable after committed editor changes.
    assert(!test_score_set_sound(
        &score, 0,
        (SoundSettings){999, 999, WAVE_SINE, WAVE_SINE, 0, 0, 0, 200, 0}));
    assert(!score_remove(&score, 1, 0));
    sequencer_service(&seq, SEQUENCER_HZ / 8);
    assert(events[1].sound.attack == 35);
}

/*
 * Exercises conditional tiles and jump branches while verifying emitted note
 * order.
 */
static void gates_branches(void)
{
    base(1);
    TileValue gate = test_score_default(TILE_CYCLE);
    gate.period = 3;
    gate.pattern = 5;
    put(1, 0, gate);
    put(1, 1, test_score_default(TILE_NOTE));
    start();
    for (int i = 1; i < 6; i++)
    {
        sequencer_service(&seq, (AudioTime)i * SEQUENCER_HZ / 8);
    }
    assert(count == 4 && events[1].at == SEQUENCER_HZ / 4);
    for (int chance = 0; chance <= 100; chance += 50)
    {
        base(1);
        gate = test_score_default(TILE_PROBABILITY);
        gate.chance = chance;
        put(1, 0, gate);
        put(1, 1, test_score_default(TILE_NOTE));
        start();
        for (int i = 1; i < 100; i++)
        {
            sequencer_service(&seq, (AudioTime)i * SEQUENCER_HZ / 8);
        }
        int n = count;
        uint32_t random = seq.random;
        Event saved[100];
        memcpy(saved, events, n * sizeof(Event));
        count = off_count = 0;
        sequencer_start(&seq, &snapshot, trace, 0);
        for (int i = 0; i < 100; i++)
        {
            sequencer_service(&seq, (AudioTime)i * SEQUENCER_HZ / 8);
        }
        assert(count == n && random == seq.random &&
               !memcmp(events, saved, n * sizeof(Event)));
        assert(chance == 50 ? n > 20 && n < 80 : n == chance);
    }
    base(2);
    put(1, 0, test_score_default(TILE_JUMP));
    int first = score.tiles[test_score_at(&score, 1, 0).tile].branch;
    assert(!test_score_apply_move(&score,
                             test_score_plan_move(&score, score.lanes[first].x,
                                             score.lanes[first].y, 20, 10)));
    // Two reached jumps: the lower destination wins, but a note below both
    // sounds.
    put(1, 1, test_score_default(TILE_JUMP));
    int second = score.tiles[test_score_at(&score, 1, 1).tile].branch;
    put(1, 2, test_score_default(TILE_NOTE));
    assert(!score_resize(&score, second, 1));
    Lane b = score.lanes[second];
    put(b.x + 1, b.y, test_score_default(TILE_JUMP));
    int child = score.tiles[test_score_at(&score, b.x + 1, b.y).tile].branch;
    assert(!score_resize(&score, child, 1));
    b = score.lanes[child];
    TileValue v = test_score_default(TILE_NOTE);
    v.pitch = 72;
    put(b.x + 1, b.y, v);
    start();
    assert(seq.count == 1 && count == 1 && seq.runners[0].lane == second &&
           first != second);
    sequencer_service(&seq, SEQUENCER_HZ / 8);
    assert(seq.runners[0].lane == child);
    sequencer_service(&seq, SEQUENCER_HZ / 4);
    assert(events[1].pitch == 72 && seq.runners[0].lane == 0 &&
           seq.runners[0].lap == 1);
    sequencer_service(&seq, 3 * SEQUENCER_HZ / 8);
    assert(events[2].pitch == 48);
    // Order is the origin head position, including x ties, not pool ID.
    base(1);
    assert(!score_create(&score, 8, 0, 1));
    put(9, 0, relative(100, 100));
    put(1, 0, test_score_default(TILE_NOTE));
    assert(!test_score_apply_move(&score, test_score_plan_move(&score, 0, 0, 0, 4)));
    start();
    assert(seq.runners[0].origin == 1 && events[0].sound.attack == 105);
}

static void driver_start(void* ctx, int voice, int bank,
                         const SoundSettings* sound)
{
    (void)ctx;
    (void)voice;
    (void)bank;
    (void)sound;
}

static void driver_pitch(void* ctx, int voice, int pitch)
{
    (void)ctx;
    assert(pitch > 0 && pitch < 0x4000);
    pitches[voice] = pitch;
}

static void driver_volume(void* ctx, int voice, int a, int b)
{
    (void)ctx;
    assert(a >= 0 && b >= 0 && a + b <= AUDIO_LEVEL);
    levels[voice] = a + b;
}

static void driver_flush(void* ctx, uint32_t on, uint32_t off_bits,
                         uint32_t wet, uint32_t dispatch_peak)
{
    (void)ctx;
    (void)dispatch_peak;
    assert(!(on & off_bits));
    last_starts = on;
    last_stops = off_bits;
    last_wet = wet;
    starts += (on != 0);
    stops += (off_bits != 0);
}

static AudioTime driver_clock(void* context)
{
    (void)context;
    return dispatch_time;
}

/*
 * Checks pair allocation, stealing, and generation-safe note release through
 * the fake driver.
 */
static void voices(void)
{
    audio = audio_init(NULL, driver_start, driver_volume, driver_pitch,
                       driver_flush, NULL, NULL);
    NoteSink sink = audio_sink(audio);
    uint32_t tokens[SEQUENCER_VOICES];
    for (int i = 0; i < SEQUENCER_VOICES; i++)
    {
        tokens[i] = sink.on(
            sink.context, 0, 48 + i,
            &(SoundSettings){100, 200, WAVE_SINE, WAVE_SINE, 0, 0, 0, 200, 0});
    }
    sink.advance(sink.context, audio_ms(50));
    assert(levels[0] >= AUDIO_LEVEL / 2 - 1 &&
           levels[0] <= (AUDIO_LEVEL + 1) / 2 && starts == 1);
    sink.off(sink.context, audio_ms(50), tokens[0]);
    sink.advance(sink.context, audio_ms(150));
    assert(levels[0] >= AUDIO_LEVEL / 4 - 1 &&
           levels[0] <= (AUDIO_LEVEL + 3) / 4 + 1);
    uint32_t replacement =
        sink.on(sink.context, audio_ms(150), 80,
                &(SoundSettings){0, 5, WAVE_SINE, WAVE_SINE, 0, 0, 0, 200, 0});
    assert((replacement & 31) == 0 && audio_steals(audio) == 1);
    sink.off(sink.context, audio_ms(160), tokens[0]);
    assert(!voice(0).releasing);
    replacement =
        sink.on(sink.context, audio_ms(160), 81,
                &(SoundSettings){0, 0, WAVE_SINE, WAVE_SINE, 0, 0, 0, 200, 0});
    assert((replacement & 31) == 1);
    sink.off(sink.context, audio_ms(160), replacement);
    sink.advance(sink.context, audio_ms(160));
    assert(!voice(1).active && levels[1] == 0);
    sink.on(
        sink.context, audio_ms(161), 82,
        &(SoundSettings){16000, 16000, WAVE_SINE, WAVE_SINE, 0, 0, 0, 200, 0});
    assert(voice(1).pitch == 82);
    sink.stop(sink.context, audio_ms(200));
    sink.advance(sink.context, audio_ms(205));
    for (int i = 0; i < SEQUENCER_VOICES; i++)
    {
        assert(!voice(i).active && levels[i] == 0);
    }
    assert(stops > 0);
    uint32_t old =
        sink.on(sink.context, audio_ms(1000), 48,
                &(SoundSettings){0, 0, WAVE_SINE, WAVE_SINE, 0, 0, 0, 200, 0});
    sink.advance(sink.context, audio_ms(1000));
    sink.off(sink.context, audio_ms(1000), old);
    uint32_t fresh =
        sink.on(sink.context, audio_ms(1000), 60,
                &(SoundSettings){0, 0, WAVE_SINE, WAVE_SINE, 0, 0, 0, 200, 0});
    assert((fresh & 31) == (old & 31) && fresh != old);
    sink.off(sink.context, audio_ms(1000), old);
    sink.advance(sink.context, audio_ms(1000));
    assert(voice(fresh & 31).active &&
           levels[fresh & 31] == AUDIO_LEVEL);
    sink.stop(sink.context, audio_ms(1001));
    sink.advance(sink.context, audio_ms(1006));
    uint32_t long_note = sink.on(
        sink.context, 0, 48,
        &(SoundSettings){16000, 16000, WAVE_SINE, WAVE_SINE, 0, 0, 0, 200, 0});
    sink.advance(sink.context, audio_ms(8000));
    assert(levels[0] >= AUDIO_LEVEL / 2 - 1 &&
           levels[0] <= (AUDIO_LEVEL + 1) / 2);
    sink.off(sink.context, audio_ms(8000), long_note);
    sink.advance(sink.context, audio_ms(24000));
    assert(!voice(0).active && !levels[0]);
    AudioTime release_start = audio_ms(25000);
    uint32_t full_release = sink.on(
        sink.context, release_start, 48,
        &(SoundSettings){0, 16000, WAVE_SINE, WAVE_SINE, 0, 0, 0, 200, 0});
    sink.advance(sink.context, release_start);
    assert(levels[0] == AUDIO_LEVEL);
    sink.off(sink.context, release_start, full_release);
    sink.advance(sink.context, release_start + audio_ms(8000));
    assert(levels[0] >= AUDIO_LEVEL / 2 - 1 &&
           levels[0] <= (AUDIO_LEVEL + 1) / 2 + 1);
    sink.advance(sink.context, release_start + audio_ms(16000));
    assert(!voice(0).active && !levels[0]);
    // Sequencer gate-offs carry the release captured at note-on.
    base(2);
    score.sounds[0] =
        (SoundSettings){0, 500, WAVE_SINE, WAVE_SINE, 0, 0, 0, 200, 0};
    put(1, 0, test_score_default(TILE_NOTE));
    put(2, 0, relative(0, -500));
    snapshot = score;
    sequencer_start(&seq, &snapshot, sink, 0);
    sequencer_service(&seq, 0);
    sequencer_service(&seq, SEQUENCER_HZ / 8);
    assert(voice(0).end == SEQUENCER_HZ / 8 + audio_ms(500));
    sequencer_stop(&seq, SEQUENCER_HZ / 8);
    sequencer_service(&seq, SEQUENCER_HZ / 8 + audio_ms(5));
    assert(!voice(0).active);
}

/*
 * A full chord with tied starts must steal each old pair in slot order.
 */
static void uniform_steals(void)
{
    audio = audio_init(NULL, driver_start, driver_volume, driver_pitch,
                       driver_flush, NULL, NULL);
    NoteSink sink = audio_sink(audio);
    SoundSettings sound = SOUND_DEFAULT;
    uint32_t old[SEQUENCER_VOICES];
    for (int i = 0; i < SEQUENCER_VOICES; i++)
    {
        old[i] = sink.on(sink.context, 0, 48 + i, &sound);
        assert((old[i] & 31) == (uint32_t)i);
    }
    sink.advance(sink.context, 0);
    AudioTime now = audio_ms(100);
    for (int i = 0; i < SEQUENCER_VOICES; i++)
    {
        uint32_t token = sink.on(sink.context, now, 60 + i, &sound);
        assert((token & 31) == (uint32_t)i);
    }
    assert(audio_steals(audio) == SEQUENCER_VOICES);
    for (int i = 0; i < SEQUENCER_VOICES; i++)
    {
        sink.off(sink.context, now, old[i]);
        assert(voice(i).active && !voice(i).releasing &&
               voice(i).pitch == 60 + i);
    }
}

/*
 * A delayed hardware dispatch must retire its voice before key-on and remove
 * the rejected slot from the wet-send mask passed to the register driver.
 */
static void late_dispatch(void)
{
    audio = audio_init(NULL, driver_start, driver_volume, driver_pitch,
                       driver_flush, NULL, driver_clock);
    SoundSettings sound =
        {0, 100, WAVE_SINE, WAVE_SINE, 0, 0, 0, 200, 1};
    dispatch_time = SEQUENCER_HZ / 1000 + 1;
    NoteSink sink = audio_sink(audio);
    sink.on(sink.context, 0, 48, &sound);
    sink.advance(sink.context, 0);
    assert(!last_starts && last_stops == 1 && !last_wet);
    assert(!voice(0).active && audio_idle_mask(audio) == AUDIO_IDLE_MASK);
    assert(audio_late_starts(audio) == 1);
}

/*
 * Exercises service limits under dense input so missed work does not become a
 * late note burst.
 */
static void overload(void)
{
    base(1);
    put(1, 0, test_score_default(TILE_NOTE));
    start();
    AudioTime now = 100 * SEQUENCER_HZ / 8;
    sequencer_service(&seq, now);
    assert(seq.overloads == 1 && count == 1 && seq.skipped == 2);
    for (int i = 0; i < 49; i++) sequencer_service(&seq, now);
    assert(seq.runners[0].lap == 101 && count == 2 && events[1].at == now &&
           seq.skipped == 99);
    sequencer_stop(&seq, now);
    sequencer_service(&seq, now + SEQUENCER_HZ);
    assert(count == 2);
}

static void model_editor(void)
{
    base(4);
    TileValue v = relative(-16000, 16000);
    put(1, 0, v);
    TileId id = test_score_at(&score, 1, 0).tile;
    Clipboard clip = {0};
    score_copy(&score, 1, 0, &clip);
    assert(!score_paste(&score, 2, 0, &clip));
    v.attack = 42;
    assert(!test_score_edit(&score, id, v));
    assert(score.tiles[test_score_at(&score, 2, 0).tile].value.attack == -16000);
    assert(!test_score_apply_move(&score, test_score_plan_move(&score, 1, 0, 3, 0)));
    assert(test_score_at(&score, 3, 0).tile == id);
    snapshot = score;
    v.attack = 16001;
    assert(test_score_edit(&score, id, v) == SCORE_INVALID &&
           !memcmp(&score, &snapshot, sizeof(score)));
    v = relative(1, 2);
    v.lock_mask = 0;
    assert(test_score_edit(&score, id, v) == SCORE_INVALID);
    assert(test_score_set_sound(&score, 0,
                           (SoundSettings){-1, 0, WAVE_SINE, WAVE_SINE, 0, 0, 0,
                                           200, 0}) == SCORE_INVALID);
    assert(test_score_set_sound(&score, 0,
                           (SoundSettings){0, 16001, WAVE_SINE, WAVE_SINE, 0, 0,
                                           0, 200, 0}) == SCORE_INVALID);
    editor_init(&editor);
    assert(!score_create(&editor.score, 0, 0, 2));
    assert(!score_place(&editor.score, 1, 0, TILE_RELATIVE));
    editor.x = 1;
    editor.y = 0;
    editor.target = test_score_at(&editor.score, 1, 0).tile;
    editor.mode = EDIT_MENU;
    editor.selected = 0;
    test_editor_update(&editor, (InputFrame){.connected = 1, .value_dir = 1});
    assert(editor.score.tiles[editor.target].value.lock_mask == LOCK_ATTACK);
    editor.selected = 1;
    test_editor_update(
        &editor,
        (InputFrame){.connected = 1, .value_dir = 1, .value_coarse = 1});
    test_editor_update(&editor, (InputFrame){.connected = 1, .value_dir = 1});
    assert(editor.score.tiles[editor.target].value.attack == 101);
    editor.selected = 0;
    test_editor_update(&editor, (InputFrame){.connected = 1, .value_dir = -1});
    assert(!editor.score.tiles[editor.target].value.lock_mask &&
           !editor.score.tiles[editor.target].value.attack);
    for (int mode = 0; mode < EDIT_MODE_COUNT; mode++)
    {
        editor.mode = mode;
        editor.gesture = 0;
        editor.value = relative(33, 44);
        snapshot = editor.score;
        test_editor_update(&editor, (InputFrame){.connected = 1, .start = 1});
        assert(!memcmp(&editor.score, &snapshot, sizeof(snapshot)));
    }
    editor.mode = EDIT_SOUND;
    editor.selected = 4;
    editor.sound_channel = 0;
    test_editor_update(
        &editor,
        (InputFrame){.connected = 1, .value_dir = 1, .value_coarse = 1});
    assert(editor.score.sounds[0].attack == 105);
    editor.selected = 5;
    test_editor_update(
        &editor,
        (InputFrame){.connected = 1, .value_dir = 1, .value_coarse = 1});
    test_editor_update(&editor, (InputFrame){.connected = 1, .circle = 1});
    assert(editor.score.sounds[0].release == 105);
    Input input;
    input_init(&input);
    test_input_update(&input, 1, 0);
    assert(test_input_update(&input, 1, INPUT_START).start);
    for (int i = 0; i < 90; i++)
    {
        assert(!test_input_update(&input, 1, INPUT_START).start);
    }
    assert(!test_input_update(&input, 0, 0).start);
    assert(!test_input_update(&input, 1, INPUT_START).start);
    test_input_update(&input, 1, 0);
    assert(test_input_update(&input, 1, INPUT_START).start);
}

/*
 * Uses a dense score to check bounded traversal and rejection without score
 * mutation.
 */
static void dense_and_rollback(void)
{
    base(64);
    TileId id = 1;
    for (int j = 0; j < 64; j++)
    {
        score.lanes[0].tiles[j] = id;
        for (int k = 0; k < 64; k++, id++)
        {
            score.tiles[id] =
                (Tile){relative(1, -1), k == 63 ? 0 : (TileId)(id + 1), -1};
        }
    }
    Clipboard clip = {.count = 1, .values = {0}};
    clip.values[0] = relative(12, 34);
    assert(!score_remove(&score, 64, 63));
    clip.count = 2;
    clip.values[1] = relative(56, 78);
    snapshot = score;
    assert(score_paste(&score, 65, 0, &clip) == SCORE_BOUNDS);
    assert(!memcmp(&score, &snapshot, sizeof(score)));
    // The first insert consumes the last pool slot; the second must roll back.
    score.lanes[0].length =
        63; // Restore a valid score by erasing the final stack.
    for (int k = 0; k < 64; k++)
    {
        memset(&score.tiles[4033 + k], 0, sizeof(Tile));
    }
    score.lanes[0].tiles[63] = 0;
    for (int k = 0; k < 63; k++)
    {
        score.tiles[4033 + k] =
            (Tile){relative(0, 0), k == 62 ? 0 : (TileId)(4034 + k), -1};
    }
    // Deliberately bypass persistence admission for the full-pool rollback
    // stress case; normal editor scores cannot reach this density.
    score.lanes[1] =
        (Lane){.active = 1, .x = 66, .y = 0, .length = 1, .division = 16};
    score.lane_generation[1] = ++score.generation;
    score.lanes[1].tiles[0] = 4033;
    snapshot = score;
    assert(score_paste(&score, 64, 0, &clip) == SCORE_FULL);
    assert(!memcmp(&score, &snapshot, sizeof(score)));
    // Splitting a stack and later replaying its held locks must preserve order.
    base(2);
    for (int k = 0; k < 63; k++) put(1, k, relative(1, 1));
    put(1, 63, test_score_default(TILE_NOTE));
    assert(!score_create(&score, 8, 0, 1));
    put(9, 0, test_score_default(TILE_NOTE));
    assert(!score_set_division(&score, 1, 32));
    snapshot = score;
    sequencer_start(&seq, &snapshot, trace, 0);
    sequencer_service(&seq, 0);
    assert(seq.slicing && count == 0);
    sequencer_service(&seq, 0);
    assert(seq.slicing && count == 1 && events[0].sound.attack == 68);
    sequencer_service(&seq, 0);
    assert(!seq.slicing && count == 2 && events[1].sound.release == 68);
    for (int i = 0; i < 3; i++) sequencer_service(&seq, SEQUENCER_HZ / 16);
    assert(count == 3 && events[2].sound.attack == 68);
    for (int i = 0; i < 3; i++) sequencer_service(&seq, SEQUENCER_HZ / 8);
    assert(events[count - 1].sound.attack == 5);
}

static void publish(AudioTime now)
{
    Score* spare = seq.score == &snapshot ? &updated : &snapshot;
    *spare = score;
    assert(sequencer_resync(&seq, spare, now));
}

static Runner* runner(int origin)
{
    for (int i = 0; i < SCORE_LANES; i++)
    {
        if (seq.runners[i].active && seq.runners[i].origin == origin)
        {
            return &seq.runners[i];
        }
    }
    return NULL;
}

/*
 * Publishes edits while playing and checks that a runner reads the new values
 * at a safe boundary.
 */
static void live_values(void)
{
    AudioTime step = SEQUENCER_HZ / 8;
    base(2);
    TileValue v = test_score_default(TILE_NOTE);
    v.length = 40;
    put(1, 0, v);
    put(2, 0, v);
    TileValue gate = test_score_default(TILE_PROBABILITY);
    gate.chance = 100;
    put(1, 1, gate);
    start();
    sequencer_service(&seq, step);
    sequencer_service(&seq, 2 * step);
    Runner before = *runner(0);
    uint32_t random = seq.random;
    NoteOff saved[SEQUENCER_VOICES];
    memcpy(saved, seq.offs, sizeof(saved));
    v.pitch = 72;
    v.length = 5;
    assert(!test_score_edit(&score, test_score_at(&score, 2, 0).tile, v));
    assert(!test_score_set_sound(
        &score, 0,
        (SoundSettings){23, 41, WAVE_SINE, WAVE_SINE, 0, 0, 0, 200, 0}));
    assert(!score_set_division(&score, 0, 32));
    publish(2 * step + 1);
    assert(!memcmp(runner(0), &before, sizeof(before)) && seq.random == random);
    assert(!memcmp(saved, seq.offs, sizeof(saved)));
    sequencer_service(&seq, 3 * step);
    assert(count == 4 && events[3].at == 3 * step && events[3].pitch == 72);
    assert(events[3].sound.attack == 23 && events[3].sound.release == 41);
    assert(runner(0)->next == 3 * step + step / 2 && runner(0)->lap == 2);
    sequencer_service(&seq, 3 * step + step / 8);
    assert(off_count == 3 && offs[2] == 3 * step + step / 8);
    sequencer_service(&seq, 3 * step + step / 2);
    assert(count == 5 && events[4].pitch == 48 && seq.random != random);
    // A previously scheduled two-step gate retains its original deadline.
    sequencer_service(&seq, 4 * step);
    assert(off_count >= 4 && offs[3] == 4 * step);
}

/*
 * Checks held locks after their tile values or birth generations change.
 */
static void live_holds(void)
{
    AudioTime step = SEQUENCER_HZ / 8;
    base(2);
    assert(!score_set_division(&score, 0, 4));
    put(1, 0, relative(100, 200));
    assert(!score_create(&score, 8, 4, 1));
    put(9, 4, test_score_default(TILE_NOTE));
    TileId held = test_score_at(&score, 1, 0).tile;
    start();
    assert(events[0].sound.attack == 105);
    assert(!test_score_edit(&score, held, relative(200, 300)));
    publish(1);
    sequencer_service(&seq, step);
    assert(events[1].sound.attack == 205);
    assert(!test_score_apply_move(&score, test_score_plan_move(&score, 1, 0, 2, 0)));
    publish(step + 1);
    sequencer_service(&seq, 2 * step);
    assert(events[2].sound.attack == 205);
    // Reusing a held slot in an unvisited step must not inherit engagement.
    uint32_t birth = score.tile_generation[held];
    assert(!score_remove(&score, 2, 0));
    put(2, 0, relative(900, 900));
    assert(test_score_at(&score, 2, 0).tile == held &&
           score.tile_generation[held] != birth);
    publish(2 * step + 1);
    sequencer_service(&seq, 3 * step);
    assert(events[3].sound.attack == 5);
    sequencer_service(&seq, 4 * step);
    assert(events[4].sound.attack == 905);
    // Head movement changes lock traversal order without resetting runners.
    Runner before = *runner(0);
    assert(!test_score_apply_move(&score, test_score_plan_move(&score, 0, 0, 0, 8)));
    publish(4 * step + 1);
    assert(!memcmp(&before, runner(0), sizeof(before)) &&
           seq.runners[seq.order[0]].origin == 1);
    sequencer_service(&seq, 5 * step);
    assert(events[5].sound.attack == 5);
}

/*
 * Checks runner ownership when root or branch lanes are edited during
 * playback.
 */
static void live_lanes(void)
{
    AudioTime step = SEQUENCER_HZ / 8;
    base(4);
    put(1, 0, test_score_default(TILE_NOTE));
    start();
    sequencer_service(&seq, step);
    sequencer_service(&seq, 2 * step);
    assert(runner(0)->step == 3 && runner(0)->playing_step == 2);
    assert(!score_resize(&score, 0, 2));
    publish(2 * step + 1);
    assert(runner(0)->step == 0 && runner(0)->playing_step == -1 &&
           !runner(0)->lap);
    assert(runner(0)->next == 3 * step);
    sequencer_service(&seq, 3 * step);
    assert(count == 2 && events[1].at == 3 * step);
    // A new non-master waits for the current master's next full lap.
    assert(!score_create(&score, 0, 6, 1));
    put(1, 6, test_score_default(TILE_NOTE));
    publish(3 * step + 1);
    assert(runner(1)->next == UINT64_MAX);
    sequencer_service(&seq, 4 * step);
    assert(runner(1)->next == 5 * step);
    sequencer_service(&seq, 5 * step);
    assert(count == 4 && runner(1)->lap == 1);
    // Skipped deletion/recreation revisions still create a fresh runner.
    uint32_t birth = score.lane_generation[1];
    assert(!score_delete(&score, 1));
    assert(!score_create(&score, 0, 6, 1));
    put(1, 6, test_score_default(TILE_NOTE));
    assert(score.lane_generation[1] != birth);
    publish(5 * step + 1);
    assert(runner(1)->next == UINT64_MAX && !runner(1)->lap);
    // Removing the master promotes a pending lane immediately.
    assert(!score_delete(&score, 0));
    publish(5 * step + 2);
    assert(seq.count == 1 && !runner(0) && runner(1)->next == 5 * step + 2);
    sequencer_service(&seq, 5 * step + 2);
    assert(count == 5);
    assert(!score_delete(&score, 1));
    publish(5 * step + 3);
    assert(!seq.count && seq.playing);
    sequencer_service(&seq, 6 * step);
    assert(count == 5);
    assert(!score_create(&score, 0, 0, 1));
    put(1, 0, test_score_default(TILE_NOTE));
    publish(6 * step + 1);
    sequencer_service(&seq, 6 * step + 1);
    assert(count == 6 && runner(0)->lap == 1);
    // A newly inserted head above the master starts without waiting on itself.
    assert(!test_score_apply_move(&score, test_score_plan_move(&score, 0, 0, 0, 8)));
    publish(6 * step + 2);
    assert(!score_create(&score, 0, 0, 1));
    put(1, 0, test_score_default(TILE_NOTE));
    publish(6 * step + 3);
    assert(seq.runners[seq.order[0]].origin == 1 &&
           runner(1)->next == 6 * step + 3);
    sequencer_service(&seq, 6 * step + 3);
    assert(count == 7 && runner(0)->next == 7 * step + 1);
}

/*
 * Exercises edits at the final step where lap and publication boundaries meet.
 */
static void live_final_step(void)
{
    AudioTime step = SEQUENCER_HZ / 8;
    base(2);
    put(1, 0, test_score_default(TILE_NOTE));
    start();
    sequencer_service(&seq, step);
    assert(runner(0)->lap == 1);
    assert(!score_create(&score, 0, 4, 1));
    put(1, 4, test_score_default(TILE_NOTE));
    publish(step + 1);
    // The final step has already selected the next lap's participants.
    sequencer_service(&seq, 2 * step);
    assert(count == 2 && runner(1)->next == UINT64_MAX);
    sequencer_service(&seq, 3 * step);
    assert(runner(1)->next == 4 * step);
    sequencer_service(&seq, 4 * step);
    assert(count == 4 && events[3].at == 4 * step);
}

/*
 * Checks pending gate-offs across live edits and slot reuse.
 */
static void live_gates(void)
{
    AudioTime step = SEQUENCER_HZ / 8;
    base(1);
    TileValue v = test_score_default(TILE_CYCLE);
    v.period = 2;
    v.pattern = 1;
    put(1, 0, v);
    put(1, 1, test_score_default(TILE_NOTE));
    start();
    assert(count == 1 && runner(0)->lap == 1);
    v.period = 3;
    v.pattern = 2;
    assert(!test_score_edit(&score, test_score_at(&score, 1, 0).tile, v));
    publish(1);
    sequencer_service(&seq, step);
    assert(count == 2 && runner(0)->lap == 2);
    sequencer_service(&seq, 2 * step);
    assert(count == 2 && runner(0)->lap == 3);
    base(1);
    v = test_score_default(TILE_PROBABILITY);
    v.chance = 0;
    put(1, 0, v);
    put(1, 1, test_score_default(TILE_NOTE));
    start();
    uint32_t random = seq.random;
    v.chance = 100;
    assert(!test_score_edit(&score, test_score_at(&score, 1, 0).tile, v));
    publish(1);
    assert(seq.random == random);
    sequencer_service(&seq, step);
    random ^= random << 13;
    random ^= random >> 17;
    random ^= random << 5;
    assert(count == 1 && events[0].at == step && seq.random == random);
}

/*
 * Checks traversal after a jump source or its branch changes in a published
 * score.
 */
static void live_branch(void)
{
    AudioTime step = SEQUENCER_HZ / 8;
    base(2);
    put(1, 0, test_score_default(TILE_JUMP));
    int branch = score.tiles[test_score_at(&score, 1, 0).tile].branch;
    Lane b = score.lanes[branch];
    put(b.x + 1, b.y, relative(100, 100));
    start();
    sequencer_service(&seq, step);
    assert(runner(0)->lane == branch && runner(0)->step == 1 &&
           runner(0)->held_count == 1);
    assert(!score_delete(&score, branch));
    publish(step + 1);
    assert(runner(0)->lane == 0 && runner(0)->step == 0 &&
           runner(0)->playing_step == -1);
    assert(!runner(0)->held_count && !runner(0)->lap &&
           runner(0)->next == 2 * step);
    sequencer_service(&seq, 2 * step);
    assert(runner(0)->playing_lane == 0 && runner(0)->playing_step == 0);
}

/*
 * Exercises publication while a dense slice spans multiple service calls.
 */
static void live_split(void)
{
    base(1);
    for (int i = 0; i < 63; i++) put(1, i, relative(1, 1));
    put(1, 63, test_score_default(TILE_NOTE));
    snapshot = score;
    sequencer_start(&seq, &snapshot, trace, 0);
    sequencer_service(&seq, 0);
    assert(seq.slicing && !count);
    TileValue v = test_score_default(TILE_NOTE);
    v.pitch = 72;
    assert(!test_score_edit(&score, test_score_at(&score, 1, 63).tile, v));
    updated = score;
    Sequencer before = seq;
    assert(!sequencer_resync(&seq, &updated, 0) &&
           !memcmp(&seq, &before, sizeof(seq)));
    sequencer_service(&seq, 0);
    assert(!seq.slicing && count == 1 && events[0].pitch == 48);
    assert(sequencer_resync(&seq, &updated, 0));
    sequencer_service(&seq, SEQUENCER_HZ / 8);
    assert(seq.slicing);
    sequencer_service(&seq, SEQUENCER_HZ / 8);
    assert(!seq.slicing && count == 2 && events[1].pitch == 72 &&
           events[1].sound.attack == 68);
}

/*
 * Checks sound selection and lock scope when multiple channels play together.
 */
static void channels(void)
{
    AudioTime step = SEQUENCER_HZ / 8;
    base(2);
    assert(!score_set_division(&score, 0, 4));
    put(1, 0, relative(100, 200));
    for (int i = 1; i < 3; i++)
    {
        assert(!score_create(&score, 0, 4 * i, 1));
        put(1, 4 * i, test_score_default(TILE_NOTE));
    }
    SoundSettings second = {20, 30, WAVE_SQUARE, WAVE_NOISE, 12, 34, -5, 67, 1};
    assert(!test_score_set_sound(&score, 7, second));
    assert(!score_set_channel(&score, 2, 7));
    start();
    assert(count == 2 && events[0].sound.attack == 105);
    assert(!memcmp(&events[1].sound, &second, sizeof(second)));
    sequencer_service(&seq, step);
    assert(count == 4 && events[2].sound.attack == 105);
    assert(!memcmp(&events[3].sound, &second, sizeof(second)));
    Runner saved = *runner(0);
    NoteOff gates[SEQUENCER_VOICES];
    memcpy(gates, seq.offs, sizeof(gates));
    assert(!score_set_channel(&score, 0, 7));
    publish(step + 1);
    assert(!memcmp(&saved, runner(0), sizeof(saved)) &&
           !memcmp(gates, seq.offs, sizeof(gates)));
    sequencer_service(&seq, 2 * step);
    assert(events[4].sound.attack == 5 && events[5].sound.attack == 120 &&
           events[5].sound.release == 230);
    assert(events[5].sound.wave_a == WAVE_SQUARE && events[5].sound.reverb);
    assert(!memcmp(&score.sounds[7], &second, sizeof(second)));
    sequencer_service(&seq, 4 * step);
    assert(events[count - 1].sound.attack == 20);

    base(1);
    assert(!score_set_channel(&score, 0, 7));
    assert(!test_score_set_sound(&score, 7, second));
    put(1, 0, test_score_default(TILE_JUMP));
    int branch = score.tiles[test_score_at(&score, 1, 0).tile].branch;
    Lane b = score.lanes[branch];
    assert(!score_resize(&score, branch, 1));
    put(b.x + 1, b.y, test_score_default(TILE_JUMP));
    int child = score.tiles[test_score_at(&score, b.x + 1, b.y).tile].branch;
    b = score.lanes[child];
    put(b.x + 1, b.y, relative(100, 200));
    put(b.x + 1, b.y + 1, test_score_default(TILE_NOTE));
    start();
    sequencer_service(&seq, step);
    sequencer_service(&seq, 2 * step);
    assert(count == 1 && events[0].sound.attack == 120 &&
           events[0].sound.wave_a == WAVE_SQUARE);

    // A budget boundary must retain every working channel, and publication must
    // wait until even the later runners have consumed the old bank.
    base(1);
    for (int i = 0; i < 63; i++) put(1, i, relative(1, 1));
    put(1, 63, test_score_default(TILE_NOTE));
    assert(!score_create(&score, 8, 0, 1));
    put(9, 0, test_score_default(TILE_NOTE));
    assert(!score_create(&score, 16, 0, 1));
    put(17, 0, test_score_default(TILE_NOTE));
    assert(!score_set_channel(&score, 2, 7));
    assert(!test_score_set_sound(&score, 7, second));
    snapshot = score;
    sequencer_start(&seq, &snapshot, trace, 0);
    sequencer_service(&seq, 0);
    assert(seq.slicing && count == 0);
    assert(!score_set_channel(&score, 0, 7));
    second.attack = 200;
    assert(!test_score_set_sound(&score, 7, second));
    updated = score;
    Sequencer before = seq;
    assert(!sequencer_resync(&seq, &updated, 0) &&
           !memcmp(&before, &seq, sizeof(seq)));
    sequencer_service(&seq, 0);
    sequencer_service(&seq, 0);
    assert(!seq.slicing && count == 3 && events[0].sound.attack == 68 &&
           events[1].sound.attack == 68 && events[2].sound.attack == 20);
    assert(sequencer_resync(&seq, &updated, 1));
    for (int i = 0; i < 3; i++) sequencer_service(&seq, step);
    assert(count == 6 && events[3].sound.attack == 263 &&
           events[4].sound.attack == 5 && events[5].sound.attack == 263);

    score_init(&score);
    count = off_count = slot = 0;
    for (int i = 0; i < SCORE_LANES; i++)
    {
        assert(!score_create(&score, 0, 3 * i, 1));
        assert(!score_set_channel(&score, i, i % SCORE_CHANNELS));
        SoundSettings sound = SOUND_DEFAULT;
        sound.attack = 10 + i % SCORE_CHANNELS;
        assert(!test_score_set_sound(&score, i % SCORE_CHANNELS, sound));
        put(1, 3 * i, test_score_default(TILE_NOTE));
    }
    start();
    assert(seq.count == SCORE_LANES && count == SCORE_LANES);
    for (int i = 0; i < SCORE_LANES; i++)
    {
        assert(events[i].sound.attack == 10 + i % SCORE_CHANNELS);
    }
    puts("PASS: channel isolation, shared held locks, reassignment continuity, "
         "nested branches, "
         "split publication and 16 lanes");
}

/*
 * Checks that a tempo edit affects future steps without moving an already
 * scheduled gate.
 */
static void tempo_changes(void)
{
    for (int bpm = SCORE_MIN_BPM; bpm <= SCORE_MAX_BPM; bpm++)
    {
        base(1);
        put(1, 0, test_score_default(TILE_NOTE));
        assert(!score_set_bpm(&score, bpm));
        start();
        AudioTime duration = 240u * SEQUENCER_HZ / bpm / 16;
        sequencer_service(&seq, duration - 1);
        assert(count == 1);
        sequencer_service(&seq, duration);
        assert(count == 2 && events[1].at == duration);
    }
    base(1);
    put(1, 0, test_score_default(TILE_NOTE));
    start();
    AudioTime old = SEQUENCER_HZ / 8;
    assert(!score_set_bpm(&score, 240));
    updated = score;
    assert(sequencer_resync(&seq, &updated, old / 2));
    sequencer_service(&seq, old - 1);
    assert(count == 1 && off_count == 0);
    sequencer_service(&seq, old);
    assert(count == 2 && offs[0] == old);
    sequencer_service(&seq, old + old / 2);
    assert(count == 3 && offs[1] == old + old / 2);
}

int main(void)
{
    channels();
    tempo_changes();
    timing();
    locks();
    gates_branches();
    voices();
    uniform_steals();
    late_dispatch();
    overload();
    model_editor();
    dense_and_rollback();
    live_values();
    live_holds();
    live_lanes();
    live_final_step();
    live_gates();
    live_branch();
    live_split();
    printf("PASS: sequencer timing, locks, gates, branches, voices, overload, "
           "START and live "
           "resync; Score %zu, Sequencer %zu, Audio %zu bytes\n",
           sizeof(Score), sizeof(Sequencer), audio_storage_size());
}
