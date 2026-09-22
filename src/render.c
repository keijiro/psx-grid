#include "render.h"
#include <psxgpu.h>
#include <psxetc.h>
#include <stdio.h>
#include <string.h>
#include <stddef.h>
#include "ui_style.h"
#include "ui_atlas.h"
#define PACKET_BYTES 32768
#define OT_SIZE 8
// OT traverses high depths first and reverses insertion within each bucket.
// 7: texture setup/lattice, 6: rails, 5: tiles, 4: endpoint, 3: cursor,
// 2: panels, 1: panel borders, 0: text and picker samples (non-overlapping).
typedef struct {
    DRAWENV draw;
    DISPENV disp;
    uint32_t ot[OT_SIZE];
    uint32_t packets[PACKET_BYTES / 4];
} Buffer;
static Buffer buffers[2];
static int active, camera_x, camera_y;
static size_t used;
// Debugger-visible counters; overflow is rejected before touching memory.
volatile unsigned render_packet_peak, render_overflows;
static void *packet(size_t bytes) {
    if (bytes > PACKET_BYTES - used) { render_overflows++; return NULL; }
    void *p = (uint8_t *)buffers[active].packets + used;
    used += bytes;
    if (used > render_packet_peak) render_packet_peak = used;
    return p;
}
static void rect(int depth, int x, int y, int w, int h, int gray) {
    TILE *p = packet(sizeof(TILE));
    if (!p) return;
    setTile(p); setXY0(p, x, y); setWH(p, w, h); setRGB0(p, gray, gray, gray);
    addPrim(&buffers[active].ot[depth], p);
}
static void outline(int depth, int x, int y, int w, int h, int gray) {
    rect(depth,x,y,w,1,gray); rect(depth,x,y+h-1,w,1,gray);
    rect(depth,x,y,1,h,gray); rect(depth,x+w-1,y,1,h,gray);
}
static void sprite(int depth, int x, int y, int u, int v, int w, int h) {
    SPRT *p = packet(sizeof(SPRT));
    if (!p) return;
    setSprt(p); setXY0(p,x,y); setUV0(p,u,v); setWH(p,w,h);
    setRGB0(p,128,128,128); setClut(p,640,64);
    addPrim(&buffers[active].ot[depth],p);
}
static int glyph(unsigned char ch) { return ch >= 32 && ch <= 126 ? ch-32 : '?'-32; }
static int text_width(const char *s) {
    int width = 0;
    while (*s) width += ui_advance[glyph((unsigned char)*s++)];
    return width ? width-1 : 0;
}
static void text(int x, int y, const char *s) {
    while (*s) {
        int i = glyph((unsigned char)*s++), advance = ui_advance[i];
        if (i != 0) sprite(0,x,y,(i%32)*8,24+(i/32)*8,advance-1,7);
        x += advance;
    }
}
static void tile(int depth, int x, int y, int kind) {
    sprite(depth,x,y,kind*16,0,16,16);
}
static void cursor(int x, int y) {
    for (int j=0;j<2;j++) for (int i=0;i<2;i++) {
        rect(3,x+i*(CELL_SIZE-4),y+j*(CELL_SIZE-1),4,1,UI_INK);
        rect(3,x+i*(CELL_SIZE-1),y+j*(CELL_SIZE-4),1,4,UI_INK);
    }
}
static int clamp(int v, int max) { return v < 0 ? 0 : v > max ? max : v; }
static int follow(int camera, int cursor, int size, int bound) {
    if (cursor < camera + 2) camera = cursor - 2;
    if (cursor >= camera + size - 2) camera = cursor - size + 3;
    return clamp(camera, bound - size);
}
void render_init(void) {
    ResetGraph(0); SetVideoMode(MODE_NTSC);
    // 4-bit texels occupy 64x64 VRAM words at x=640. CLUT sits immediately
    // below; neither overlaps framebuffers (x=0..319, y=0..479), and UVs
    // stay inside one texture page. Index zero alone is transparent.
    RECT atlas = {640,0,64,64}, clut = {640,64,16,1};
    LoadImage(&atlas,ui_pixels); DrawSync(0);
    LoadImage(&clut,ui_clut); DrawSync(0);
    for (int i = 0; i < 2; i++) {
        SetDefDrawEnv(&buffers[i].draw, 0, i * SCREEN_H, SCREEN_W, SCREEN_H);
        SetDefDispEnv(&buffers[i].disp, 0, i * SCREEN_H, SCREEN_W, SCREEN_H);
        buffers[i].draw.isbg = 1;
        setRGB0(&buffers[i].draw, UI_BACKGROUND, UI_BACKGROUND, UI_BACKGROUND);
        ClearOTagR(buffers[i].ot, OT_SIZE);
    }
    SetDispMask(1);
}
void render_frame(const Editor *e, int connected) {
    camera_x = follow(camera_x, e->x, VIEW_COLS, SCORE_WIDTH);
    camera_y = follow(camera_y, e->y, VIEW_ROWS, SCORE_HEIGHT);
    used = 0;
    ClearOTagR(buffers[active].ot, OT_SIZE);
    for (int row = 0; row < VIEW_ROWS; row++) for (int col = 0; col < VIEW_COLS; col++) {
        int x = VIEW_X + col * CELL_SIZE, y = VIEW_Y + row * CELL_SIZE;
        Cell c = score_at(&e->score, camera_x + col, camera_y + row);
        if (c.kind == CELL_EMPTY) {
            rect(7,x+CELL_SIZE/2,y+CELL_SIZE/2,1,1,UI_DOT);
            continue;
        }
        const Lane *l = &e->score.lanes[c.lane];
        // Logical origin, not camera position, fixes the phase under scrolling.
        // Clip to the head/end centers as well as the viewport's whole cells.
        int offset = (camera_x + col - l->x) * CELL_SIZE;
        for (int dx=0;dx<CELL_SIZE;dx++) {
            int logical = offset+dx-CELL_SIZE/2;
            if (logical >= 0 && logical <= (l->length+1)*CELL_SIZE && logical%4 == 0)
                rect(6,x+dx,y+CELL_SIZE/2,1,1,UI_RAIL);
        }
        int kind = c.kind == CELL_HEAD ? 0 : c.kind == CELL_END ? 7 :
            c.kind == CELL_STEP ? 8 : l->tiles[c.step];
        tile(5,x,y,kind);
    }
    if (e->mode == EDIT_LENGTH) {
        const Lane *l = &e->score.lanes[e->lane];
        int col = l->x + e->candidate + 1 - camera_x;
        int row = l->y - camera_y;
        if (row >= 0 && row < VIEW_ROWS) {
            // An edge marker keeps off-screen candidate endpoints visible.
            int visible = clamp(col, VIEW_COLS - 1);
            int x = VIEW_X + visible * CELL_SIZE, y = VIEW_Y + row * CELL_SIZE;
            // A dashed rectangle differs from the cursor's corner brackets.
            for (int d=1;d<CELL_SIZE-1;d+=3) {
                rect(4,x+d,y,2,1,UI_INK); rect(4,x+d,y+CELL_SIZE-1,2,1,UI_INK);
                rect(4,x,y+d,1,2,UI_INK); rect(4,x+CELL_SIZE-1,y+d,1,2,UI_INK);
            }
            if (col != visible) text(x+4,y+4,col < 0 ? "<" : ">");
        }
    }
    int cx = VIEW_X + (e->x-camera_x)*CELL_SIZE;
    int cy = VIEW_Y + (e->y-camera_y)*CELL_SIZE;
    cursor(cx,cy);
    char line[64];
    const char *kinds[] = {"EMPTY","HEAD","STEP","TILE","END"};
    Cell cell = score_at(&e->score,e->x,e->y);
    snprintf(line,sizeof(line),"PSX GRID   %03d,%02d  %s",e->x,e->y,kinds[cell.kind]);
    text(8,8,line);
    int count = 0;
    for (int i=0;i<SCORE_LANES;i++) count += e->score.lanes[i].active;
    snprintf(line,sizeof(line),"LANES %02d/16   VIEW %03d,%02d",count,camera_x,camera_y);
    text(8,20,line);
    if (e->mode != EDIT_PLANE) {
        EditorAction items[3]; int n = editor_menu(e,items);
        int width = 0, height = e->mode == EDIT_PICKER ? 136 : 60;
        if (e->mode == EDIT_MENU) {
            for (int i=0;i<n;i++) {
                int w = text_width(editor_action_label(items[i])) + 24;
                if (w > width) width = w;
            }
        } else if (e->mode == EDIT_PICKER) {
            for (int k=TILE_NOTE;k<TILE_KIND_COUNT;k++) {
                int w = text_width(score_tile_label((TileKind)k)) + 48;
                if (w > width) width = w;
            }
        } else width = text_width("DELETE LANE + TILES?") + 16;
        int px = clamp(cx+CELL_SIZE+2, SCREEN_W-width-8);
        int py = clamp(cy+CELL_SIZE+2, 192-height);
        rect(2,px,py,width,height,UI_PANEL);
        outline(1,px,py,width,height,UI_BORDER);
        if (e->mode == EDIT_MENU) {
            for (int i=0;i<n;i++) {
                snprintf(line,sizeof(line),"%c %s",i==e->selected?'>':' ',editor_action_label(items[i]));
                text(px+6,py+8+i*16,line);
            }
        } else if (e->mode == EDIT_PICKER) {
            text(px+8,py+7,"PLACE TILE");
            // All samples use the same full tile sprite as the plane. The last
            // row is a separate preview, never a temporary write to Score.
            for (int k=TILE_NOTE;k<TILE_KIND_COUNT;k++) {
                int y = py+20+(k-1)*16;
                tile(0,px+17,y,k);
                if ((int)e->tile_candidate == k) text(px+7,y+4,">");
                text(px+39,y+4,score_tile_label((TileKind)k));
            }
            text(px+8,py+122,"PREVIEW");
            tile(0,px+width-24,py+117,e->tile_candidate);
        } else if (e->mode == EDIT_LENGTH) {
            snprintf(line,sizeof(line),"LENGTH  < %02d >",e->candidate);
            text(px+8,py+8,line);
            snprintf(line,sizeof(line),"END %03d,%02d",e->score.lanes[e->lane].x+e->candidate+1,e->score.lanes[e->lane].y);
            text(px+8,py+24,line);
            text(px+8,py+40,"X APPLY  O CANCEL");
        } else {
            text(px+8,py+8,"DELETE LANE + TILES?");
            text(px+8,py+32,e->confirm?"  CANCEL  > DELETE":"> CANCEL    DELETE");
        }
    }
    text(8,200,connected ? e->message : "CONNECT PAD 1 / RELEASE BUTTONS");
    const char *guides[] = {"D-PAD MOVE    X MENU", "UP/DOWN SELECT  X OK  O BACK",
        "LEFT/RIGHT LENGTH  X OK  O BACK", "LEFT/RIGHT SELECT  X OK  O BACK",
        "UP/DOWN KIND  X PLACE  O BACK"};
    text(8,216,guides[e->mode]);
    text(8,228,"SCORE PLANE / GUI PROTOTYPE");
    // Added last at the highest depth: this executes before every sprite.
    DR_TPAGE *page = packet(sizeof(DR_TPAGE));
    if (page) {
        setDrawTPage(page,0,0,getTPage(0,0,640,0));
        addPrim(&buffers[active].ot[7],page);
    }
    DrawSync(0); VSync(0);
    PutDispEnv(&buffers[active ^ 1].disp);
    DrawOTagEnv(&buffers[active].ot[OT_SIZE-1], &buffers[active].draw);
    active ^= 1;
}
