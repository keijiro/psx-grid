/*
 * render.c - Double-buffered grid and menu rendering
 *
 * Implementation notes:
 *
 * GPU primitives are allocated from fixed packet buffers. Ordering-table
 * depths control layering while host checks measure packet use.
 */

#include "render.h"

#include <psxetc.h>
#include <psxgpu.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

#include "ui_atlas.h"
#include "ui_style.h"

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
static int camera_x;
static int camera_y;
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

/*
 * Clips panel-depth primitives to display margins while leaving grid-depth
 * primitives available across the full frame.
 */
static void rect(int depth, int x, int y, int w, int h, int gray)
{
    // Menu packets stay inside the display edge even when their panel scrolls
    // beyond it; the plane continues to use the full framebuffer.
    int top = depth <= 2 ? UI_MENU_EDGE : 0;
    int bottom = depth <= 2 ? SCREEN_H - UI_MENU_EDGE : SCREEN_H;
    if (x < 0)
    {
        w += x;
        x = 0;
    }
    if (y < top)
    {
        h += y - top;
        y = top;
    }
    if (x + w > SCREEN_W) w = SCREEN_W - x;
    if (y + h > bottom) h = bottom - y;
    if (w <= 0 || h <= 0) return;
    TILE* p = packet(sizeof(TILE));
    if (!p) return;
    setTile(p);
    setXY0(p, x, y);
    setWH(p, w, h);
    setRGB0(p, gray, gray, gray);
    addPrim(&buffers[active].ot[depth], p);
}

static void outline(int depth, int x, int y, int w, int h, int gray)
{
    rect(depth, x, y, w, 1, gray);
    rect(depth, x, y + h - 1, w, 1, gray);
    rect(depth, x, y, 1, h, gray);
    rect(depth, x + w - 1, y, 1, h, gray);
}

/*
 * Clips both destination and texture origin so partial sprites keep their
 * source pixels aligned.
 */
static void sprite(int depth, int x, int y, int u, int v, int w, int h)
{
    int top = depth <= 2 ? UI_MENU_EDGE : 0;
    int bottom = depth <= 2 ? SCREEN_H - UI_MENU_EDGE : SCREEN_H;
    if (x < 0)
    {
        u -= x;
        w += x;
        x = 0;
    }
    if (y < top)
    {
        v += top - y;
        h += y - top;
        y = top;
    }
    if (x + w > SCREEN_W) w = SCREEN_W - x;
    if (y + h > bottom) h = bottom - y;
    if (w <= 0 || h <= 0) return;
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

static int glyph(unsigned char ch)
{
    return ch >= 32 && ch <= 126 ? ch - 32 : '?' - 32;
}

static void text(int x, int y, const char* s)
{
    while (*s)
    {
        int i = glyph((unsigned char)*s++);
        int advance = ui_advance[i];
        if (i != 0)
        {
            sprite(0, x, y, (i % 32) * 8, 24 + (i / 32) * 8, advance - 1, 7);
        }
        x += advance;
    }
}

static int text_width(const char* s)
{
    int width = 0;
    while (*s) width += ui_advance[glyph((unsigned char)*s++)];
    return width;
}

/*
 * Stops before a whole glyph would cross the panel limit, avoiding partial
 * labels in the small font.
 */
static void clipped_text(int x, int y, const char* s, int right)
{
    while (*s)
    {
        int i = glyph((unsigned char)*s++);
        int advance = ui_advance[i];
        if (x + advance > right) break;
        if (i) sprite(0, x, y, (i % 32) * 8, 24 + (i / 32) * 8, advance - 1, 7);
        x += advance;
    }
}

/*
 * Fills a panel with clipped horizontal strips, including its angled edges.
 */
static void clipped_panel(int x, int y, int w, int h, int gray)
{
    // Match the reference's diagonal cuts at native resolution. Scanline strips
    // keep the panel and its shadow on the same integer pixel edge.
    const int top = 9;
    const int bottom = 9;
    for (int i = 0; i < top; i++)
    {
        rect(2, x + top - i, y + i, w - top + i, 1, gray);
    }
    rect(2, x, y + top, w, h - top - bottom, gray);
    for (int i = 0; i < bottom; i++)
    {
        rect(2, x, y + h - bottom + i, w - i - 1, 1, gray);
    }
}

static void menu_panel(int x, int y, int w, int h)
{
    clipped_panel(x, y, w, h, UI_PANEL);
    clipped_panel(x + 4, y + 4, w, h, UI_SHADOW);
}

/*
 * Right-aligns a row value while clipping its label before the value begins;
 * selection underlines stay in a shallower ordering-table bucket.
 */
static void menu_row(const Editor* e, const EditorRow* row, int index, int x,
                     int y, int width)
{
    char value[48];
    editor_row_value(e, row->id, value, sizeof(value));
    int right = x + width;
    int value_x = right - text_width(value);
    clipped_text(x, y, row->label, value[0] ? value_x - 6 : right);
    if (value[0]) clipped_text(value_x, y, value, right);
    if (index == e->selected) rect(1, x, y + 9, width, 1, UI_INK);
}

/*
 * Keeps the labels for all menu surfaces in one place.
 */
static void menu_title(const Editor* e, char title[40])
{
    if (e->mode == EDIT_MAIN)
    {
        strcpy(title, "JACQUARD / MAIN");
    }
    else if (e->mode == EDIT_REVERB)
    {
        strcpy(title, "REVERB / GLOBAL");
    }
    else if (e->mode == EDIT_SOUND)
    {
        snprintf(title, 40, "SOUND CH %d", e->sound_channel + 1);
    }
    else if (e->mode == EDIT_PATTERN)
    {
        strcpy(title, "CYCLE PATTERN");
    }
    else if (e->mode == EDIT_PICKER)
    {
        strcpy(title, "CREATE TILE");
    }
    else if (e->mode == EDIT_DELETE)
    {
        if (e->target)
        {
            strcpy(title, "DELETE JUMP BRANCH?");
        }
        else if (e->score.lanes[e->lane].source)
        {
            strcpy(title, "DELETE BRANCH LANE?");
        }
        else
        {
            strcpy(title, "DELETE LANE?");
        }
    }
    else
    {
        strcpy(title, "MENU");
    }
}

/*
 * Sizes the panel to its visible rows and footer text.
 */
static int menu_width(const Editor* e, const EditorRow* rows, int count,
                      const char* title, const char* status)
{
    char value[48];
    // Size each panel around its visible text. The pattern grid keeps a fixed
    // width because its eight columns need a stable 26-pixel pitch.
    int width =
        e->mode == EDIT_PATTERN ? 232 : text_width(title) + 2 * UI_MENU_MARGIN;
    if (e->mode != EDIT_PATTERN)
    {
        for (int i = 0; i < count; i++)
        {
            editor_row_value(e, rows[i].id, value, sizeof(value));
            int row_width = text_width(rows[i].label) + text_width(value) +
                            (value[0] ? 12 : 0) + 30;
            if (row_width > width) width = row_width;
        }
        if (e->mode == EDIT_PICKER)
        {
            for (int k = 1; k < TILE_KIND_COUNT; k++)
            {
                int row_width = text_width(score_tile_label((TileKind)k)) + 32;
                if (row_width > width) width = row_width;
            }
        }
        int status_width = text_width(status) + 30;
        if (status_width > width) width = status_width;
        if (e->message)
        {
            int message_width = text_width(e->message) + 30;
            if (message_width > width) width = message_width;
        }
        if (width < 128) width = 128;
        if (width > 286) width = 286;
    }
    return width;
}

/*
 * Keeps row and footer placement consistent across the menu surfaces.
 */
static int menu_height(const Editor* e, int count, int alert)
{
    // Leave ten pixels below the last selection underline, rather than
    // reserving a full unused row. Status and message lines add their own
    // space.
    int bottom = 35 + (count - 1) * UI_MENU_ROW + 10;
    if (e->mode == EDIT_PATTERN)
    {
        bottom =
            39 + ((e->score.tiles[e->target].value.period - 1) / 8) * 18 + 10;
    }
    else if (e->mode == EDIT_PICKER)
    {
        bottom = 32 + (TILE_KIND_COUNT - 2) * UI_MENU_ROW + 10;
    }
    else if (e->mode == EDIT_DELETE) bottom = 11 + 7;
    int height = bottom + 10;
    if (e->mode == EDIT_MAIN) height += UI_MENU_ROW;
    if (alert) height += 14;
    return height;
}

/*
 * Draws the surface-specific body after the shared panel and title.
 */
static void menu_body(const Editor* e, const EditorRow* rows, int count, int x,
                      int y, int width, int height, const char* status,
                      int alert)
{
    if (e->mode == EDIT_PATTERN)
    {
        TileValue v = e->score.tiles[e->target].value;
        for (int i = 0; i < v.period; i++)
        {
            int gx = x + 16 + (i % 8) * 26;
            int gy = y + 39 + (i / 8) * 18;
            char digit[2] =
            {
                v.pattern & ((uint32_t)1 << i) ? '1' : '0', 0
            };
            text(gx + 8, gy, digit);
            if (i == e->pattern_cursor) rect(1, gx + 5, gy + 9, 13, 1, UI_INK);
        }
    }
    else if (e->mode == EDIT_PICKER)
    {
        for (int k = 1; k < TILE_KIND_COUNT; k++)
        {
            int py = y + 32 + (k - 1) * UI_MENU_ROW;
            clipped_text(x + 16, py, score_tile_label((TileKind)k),
                         x + width - 16);
            if (k == (int)e->tile_candidate)
            {
                rect(1, x + 16, py + 9, width - 32, 1, UI_INK);
            }
        }
    }
    else if (e->mode != EDIT_DELETE)
    {
        for (int i = 0; i < count; i++)
        {
            int py = y + 35 + i * UI_MENU_ROW;
            if (rows[i].kind == ROW_HEADING)
            {
                clipped_text(x + 15, py, rows[i].label, x + width - 15);
                rect(1, x + 15, py + 10, 62, 1, UI_RULE);
            }
            else
            {
                menu_row(e, &rows[i], i, x + 15, py, width - 30);
            }
        }
        if (e->mode == EDIT_MAIN)
        {
            clipped_text(x + 15, y + height - (alert ? 31 : 17), status,
                         x + width - 15);
        }
    }
    if (alert)
    {
        clipped_text(x + 15, y + height - 17, e->message, x + width - 15);
    }
}

/*
 * Builds the current menu, picker, pattern editor or deletion confirmation
 * from the same editor state consumed by the grid view.
 */
static void draw_menu(const Editor* e)
{
    EditorRow rows[EDITOR_ROWS];
    int count = editor_rows(e, rows);
    char title[40];
    menu_title(e, title);
    char status[64] = "";
    if (e->mode == EDIT_MAIN)
    {
        snprintf(status, sizeof(status), "%s", storage_message(e->slot_status));
        if (e->card_free >= 0)
        {
            snprintf(status, sizeof(status), "%s  %d BLK",
                     storage_message(e->slot_status), e->card_free);
        }
    }
    int width = menu_width(e, rows, count, title, status);
    int alert = e->message && e->message[0];
    int height = menu_height(e, count, alert);
    int x = (SCREEN_W - width) / 2;
    int y = (SCREEN_H - height) / 2;
    if (height > SCREEN_H)
    {
        // Keep the entire menu laid out as one panel and move it just enough to
        // keep the selected row visible within the display margins.
        int selected_bottom = 35 + e->selected * UI_MENU_ROW + 10;
        y = UI_MENU_EDGE;
        if (y + selected_bottom > SCREEN_H - UI_MENU_EDGE)
        {
            y = SCREEN_H - UI_MENU_EDGE - selected_bottom;
        }
    }
    menu_panel(x, y, width, height);
    clipped_text(x + UI_MENU_MARGIN, y + 11, title, x + width - UI_MENU_MARGIN);
    menu_body(e, rows, count, x, y, width, height, status, alert);
}

static void tile(int depth, int x, int y, int kind)
{
    sprite(depth, x, y, kind * 16, 0, 16, 16);
}

/*
 * The body spans x+2..14 and y+1..14. Brackets at x+1..15 and y+0..15
 * touch its straight edges without covering the body.
 */
static void cursor(int x, int y)
{
    const int width = 15;
    const int height = 16;
    const int arm = 4;
    x++;
    for (int j = 0; j < 2; j++)
    {
        for (int i = 0; i < 2; i++)
        {
            rect(3, x + i * (width - arm), y + j * (height - 1), arm, 1,
                 UI_INK);
            rect(3, x + i * (width - 1), y + j * (height - arm), 1, arm,
                 UI_INK);
        }
    }
}

static int clamp(int v, int max)
{
    return v < 0 ? 0 : v > max ? max : v;
}

/*
 * Keeps two grid cells of context around the cursor when possible and clamps
 * at score edges.
 */
static int follow(int camera, int cursor, int size, int bound)
{
    if (cursor < camera + 2) camera = cursor - 2;
    if (cursor >= camera + size - 2) camera = cursor - size + 3;
    return clamp(camera, bound - size);
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
 * Draws the tile's compact atlas glyphs centered within one grid cell.
 */
static void small(int x, int y, const char* s, int dark, int shift,
                  int first_shift)
{
    const char* chars = "0123456789ABCDEFG+LR%h";
    // Tight spacing after the compact plus keeps sharp labels inside the tile
    // body.
    int width = 0;
    int advance = 0;
    int compact = 0;
    for (const char* p = s; *p; p++)
    {
        advance = *p == '+' || (compact && *p >= '0' && *p <= '9') ? 3 : 4;
        width += advance;
        compact = *p == '+' || (compact && *p >= '0' && *p <= '9');
    }
    if (!width) return;
    width -= advance - 3;
    int inset = (CELL_SIZE - width) / 2;
    x += (inset < 2 ? 2 : inset) + shift;
    compact = 0;
    while (*s)
    {
        char ch = *s++;
        const char* p = strchr(chars, ch);
        if (p)
        {
            sprite(5, x + first_shift, y, (int)(p - chars) * 4, dark ? 56 : 48,
                   3, 5);
        }
        first_shift = 0;
        x += ch == '+' || (compact && ch >= '0' && ch <= '9') ? 3 : 4;
        compact = ch == '+' || (compact && ch >= '0' && ch <= '9');
    }
}

/*
 * Maps one resolved model cell to its atlas sprite and optional text label.
 */
static void draw_cell(const Score* s, Cell c, int x, int y)
{
    if (c.kind == CELL_EMPTY) return;
    int kind;
    if (c.kind == CELL_HEAD) kind = s->lanes[c.lane].source ? 5 : 0;
    else if (c.kind == CELL_END) kind = 7;
    else if (c.kind == CELL_STEP)
    {
        kind = 8;
    }
    else
    {
        kind = s->tiles[c.tile].value.kind;
        if (kind == TILE_RELATIVE) kind = 6;
    }
    char label[16] = "";
    int shift = 0;
    int first_shift = 0;
    if (c.kind == CELL_HEAD)
    {
        if (s->lanes[c.lane].source)
        {
            strcpy(label, "B");
        }
        else
        {
            snprintf(label, sizeof(label), "Ch%d",
                     score_channel(s, c.lane) + 1);
            shift = 1;
        }
    }
    if (c.kind == CELL_TILE)
    {
        TileValue v = s->tiles[c.tile].value;
        if (v.kind == TILE_NOTE)
        {
            snprintf(label, sizeof(label), "%s%d", score_note_name(v.pitch),
                     v.pitch / 12);
            for (char* p = label; *p; p++)
            {
                if (*p == '#') *p = '+';
            }
            // The plus and octave already align; only the note letter needs a
            // nudge.
            if (strchr(label, '+')) first_shift = 1;
            else shift = 1;
        }
        if (v.kind == TILE_RELATIVE)
        {
            snprintf(label, sizeof(label), "%s%s",
                     v.lock_mask & LOCK_ATTACK ? "A" : "",
                     v.lock_mask & LOCK_RELEASE ? "R" : "");
        }
        if (v.kind == TILE_CYCLE)
        {
            snprintf(label, sizeof(label), "C%d", v.period);
        }
        if (v.kind == TILE_PROBABILITY)
        {
            snprintf(label, sizeof(label), "%d", v.chance);
        }
    }
    // Labels share the tile bucket so later panels cover them completely.
    small(x, y + 5, label, c.kind == CELL_HEAD, shift, first_shift);
    tile(5, x, y, kind);
}

static int screen_x(int x)
{
    return VIEW_X + clamp(x - camera_x, VIEW_COLS - 1) * CELL_SIZE;
}

static int screen_y(int y)
{
    return VIEW_Y + clamp(y - camera_y, VIEW_ROWS - 1) * CELL_SIZE;
}

static int visible(int x, int y)
{
    return x >= camera_x && x < camera_x + VIEW_COLS && y >= camera_y &&
           y < camera_y + VIEW_ROWS;
}

static void marker(int x, int y, int gray)
{
    outline(4, screen_x(x) + 1, screen_y(y) + 1, 14, 14, gray);
}

void render_frame(const Editor* e, int connected)
{
    camera_x = follow(camera_x, e->x, VIEW_COLS, SCORE_WIDTH);
    camera_y = follow(camera_y, e->y, VIEW_ROWS, SCORE_HEIGHT);
    used = 0;
    ClearOTagR(buffers[active].ot, OT_SIZE);
    for (int row = 0; row < VIEW_ROWS; row++)
    {
        for (int col = 0; col < VIEW_COLS; col++)
        {
            int x = VIEW_X + col * CELL_SIZE;
            int y = VIEW_Y + row * CELL_SIZE;
            Cell c;
            score_at(&e->score, camera_x + col, camera_y + row, &c);
            rect(7, x + 8, y + 8, 1, 1, UI_DOT);
            if (c.kind == CELL_EMPTY) continue;
            const Lane* l = &e->score.lanes[c.lane];
            if (!c.depth)
            {
                for (int dx = 0; dx < CELL_SIZE; dx += 4)
                {
                    int logical = (camera_x + col - l->x) * CELL_SIZE + dx - 8;
                    if (logical >= 0 && logical <= (l->length + 1) * CELL_SIZE)
                    {
                        rect(6, x + dx, y + 8, 1, 1, UI_RAIL);
                    }
                }
            }
            if (c.depth) rect(6, x + 8, y, 1, 16, UI_RAIL);
            draw_cell(&e->score, c, x, y);
        }
    }
    for (int i = 0; i < SCORE_LANES; i++)
    {
        if (e->score.lanes[i].active)
        {
            const Lane* l = &e->score.lanes[i];
            for (int j = 0; j < l->length; j++)
            {
                int d = 0;
                for (TileId t = l->tiles[j]; t; t = e->score.tiles[t].next, d++)
                {
                    if (e->score.tiles[t].value.kind == TILE_JUMP)
                    {
                        const Lane* b =
                            &e->score.lanes[e->score.tiles[t].branch];
                        int sx = l->x + j + 1;
                        int sy = l->y + d;
                        if (!visible(sx, sy) && !visible(b->x, b->y)) continue;
                        int x1 = screen_x(sx) + 8;
                        int y1 = screen_y(sy) + 8;
                        int x2 = screen_x(b->x) + 8;
                        int y2 = screen_y(b->y) + 8;
                        rect(6, x1 < x2 ? x1 : x2, y1,
                             (x1 < x2 ? x2 - x1 : x1 - x2) + 1, 1, UI_BORDER);
                        rect(6, x2, y1 < y2 ? y1 : y2, 1,
                             (y1 < y2 ? y2 - y1 : y1 - y2) + 1, UI_BORDER);
                        if (!visible(b->x, b->y)) marker(b->x, b->y, UI_INK);
                        if (!visible(sx, sy)) marker(sx, sy, UI_INK);
                    }
                }
            }
        }
    }
    if (e->mode == EDIT_MOVE)
    {
        MovePlan p;
        score_plan_move(&e->score, e->source_x, e->source_y, e->x, e->y, &p);
        Cell source;
        score_at(&e->score, e->source_x, e->source_y, &source);
        int gray = p.result == SCORE_OK ? UI_INK : UI_RAIL;
        marker(e->source_x, e->source_y, UI_BORDER);
        for (int row = 0; row < VIEW_ROWS; row++)
        {
            for (int col = 0; col < VIEW_COLS; col++)
            {
                int x = camera_x + col;
                int y = camera_y + row;
                Cell c;
                score_at(&e->score, x - e->x + e->source_x,
                         y - e->y + e->source_y, &c);
                int carried =
                    source.kind == CELL_HEAD
                        ? c.lane == source.lane
                        : c.kind == CELL_TILE && c.lane == source.lane &&
                              c.step == source.step && c.depth >= source.depth;
                Cell dest;
                score_resolve(&e->score, e->x, e->y, &dest);
                if (source.kind == CELL_TILE && dest.lane == source.lane &&
                    dest.step == source.step)
                {
                    carried = x == e->x && y == e->y;
                }
                if (carried)
                {
                    outline(4, VIEW_X + col * 16 + 2, VIEW_Y + row * 16 + 2, 12,
                            12, gray);
                }
            }
        }
    }
    int cx = screen_x(e->x);
    int cy = screen_y(e->y);
    cursor(cx, cy);
    if (e->mode != EDIT_PLANE && e->mode != EDIT_MOVE) draw_menu(e);
    (void)connected;
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
