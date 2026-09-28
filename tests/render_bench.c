/*
 * render_bench.c - Console rendering work benchmark
 *
 * Implementation notes:
 *
 * Fixed stopped scores keep input, audio planning, and editor transactions
 * out of the timed frame. The renderer's own pre-wait clock gives the same
 * main-thread work interval displayed by the Debug performance monitor.
 */

#include "audio_platform.h"
#include "editor.h"
#include "render_backend.h"
#include "ui_render.h"

#include <psxgpu.h>
#include <stdio.h>

#define WARMUP_FRAMES 16
#define SAMPLE_FRAMES 64

static Editor editor;
static uint32_t samples[SAMPLE_FRAMES];

static void log_line(const char* line)
{
    *(const char* volatile*)0x1f802084 = line;
}

static void sort_samples(void)
{
    for (int i = 1; i < SAMPLE_FRAMES; i++)
    {
        uint32_t value = samples[i];
        int j = i;
        while (j > 0 && samples[j - 1] > value)
        {
            samples[j] = samples[j - 1];
            j--;
        }
        samples[j] = value;
    }
}

/*
 * Matches the visible 12-column score: four note rows throughout and four
 * more rows under the first five columns, for 68 labeled note tiles.
 */
static void screenshot_score(void)
{
    editor_init(&editor);
    Lane* lane = &editor.score.lanes[0];
    *lane = (Lane){.active = 1, .x = 1, .y = 1, .length = 12,
                   .division = 16, .channel = 0};
    TileId id = 1;
    for (int step = 0; step < 12; step++)
    {
        lane->tiles[step] = id;
        int height = step < 5 ? 8 : 4;
        for (int depth = 0; depth < height; depth++, id++)
        {
            TileValue value = {.kind = TILE_NOTE,
                               .pitch = 48 + depth % 4 * 7};
            editor.score.tiles[id] =
                (Tile){value, depth + 1 == height ? 0 : (TileId)(id + 1), -1};
        }
    }
}

/*
 * Offscreen notes isolate the cost of the unconditional jump search from
 * tile sprites and labels. The pool contains 16 lanes * 4 steps * 64 tiles.
 */
static void offscreen_score(void)
{
    editor_init(&editor);
    // Force the jump cache to rebuild before its warmup frames even though
    // this fixture populates the score directly rather than through edits.
    editor.score.revision = 1;
    TileId id = 1;
    for (int i = 0; i < SCORE_LANES; i++)
    {
        Lane* lane = &editor.score.lanes[i];
        *lane = (Lane){.active = 1, .x = 24 + i * 6, .y = 30,
                       .length = 4, .division = 16, .channel = i % 8};
        for (int step = 0; step < 4; step++)
        {
            lane->tiles[step] = id;
            for (int depth = 0; depth < 64; depth++, id++)
            {
                TileValue value = {.kind = TILE_NOTE, .pitch = 48};
                editor.score.tiles[id] =
                    (Tile){value,
                           depth == 63 ? 0 : (TileId)(id + 1), -1};
            }
        }
    }
}

static void measure(const char* name)
{
    render_backend_monitor_reset();
    for (int i = 0; i < WARMUP_FRAMES + SAMPLE_FRAMES; i++)
    {
        render_frame(&editor, 1, NULL);
        if (i >= WARMUP_FRAMES)
            samples[i - WARMUP_FRAMES] = render_backend_monitor()->work_ticks;
    }
    sort_samples();
    char line[160];
    snprintf(line, sizeof(line),
             "RENDER %s min=%lu median=%lu p95=%lu max=%lu ticks\n",
             name, (unsigned long)samples[0],
             (unsigned long)samples[SAMPLE_FRAMES / 2],
             (unsigned long)samples[SAMPLE_FRAMES * 95 / 100],
             (unsigned long)samples[SAMPLE_FRAMES - 1]);
    log_line(line);
}

int main(void)
{
    editor_init(&editor);
    render_init();
    audio_platform_init();
    screenshot_score();
    measure("visible-68");
    offscreen_score();
    measure("offscreen-4096");
    log_line("RENDER BENCH COMPLETE\n");
    *(volatile short*)0x1f802082 = 0;
    for (;;) VSync(0);
}
