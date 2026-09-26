/*
 * input_fixture.c - Console controller input fixture
 *
 * Implementation notes:
 *
 * Lua drives actual emulated pad replies while this program records
 * normalized events through the device polling path.
 */

#include "value_api.h"

#include "audio/audio_platform.h"
#include "editor.h"
#include "input/pad.h"
#include "ui/render_backend.h"
#include "ui_render.h"

#include <psxgpu.h>
#include <stdio.h>

volatile unsigned input_fixture_phase;
volatile unsigned input_fixture_expected;
static Editor editor;
static Input input;
static unsigned moves;
static unsigned presses;
static unsigned releases;
static unsigned starts;
static unsigned selects;
static unsigned disconnected;

static void drain(void)
{
    InputSample sample;
    while (pad_read(&sample))
    {
        InputFrame f = test_input_update(&input, sample.connected, sample.held);
        moves += f.dx != 0;
        presses += f.cross;
        releases += f.cross_released;
        starts += f.start;
        selects += f.select;
        disconnected += !f.connected;
    }
}

/*
 * Separates emulator-driven button phases so each report can be correlated
 * with normalized input counters.
 */
static void run(unsigned phase)
{
    moves = presses = releases = starts = selects = disconnected = 0;
    unsigned polls = pad_polls;
    unsigned reports = pad_reports;
    unsigned timeouts = pad_timeouts;
    unsigned overflows = pad_overflows;
    input_fixture_expected = 0;
    input_fixture_phase = phase;
    for (int i = 0; i < (phase == 5 ? 120 : 480); i++)
    {
        editor.x = i % 64;
        render_frame(&editor, 1);
        // A slow main loop must retain complete taps, not just the latest held
        // state. The interrupt still collects one report per video frame.
        if (phase != 3 || i % 8 == 7) drain();
    }
    drain();
    char line[256];
    snprintf(line, sizeof(line),
             "INPUT phase=%u id=%u polls=%u reports=%u timeouts=%u "
             "overflows=%u expected=%u "
             "moves=%u presses=%u releases=%u starts=%u selects=%u "
             "disconnected=%u\n",
             phase, pad_id, pad_polls - polls, pad_reports - reports,
             pad_timeouts - timeouts, pad_overflows - overflows,
             input_fixture_expected, moves, presses, releases, starts, selects,
             disconnected);
    *(const char* volatile*)0x1f802084 = line;
}

int main(void)
{
    editor_init(&editor);
    input_init(&input);
    render_init();
    audio_platform_init();
    pad_init();
    for (int i = 0; i < 8; i++)
    {
        render_frame(&editor, 1);
        drain();
    }
    run(1);
    score_create(&editor.score, 0, 0, 16);
    for (int i = 0; i < SEQUENCER_VOICES; i++)
    {
        TileValue v = test_score_default(TILE_NOTE);
        v.pitch = 36 + i;
        v.length = 1280;
        test_score_place_value(&editor.score, 1, i, v);
    }
    audio_platform_update(&editor.score, 1, 1);
    run(2);
    run(3);
    run(4);
    audio_platform_update(&editor.score, 0, 0);
    score_init(&editor.score);
    // Deliberate input/audio overload fixture, outside the saveable score
    // limit.
    TileId id = 1;
    for (int i = 0; i < 16; i++)
    {
        Lane* lane = &editor.score.lanes[i];
        *lane = (Lane){
            .active = 1, .x = i * 6, .y = 0, .length = 4, .division = 64};
        for (int j = 0; j < 4; j++)
        {
            lane->tiles[j] = id;
            for (int k = 0; k < 64; k++, id++)
            {
                editor.score.tiles[id] =
                    (Tile){test_score_default(TILE_NOTE),
                           k == 63 ? 0 : (TileId)(id + 1), -1};
            }
        }
    }
    audio_platform_update(&editor.score, 1, 1);
    run(5);
    audio_platform_update(&editor.score, 0, 0);
    *(const char* volatile*)0x1f802084 = "INPUT FIXTURE COMPLETE\n";
    *(volatile short*)0x1f802082 = 0;
    for (;;) VSync(0);
}
