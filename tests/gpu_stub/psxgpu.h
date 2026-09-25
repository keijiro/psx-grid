/*
 * psxgpu.h - GPU packet types and calls for host rendering tests
 */

#ifndef GPU_STUB_H
#define GPU_STUB_H

#include <stdint.h>
#include <stddef.h>

// SDK-compatible filled rectangle packet inspected by the host renderer.
typedef struct {
    uint32_t tag;
    uint8_t r0, g0, b0, code;
    int16_t x0, y0, w, h;
} TILE;

// SDK-compatible textured sprite packet inspected by the host renderer.
typedef struct {
    uint32_t tag;
    uint8_t r0, g0, b0, code;
    int16_t x0, y0;
    uint8_t u0, v0;
    uint16_t clut;
    int16_t w, h;
} SPRT;

// Texture-page command packet used to begin an ordering table.
typedef struct {
    uint32_t tag, code;
} DR_TPAGE;

// Rectangle describing a host texture upload region.
typedef struct {
    int16_t x, y, w, h;
} RECT;

// Draw environment fields consumed by the rendering checks.
typedef struct {
    int r0, g0, b0, isbg;
} DRAWENV;

// Display environment placeholder for the host implementation.
typedef struct {
    int unused;
} DISPENV;

// Packet constructors mirror the SDK macros used by render.c.
#define setTile(p) ((p)->code = 0x60)
#define setSprt(p) ((p)->code = 0x64)
#define setXY0(p, x, y) ((p)->x0 = (x), (p)->y0 = (y))
#define setWH(p, width, height) ((p)->w = (width), (p)->h = (height))
#define setUV0(p, u, v) ((p)->u0 = (u), (p)->v0 = (v))
#define setRGB0(p, r, g, b) ((p)->r0 = (r), (p)->g0 = (g), (p)->b0 = (b))
#define getTPage(tp, abr, x, y) (((x) >> 6) | (((y) & 256) >> 4) | ((tp) << 7) | ((abr) << 5))
#define setClut(p, x, y) ((p)->clut = ((y) << 6) | ((x) >> 4))
#define setDrawTPage(p, dfe, dtd, page) ((p)->code = 0xe1000000 | (page))

/* Links primitive `p` into ordering-table bucket `ot`. */
void addPrim(uint32_t* ot, void* p);
/* Resets host GPU state before a rendering test. */
void ResetGraph(int mode);
/* Copies texture data into the host VRAM region `rect`. */
int LoadImage(const RECT* rect, const uint32_t* data);
/* Initializes `p` for a draw region at `(x, y)` of size `(w, h)`. */
void SetDefDrawEnv(DRAWENV* p, int x, int y, int w, int h);
/* Initializes `p` for the corresponding display region. */
void SetDefDispEnv(DISPENV* p, int x, int y, int w, int h);
/* Clears `n` ordering-table entries before frame submission. */
void ClearOTagR(uint32_t* p, int n);
/* Enables or disables display output in the host stub. */
void SetDispMask(int mode);
/* Completes pending host GPU work. */
void DrawSync(int mode);
/* Selects `p` as the visible display environment. */
void PutDispEnv(DISPENV* p);
/* Interprets the ordering table using draw environment `e`. */
void DrawOTagEnv(uint32_t* p, DRAWENV* e);

#endif // GPU_STUB_H
