/*
 * render_test.c - Host GPU packet and framebuffer checks
 *
 * Implementation notes:
 *
 * SDK GPU stubs interpret the renderer packet stream so geometry, clipping
 * and capacity can be inspected without a console.
 */

#include "value_api.h"

#include "render_backend.h"
#include "ui_style.h"
#include "ui_render.h"

#include <assert.h>
#include <psxgpu.h>
#include <stdio.h>
#include <string.h>

extern volatile unsigned render_packet_peak;
extern volatile unsigned render_overflows;
extern volatile unsigned render_cache_hits;
extern volatile unsigned render_cache_misses;

/*
 * Packet captures retain ordering-table depth and VRAM pixels so the test
 * can inspect both submitted commands and their rendered result.
 */
static uint32_t* current_ot;
static void* primitives[8][2048];
static int counts[8];
static int frame_count;
static uint16_t vram[512][1024];
static uint8_t output[240][320];
static int capture;
_Static_assert(sizeof(TILE) == 16 && sizeof(SPRT) == 20 &&
                   sizeof(DR_TPAGE) == 8,
               "SDK packet sizes");

/*
 * Encodes each link as an offset from the buffer's ordering table. This lets
 * the host stub traverse retained packets after the C backend restores a
 * static table, without relying on truncated host pointers in PSX tags.
 */
void addPrim(uint32_t* ot, void* packet)
{
    int depth = (int)(ot - current_ot);
    assert(depth >= 0 && depth < 8);
    uintptr_t offset = (uintptr_t)packet - (uintptr_t)current_ot;
    assert(offset > 0 && offset < 65536 + sizeof(uint32_t) * 8);
    *(uint32_t*)packet = *ot;
    *ot = (uint32_t)offset;
    if ((((DR_TPAGE*)packet)->code >> 24) == 0xe1) return;

    TILE* p = packet;
    assert(p->r0 == p->g0 && p->g0 == p->b0);
    int w = p->w;
    int h = p->h;
    if (p->code == 0x64)
    {
        SPRT* s = packet;
        w = s->w;
        h = s->h;
        assert(s->u0 + w <= 256 && s->v0 + h <= 96);
        assert(s->clut == ((96 << 6) | (640 >> 4)));
    }
    else
    {
        assert(p->code == 0x60);
    }

    assert(p->x0 >= 0 && p->y0 >= 0 && w > 0 && h > 0);
    assert(p->x0 + w <= 320 && p->y0 + h <= 240);
}
int LoadImage(const RECT* r, const uint32_t* data)
{
    assert(r->x == 640 && ((r->y == 0 && r->w == 64 && r->h == 96) ||
                           (r->y == 96 && r->w == 16 && r->h == 1)));

    const uint16_t* src = (const uint16_t*)data;
    for (int y = 0; y < r->h; y++)
    {
        for (int x = 0; x < r->w; x++) vram[r->y + y][r->x + x] = *src++;
    }
    if (r->y == 96)
    {
        assert(vram[96][640] == 0);
        for (int i = 1; i < 8; i++)
        {
            uint16_t rgb = vram[96][640 + i];
            assert(rgb);
            assert((rgb & 31) == ((rgb >> 5) & 31) &&
                   (rgb & 31) == ((rgb >> 10) & 31));
        }
    }
    return 0;
}

void ResetGraph(int mode)
{
    (void)mode;
}

void SetVideoMode(int mode)
{
    (void)mode;
}

void SetDispMask(int mode)
{
    (void)mode;
}

void DrawSync(int mode)
{
    (void)mode;
}

void VSync(int mode)
{
    (void)mode;
}

void SetDefDrawEnv(DRAWENV* p, int x, int y, int w, int h)
{
    (void)p;
    (void)x;
    (void)y;
    (void)w;
    (void)h;
}

void SetDefDispEnv(DISPENV* p, int x, int y, int w, int h)
{
    (void)p;
    (void)x;
    (void)y;
    (void)w;
    (void)h;
}

void ClearOTagR(uint32_t* p, int n)
{
    memset(p, 0, n * sizeof(*p));
    current_ot = p;
    memset(counts, 0, sizeof(counts));
}

void PutDispEnv(DISPENV* p)
{
    (void)p;
}

void DrawOTagEnv(uint32_t* p, DRAWENV* e)
{
    assert(p == current_ot + 7);
    frame_count++;
    assert(e->r0 == e->g0 && e->g0 == e->b0);
    if (capture) memset(output, (e->r0 >> 3) * 255 / 31, sizeof(output));
    for (int depth = 0; depth < 8; depth++)
    {
        counts[depth] = 0;
        for (uint32_t offset = current_ot[depth]; offset != 0;)
        {
            void* packet = (uint8_t*)current_ot + offset;
            assert(counts[depth] < 2048);
            primitives[depth][counts[depth]++] = packet;
            offset = *(uint32_t*)packet;
        }
    }
    int page = -1;
    for (int depth = 7; depth >= 0; depth--)
    {
        for (int i = 0; i < counts[depth]; i++)
        {
            void* packet = primitives[depth][i];
            if ((((DR_TPAGE*)packet)->code >> 24) == 0xe1)
            {
                page = ((DR_TPAGE*)packet)->code & 0x1ff;
                assert(page == getTPage(0, 0, 640, 0));
                continue;
            }
            TILE* t = packet;
            SPRT* s = packet;
            int textured = t->code == 0x64;
            if (textured) assert(page == getTPage(0, 0, 640, 0));
            if (!capture) continue;
            int w = textured ? s->w : t->w;
            int h = textured ? s->h : t->h;
            for (int y = 0; y < h; y++)
            {
                for (int x = 0; x < w; x++)
                {
                    int gray = (t->r0 >> 3) * 255 / 31;
                    if (textured)
                    {
                        int u = s->u0 + x;
                        int v = s->v0 + y;
                        int index =
                            (vram[v][640 + u / 4] >> ((u % 4) * 4)) & 15;
                        uint16_t rgb = vram[96][640 + index];
                        if (!rgb) continue;
                        assert((rgb & 31) == ((rgb >> 5) & 31) &&
                               (rgb & 31) == ((rgb >> 10) & 31));
                        gray = (rgb & 31) * 255 / 31;
                    }
                    output[t->y0 + y][t->x0 + x] = gray;
                }
            }
        }
    }
}

/*
 * Writes the current host framebuffer as a grayscale PGM for visual review.
 */
static void save(const char* path)
{
    FILE* f = fopen(path, "wb");
    assert(f);
    fprintf(f, "P5\n320 240\n255\n");
    fwrite(output, 1, sizeof(output), f);
    fclose(f);
}

static Editor e;

/*
 * Submits one frame and optionally saves its rendered grayscale image.
 */
static void draw(const char* name)
{
    render_frame(&e, 1, NULL);
    if (name)
    {
        char path[256];
        snprintf(path, sizeof(path), "build/tests/%s.pgm", name);
        save(path);
    }
}

/*
 * Finds sound-menu coordinates from rendered output to check selection
 * placement.
 */
static void sound_selection(int* x, int* y)
{
    int found = 0;
    for (int i = 0; i < counts[1]; i++)
    {
        TILE* p = primitives[1][i];
        if (p->w <= 62) continue;
        *x = p->x0;
        *y = p->y0;
        found++;
    }
    assert(found == 1);
}

/*
 * Checks that menu clipping does not draw into the protected display margin.
 */
static void assert_menu_edge(void)
{
    for (int depth = 0; depth <= 2; depth++)
    {
        for (int i = 0; i < counts[depth]; i++)
        {
            TILE* p = primitives[depth][i];
            int h = p->code == 0x64 ? ((SPRT*)p)->h : p->h;
            assert(p->y0 >= UI_MENU_EDGE &&
                   p->y0 + h <= SCREEN_H - UI_MENU_EDGE);
        }
    }
}

/*
 * Resolves the model tile beneath a grid coordinate for packet assertions.
 */
static int tile_at(int x, int y)
{
    return test_score_at(&e.score, x, y).tile;
}

static uint32_t output_hash(void)
{
    uint32_t hash = 2166136261u;
    for (size_t i = 0; i < sizeof(output); i++)
        hash = (hash ^ ((uint8_t*)output)[i]) * 16777619u;
    return hash;
}

/*
 * Finds an exact untextured packet in one ordering-table bucket.
 */
static int has_tile(int depth, int x, int y, int w, int h, int gray)
{
    for (int i = 0; i < counts[depth]; i++)
    {
        TILE* p = primitives[depth][i];
        if (p->code == 0x60 && p->x0 == x && p->y0 == y && p->w == w &&
            p->h == h && p->r0 == gray)
            return 1;
    }
    return 0;
}

int main(void)
{
    editor_init(&e);
    render_init();
    capture = 1;

    // A sparse plane must keep its single cursor separate from the lattice.
    e.x = 10;
    e.y = 7;
    draw("plane");
    uint32_t plane_hash = output_hash();
    draw(NULL);
    draw(NULL);
    assert(render_cache_hits == 1 && render_cache_misses == 2);
    assert(output_hash() == plane_hash);
    uint8_t background = (0x16 >> 3) * 255 / 31;
    for (int row = 0; row < 15; row++)
    {
        for (int col = 0; col < 20; col++)
        {
            if (col == e.x && row == e.y) continue;
            int changed = 0;
            for (int y = row * 16; y < (row + 1) * 16; y++)
            {
                for (int x = col * 16; x < (col + 1) * 16; x++)
                {
                    changed += output[y][x] != background;
                }
            }
            assert(changed == 1);
        }
    }
    // An admitted jump edit must refresh both connector geometry and its
    // cached origin list on the next frame.
    editor_init(&e);
    assert(score_create(&e.score, 0, 0, 4) == SCORE_OK);
    assert(score_place(&e.score, 1, 0, TILE_JUMP) == SCORE_OK);
    TileId jump = tile_at(1, 0);
    Lane* branch = &e.score.lanes[e.score.tiles[jump].branch];
    int x1 = CELL_SIZE + CELL_SIZE / 2;
    int y1 = CELL_SIZE / 2;
    int x2 = branch->x * CELL_SIZE + CELL_SIZE / 2;
    int y2 = branch->y * CELL_SIZE + CELL_SIZE / 2;
    draw(NULL);
    assert(has_tile(6, x2, y1, x1 - x2 + 1, 1, UI_BORDER));
    assert(has_tile(6, x2, y1, 1, y2 - y1 + 1, UI_BORDER));
    uint32_t jump_hash = output_hash();
    draw(NULL);
    draw(NULL);
    assert(output_hash() == jump_hash);
    uint32_t revision = e.score.revision;
    assert(score_remove(&e.score, 1, 0) == SCORE_OK);
    assert(e.score.revision == revision + 1);
    draw(NULL);
    assert(!has_tile(6, x2, y1, x1 - x2 + 1, 1, UI_BORDER));
    assert(!has_tile(6, x2, y1, 1, y2 - y1 + 1, UI_BORDER));
    draw(NULL);
    assert(!has_tile(6, x2, y1, x1 - x2 + 1, 1, UI_BORDER));

    // A held tile previews its suffix at the destination, and a new cursor
    // position changes the preview even while the score stays cached.
    editor_init(&e);
    assert(score_create(&e.score, 0, 0, 4) == SCORE_OK);
    assert(score_place(&e.score, 1, 0, TILE_NOTE) == SCORE_OK);
    assert(score_place(&e.score, 1, 1, TILE_NOTE) == SCORE_OK);
    assert(score_place(&e.score, 2, 0, TILE_NOTE) == SCORE_OK);
    e.mode = EDIT_MOVE;
    e.source_x = 1;
    e.source_y = 0;
    e.x = 2;
    e.y = 0;
    draw(NULL);
    assert(has_tile(4, 2 * 16 + 2, 2, 12, 1, UI_INK));
    assert(has_tile(4, 2 * 16 + 2, 16 + 2, 12, 1, UI_INK));
    uint32_t move_hash = output_hash();
    draw(NULL);
    draw(NULL);
    assert(output_hash() == move_hash);
    e.x = 0;
    draw(NULL);
    assert(has_tile(4, 2, 2, 12, 1, UI_RAIL));
    e.source_x = 0;
    e.x = 5;
    draw(NULL);
    assert(has_tile(4, 5 * 16 + 2, 2, 12, 1, UI_INK));
    assert(has_tile(4, 6 * 16 + 2, 16 + 2, 12, 1, UI_INK));

    // Each runner gets its own gutter bar, extending beside a full stack.
    e.score.lanes[0] = (Lane){.active = 1, .x = 2, .y = 2, .length = 2};
    e.score.lanes[1] = (Lane){.active = 1, .x = 2, .y = 5, .length = 2};
    e.score.lanes[0].tiles[0] = 1;
    e.score.tiles[1].next = 2;
    e.score.revision++;
    AudioPlayheads heads = {.revision = e.score.revision,
                            .count = 2,
                            .items = {{0, 0}, {1, 1}}};
    render_frame(&e, 1, &heads);
    assert(output[33][48] != background);
    assert(output[49][48] != background);
    assert(output[81][64] != background);
    heads.revision++;
    render_frame(&e, 1, &heads);
    assert(output[33][48] == background);
    assert(output[49][48] == background);
    assert(output[81][64] == background);
    editor_init(&e);
    e.x = 10;
    e.y = 7;
    e.mode = EDIT_MAIN;
    e.selected = 0;
    draw("main");
    uint32_t main_hash = output_hash();
    e.selected = 2;
    draw("main-last-row");
    assert(output_hash() != main_hash);
    e.mode = EDIT_CARD;
    e.selected = 3;
    draw("card");
    for (int status = STORAGE_UNKNOWN; status <= STORAGE_GENERATION_FULL;
         status++)
    {
        e.slot_status = status;
        e.card_free = status % 16;
        e.message = storage_message(status);
        e.selected = 2;
        draw(NULL);
    }
    e.message = "";
    e.mode = EDIT_REVERB;
    e.selected = 1;
    draw("reverb");

    assert(score_create(&e.score, 0, 0, 1) == SCORE_OK);
    assert(score_place(&e.score, 1, 0, TILE_RELATIVE) == SCORE_OK);
    e.target = tile_at(1, 0);
    e.mode = EDIT_LOCK;
    e.selected = 0;
    draw("lock-first-row");
    assert_menu_edge();
    e.selected = 9;
    draw("lock-last-row");
    assert_menu_edge();

    // Exercise the renderer's full 300-cell view; bypass model admission here
    // because the fixture intentionally exceeds persistence and lane limits.
    for (int lane = 0; lane < SCORE_LANES; lane++)
    {
        Lane* l = &e.score.lanes[lane];
        l->active = 1;
        l->x = lane * 8;
        l->y = 0;
        l->length = 64;
        l->division = 16;
        l->channel = lane % SCORE_CHANNELS;
    }
    for (int i = 0; i < SCORE_TILE_CAPACITY; i++)
    {
        TileValue v = test_score_default((TileKind)(1 + i % 3));
        v.pitch = 108;
        v.length = 1280;
        v.period = 32;
        v.chance = 100;
        v.lock_mask = 3;
        v.attack = -16000;
        v.release = 16000;
        e.score.tiles[i + 1] = (Tile){v, 0, -1};
    }
    for (int lane = 0; lane < SCORE_LANES; lane++)
    {
        for (int step = 0; step < 64; step++)
        {
            e.score.lanes[lane].tiles[step] = (TileId)(1 + lane * 64 + step);
        }
    }
    e.score.revision++;
    e.mode = EDIT_PLANE;
    for (int y = 0; y < 64; y++)
    {
        for (int x = 0; x < 128; x++)
        {
            e.x = x;
            e.y = y;
            draw(NULL);
        }
    }
    for (int corner = 0; corner < 4; corner++)
    {
        e.x = (corner & 1) ? 127 : 0;
        e.y = (corner & 2) ? 63 : 0;
        e.mode = EDIT_MENU;
        e.selected = 0;
        draw("context-corner");
        e.mode = EDIT_SOUND;
        e.sound_channel = 7;
        e.selected = 12;
        e.score.sounds[7] = (SoundSettings){
            16000, 16000, WAVE_TRIANGLE, WAVE_NOISE, 500, 500, -24, 2000, 1, 0, 0, 0, 100};
        draw("sound-corner");
        e.mode = EDIT_PICKER;
        draw("picker-corner");
        e.mode = EDIT_PATTERN;
        e.pattern_cursor = 31;
        draw("pattern-corner");
        e.mode = EDIT_DELETE;
        e.target = tile_at(0, 1);
        draw("delete-corner");
    }

    e.mode = EDIT_SOUND;
    e.x = 1;
    e.y = 1;
    e.sound_channel = 7;
    e.selected = 0;
    draw("sound-top");
    assert_menu_edge();
    int top_x;
    int top_y;
    int bottom_x;
    int bottom_y;
    sound_selection(&top_x, &top_y);
    const int sound_rows[] = {1, 2, 3, 5, 6, 8, 9, 11, 12, 14, 15, 16};
    for (unsigned i = 0; i < sizeof(sound_rows) / sizeof(*sound_rows); i++)
    {
        e.selected = sound_rows[i];
        draw(NULL);
        assert_menu_edge();
        sound_selection(&bottom_x, &bottom_y);
        assert(bottom_x == top_x && bottom_y >= 0 && bottom_y < 240);
    }
    draw("sound");
    assert(top_y < bottom_y);

    e.mode = EDIT_PATTERN;
    e.pattern_cursor = 31;
    draw("pattern");
    e.mode = EDIT_PICKER;
    draw("picker");
    e.mode = EDIT_DELETE;
    e.target = tile_at(0, 1);
    draw("delete");

    assert(render_overflows == 0);
    printf("PASS: %d render frames; peak %u / 65536 bytes; no overflow or "
           "screen escape\n",
           frame_count, render_packet_peak);
}
