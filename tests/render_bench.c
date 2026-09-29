/*
 * render_bench.c - Console rendering work benchmark
 *
 * Implementation notes:
 *
 * Fixed stopped scores keep input, audio planning, and editor transactions
 * out of the timed frame. Stationary drag cases isolate the preview render
 * path without controller-repeat timing. The renderer's own pre-wait clock
 * gives the main-thread interval shown by the Debug performance monitor.
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
 * Fills the entire 20-by-15 view with fifteen single-height note lanes so
 * drag previews exercise lane scans even after static packets are cached.
 */
static void dense_score(void)
{
    editor_init(&editor);
    editor.score.revision = 2;
    TileId id = 1;
    for (int row = 0; row < 15; row++)
    {
        Lane* lane = &editor.score.lanes[row];
        *lane = (Lane){.active = 1, .x = 0, .y = row, .length = 18,
                       .division = 16, .channel = row % SCORE_CHANNELS};
        for (int step = 0; step < 18; step++, id++)
        {
            lane->tiles[step] = id;
            editor.score.tiles[id] =
                (Tile){.value = {.kind = TILE_NOTE, .pitch = 48}};
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

/*
 * Advances between occupied steps without moving the camera, so every
 * sample includes admission for a newly chosen destination.
 */
static void measure_drag_steps(void)
{
    render_backend_monitor_reset();
    for (int i = 0; i < WARMUP_FRAMES + SAMPLE_FRAMES; i++)
    {
        editor.x = 2 + i % 16;
        render_frame(&editor, 1, NULL);
        if (i >= WARMUP_FRAMES)
            samples[i - WARMUP_FRAMES] = render_backend_monitor()->work_ticks;
    }
    sort_samples();
    char line[160];
    snprintf(line, sizeof(line),
             "RENDER dense-270-drag-moving min=%lu median=%lu p95=%lu "
             "max=%lu ticks\n",
             (unsigned long)samples[0],
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
    editor.mode = EDIT_MOVE;
    editor.source_x = 2;
    editor.source_y = 1;
    editor.x = 15;
    editor.y = 12;
    measure("visible-68-drag-invalid");
    editor.x = 3;
    editor.y = 1;
    measure("visible-68-drag-valid");
    editor.mode = EDIT_MAIN;
    measure("visible-68-menu");
    dense_score();
    measure("dense-270");
    editor.mode = EDIT_MOVE;
    editor.source_x = 1;
    editor.source_y = 0;
    editor.x = 0;
    editor.y = 0;
    measure("dense-270-drag-invalid");
    editor.x = 2;
    measure("dense-270-drag-valid");
    measure_drag_steps();
    offscreen_score();
    measure("offscreen-4096");
    log_line("RENDER BENCH COMPLETE\n");
    *(volatile short*)0x1f802082 = 0;
    for (;;) VSync(0);
}
