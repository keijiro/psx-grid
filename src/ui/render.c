/*
 * render.c - PlayStation GPU packet and video backend
 *
 * Implementation notes:
 *
 * Rust decides geometry and clipping. This file owns SDK packet construction,
 * fixed double buffers, and submission order so SDK DMA types stay in C.
 */

#include "ui/render_backend.h"

#include <psxetc.h>
#include <psxgpu.h>
#include <stddef.h>

#include "ui_atlas.h"
#include "ui/ui_style.h"

// A full 20-by-15 view can contain 300 labeled tiles and their rail dots. Both
// buffers still fit in main RAM at the measured 64 KiB packet budget.
#define PACKET_BYTES 65536
#define OT_SIZE 8

/*
 * OT traverses high depths first and reverses insertion within each bucket.
 * 7: texture setup/lattice, 6: rails, 5: tiles, 4: endpoint, 3: cursor,
 * 2: panels and shadows, 1: underlines, 0: text.
 */
typedef struct
{
    DRAWENV draw;
    DISPENV disp;
    uint32_t ot[OT_SIZE];
    uint32_t packets[PACKET_BYTES / 4];
} Buffer;

/*
 * One command buffer is submitted while the other remains visible. used
 * counts packet bytes only in the buffer under construction.
 */
static Buffer buffers[2];
static int active;
static size_t used;
// Debugger-visible counters; overflow is rejected before touching memory.
volatile unsigned render_packet_peak;
volatile unsigned render_overflows;

/*
 * Rejects a primitive before touching either packet buffer when the fixed
 * command budget is exhausted.
 */
static void* packet(size_t bytes)
{
    if (bytes > PACKET_BYTES - used)
    {
        render_overflows++;
        return NULL;
    }
    void* p = (uint8_t*)buffers[active].packets + used;
    used += bytes;
    if (used > render_packet_peak) render_packet_peak = used;
    return p;
}

void render_init(void)
{
    ResetGraph(0);
    SetVideoMode(MODE_NTSC);
    // 4-bit texels occupy 64x96 VRAM words at x=640. CLUT sits immediately
    // below; neither overlaps framebuffers (x=0..319, y=0..479), and UVs stay
    // inside one texture page. Index zero alone is transparent.
    RECT atlas = {640, 0, 64, 96};
    RECT clut = {640, 96, 16, 1};
    LoadImage(&atlas, ui_pixels);
    DrawSync(0);
    LoadImage(&clut, ui_clut);
    DrawSync(0);
    for (int i = 0; i < 2; i++)
    {
        SetDefDrawEnv(&buffers[i].draw, 0, i * SCREEN_H, SCREEN_W, SCREEN_H);
        SetDefDispEnv(&buffers[i].disp, 0, i * SCREEN_H, SCREEN_W, SCREEN_H);
        buffers[i].draw.isbg = 1;
        setRGB0(&buffers[i].draw, UI_BACKGROUND, UI_BACKGROUND, UI_BACKGROUND);
        ClearOTagR(buffers[i].ot, OT_SIZE);
    }
    SetDispMask(1);
}

/*
 * Clears only the buffer being constructed; the other may still be visible.
 */
void render_backend_begin(void)
{
    used = 0;
    ClearOTagR(buffers[active].ot, OT_SIZE);
}

/*
 * Accepts already clipped screen geometry from Rust and appends an SDK tile.
 */
void render_backend_tile(int depth, int x, int y, int w, int h, int gray)
{
    TILE* p = packet(sizeof(TILE));
    if (!p) return;
    setTile(p);
    setXY0(p, x, y);
    setWH(p, w, h);
    setRGB0(p, gray, gray, gray);
    addPrim(&buffers[active].ot[depth], p);
}

/*
 * Accepts already clipped screen and atlas geometry from Rust.
 */
void render_backend_sprite(int depth, int x, int y, int u, int v, int w, int h)
{
    SPRT* p = packet(sizeof(SPRT));
    if (!p) return;
    setSprt(p);
    setXY0(p, x, y);
    setUV0(p, u, v);
    setWH(p, w, h);
    setRGB0(p, 128, 128, 128);
    setClut(p, 640, 96);
    addPrim(&buffers[active].ot[depth], p);
}

/*
 * Inserts texture setup last so reversed OT traversal draws it first.
 */
void render_backend_present(void)
{
    DR_TPAGE* page = packet(sizeof(DR_TPAGE));
    if (page)
    {
        setDrawTPage(page, 0, 0, getTPage(0, 0, 640, 0));
        addPrim(&buffers[active].ot[7], page);
    }
    DrawSync(0);
    VSync(0);
    PutDispEnv(&buffers[active ^ 1].disp);
    DrawOTagEnv(&buffers[active].ot[OT_SIZE - 1], &buffers[active].draw);
    active ^= 1;
}
