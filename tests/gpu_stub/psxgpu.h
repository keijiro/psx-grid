/*
 * psxgpu.h - GPU packet types and calls for host rendering tests
 *
 * Host tests supply fixed GPU state behind the SDK interface consumed by the
 * renderer. Type and function names match the console SDK so the renderer
 * builds without alternate call sites.
 */

#ifndef GPU_STUB_H
#define GPU_STUB_H

#include <stddef.h>
#include <stdint.h>

// Packet constructors mirror the SDK macros used by render.c.
#define setTile(p) ((p)->code = 0x60)
#define setSprt(p) ((p)->code = 0x64)
#define setXY0(p, x, y) ((p)->x0 = (x), (p)->y0 = (y))
#define setWH(p, width, height) ((p)->w = (width), (p)->h = (height))
#define setUV0(p, u, v) ((p)->u0 = (u), (p)->v0 = (v))
#define setRGB0(p, r, g, b) ((p)->r0 = (r), (p)->g0 = (g), (p)->b0 = (b))
#define getTPage(tp, abr, x, y)                                                \
    (((x) >> 6) | (((y) & 256) >> 4) | ((tp) << 7) | ((abr) << 5))
#define setClut(p, x, y) ((p)->clut = ((y) << 6) | ((x) >> 4))
#define setDrawTPage(p, dfe, dtd, page) ((p)->code = 0xe1000000 | (page))

// SDK-compatible filled rectangle packet inspected by the host renderer.
typedef struct
{
    uint32_t tag;
    uint8_t r0;
    uint8_t g0;
    uint8_t b0;
    uint8_t code;
    int16_t x0;
    int16_t y0;
    int16_t w;
    int16_t h;
} TILE;

// SDK-compatible textured sprite packet inspected by the host renderer.
typedef struct
{
    uint32_t tag;
    uint8_t r0;
    uint8_t g0;
    uint8_t b0;
    uint8_t code;
    int16_t x0;
    int16_t y0;
    uint8_t u0;
    uint8_t v0;
    uint16_t clut;
    int16_t w;
    int16_t h;
} SPRT;

// Texture-page command packet used to begin an ordering table.
typedef struct
{
    uint32_t tag;
    uint32_t code;
} DR_TPAGE;

// Rectangle describing a host texture upload region.
typedef struct
{
    int16_t x;
    int16_t y;
    int16_t w;
    int16_t h;
} RECT;

// Draw environment fields consumed by the rendering checks.
typedef struct
{
    int r0;
    int g0;
    int b0;
    int isbg;
} DRAWENV;

// Display environment placeholder for the host implementation.
typedef struct
{
    int unused;
} DISPENV;

/*
 * Links primitive `p` into ordering-table bucket `ot`.
 * `ot` and `p` must not be NULL.
 */
void addPrim(uint32_t* ot, void* p);
/*
 * Resets host GPU state before a rendering test.
 */
void ResetGraph(int mode);
/*
 * Copies texture data into the host VRAM region `rect`.
 * `rect` and `data` must not be NULL.
 */
int LoadImage(const RECT* rect, const uint32_t* data);
/*
 * Initializes `p` for a draw region at `(x, y)` of size `(w, h)`.
 * `p` must not be NULL.
 */
void SetDefDrawEnv(DRAWENV* p, int x, int y, int w, int h);
/*
 * Initializes `p` for the corresponding display region.
 * `p` must not be NULL.
 */
void SetDefDispEnv(DISPENV* p, int x, int y, int w, int h);
/*
 * Clears `n` ordering-table entries before frame submission.
 * `p` must not be NULL.
 */
void ClearOTagR(uint32_t* p, int n);
/*
 * Enables or disables display output in the host stub.
 */
void SetDispMask(int mode);
/*
 * Completes pending host GPU work.
 */
void DrawSync(int mode);
/*
 * Selects `p` as the visible display environment.
 * `p` must not be NULL.
 */
void PutDispEnv(DISPENV* p);
/*
 * Interprets the ordering table using draw environment `e`.
 * `p` and `e` must not be NULL.
 */
void DrawOTagEnv(uint32_t* p, DRAWENV* e);

#endif // GPU_STUB_H
