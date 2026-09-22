#include "render.h"
#include <psxgpu.h>
#include <psxetc.h>
#include <stdio.h>
#include <string.h>
#include <stddef.h>
#define SCREEN_W 320
#define SCREEN_H 240
#define CELL_SIZE 16
#define VIEW_COLS 19
#define VIEW_ROWS 10
#define VIEW_X 8
#define VIEW_Y 32
#define PACKET_BYTES 32768
#define OT_SIZE 8
// OT draws high depths first; text is always above its panel.
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
static void rect(int depth, int x, int y, int w, int h, int r, int g, int b) {
    TILE *p = packet(sizeof(TILE));
    if (!p) return;
    setTile(p); setXY0(p, x, y); setWH(p, w, h); setRGB0(p, r, g, b);
    addPrim(&buffers[active].ot[depth], p);
}
static void outline(int depth, int x, int y, int w, int h, int r, int g, int b) {
    rect(depth,x,y,w,1,r,g,b); rect(depth,x,y+h-1,w,1,r,g,b);
    rect(depth,x,y,1,h,r,g,b); rect(depth,x+w-1,y,1,h,r,g,b);
}
static void text(int x, int y, const char *s) {
    // FntSort emits at most one SPRT_8 per byte plus texture-page setup.
    size_t reserved = strlen(s) * sizeof(SPRT_8) + 2 * sizeof(DR_TPAGE);
    void *p = packet(reserved);
    if (p) FntSort(&buffers[active].ot[0], p, x, y, s);
}
static int clamp(int v, int max) { return v < 0 ? 0 : v > max ? max : v; }
static int follow(int camera, int cursor, int size, int bound) {
    if (cursor < camera + 2) camera = cursor - 2;
    if (cursor >= camera + size - 2) camera = cursor - size + 3;
    return clamp(camera, bound - size);
}
void render_init(void) {
    ResetGraph(0); SetVideoMode(MODE_NTSC); FntLoad(960, 0);
    for (int i = 0; i < 2; i++) {
        SetDefDrawEnv(&buffers[i].draw, 0, i * SCREEN_H, SCREEN_W, SCREEN_H);
        SetDefDispEnv(&buffers[i].disp, 0, i * SCREEN_H, SCREEN_W, SCREEN_H);
        buffers[i].draw.isbg = 1;
        setRGB0(&buffers[i].draw, 12, 17, 28);
        ClearOTagR(buffers[i].ot, OT_SIZE);
    }
    SetDispMask(1);
}
void render_frame(const Editor *e, int connected) {
    camera_x = follow(camera_x, e->x, VIEW_COLS, SCORE_WIDTH);
    camera_y = follow(camera_y, e->y, VIEW_ROWS, SCORE_HEIGHT);
    used = 0;
    ClearOTagR(buffers[active].ot, OT_SIZE);
    for (int col = 0; col <= VIEW_COLS; col++)
        rect(7, VIEW_X + col * CELL_SIZE, VIEW_Y, 1, VIEW_ROWS * CELL_SIZE, 28, 37, 50);
    for (int row = 0; row <= VIEW_ROWS; row++)
        rect(7, VIEW_X, VIEW_Y + row * CELL_SIZE, VIEW_COLS * CELL_SIZE, 1, 28, 37, 50);
    for (int row = 0; row < VIEW_ROWS; row++) for (int col = 0; col < VIEW_COLS; col++) {
        int x = VIEW_X + col * CELL_SIZE, y = VIEW_Y + row * CELL_SIZE;
        Cell c = score_at(&e->score, camera_x + col, camera_y + row);
        if (c.kind == CELL_EMPTY) continue;
        rect(6, x, y+7, CELL_SIZE, 2, 70, 106, 122);
        if (c.kind == CELL_HEAD) {
            rect(5,x+3,y+3,10,10,62,197,170);
            rect(4,x+6,y+6,4,4,12,40,45);
        } else if (c.kind == CELL_END) {
            rect(5,x+6,y+2,3,12,231,139,83);
        } else if (c.kind == CELL_TILE) {
            rect(5,x+3,y+3,10,10,107,142,236);
            rect(4,x+6,y+6,4,4,202,220,255);
        } else rect(5,x+7,y+6,2,4,121,158,175);
    }
    if (e->mode == EDIT_LENGTH) {
        const Lane *l = &e->score.lanes[e->lane];
        int col = l->x + e->candidate + 1 - camera_x;
        int row = l->y - camera_y;
        if (row >= 0 && row < VIEW_ROWS) {
            // An edge marker keeps off-screen candidate endpoints visible.
            int visible = clamp(col, VIEW_COLS - 1);
            int x = VIEW_X + visible * CELL_SIZE, y = VIEW_Y + row * CELL_SIZE;
            outline(3,x+1,y+1,14,14,241,202,77);
            if (col != visible) text(x+4,y+4,col < 0 ? "<" : ">");
        }
    }
    int cx = VIEW_X + (e->x-camera_x)*CELL_SIZE;
    int cy = VIEW_Y + (e->y-camera_y)*CELL_SIZE;
    outline(2,cx,cy,16,16,250,244,208);
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
        int px = clamp(cx+18, SCREEN_W-180), py = clamp(cy+18, 132);
        rect(1,px,py,176,60,25,38,58);
        if (e->mode == EDIT_MENU) {
            EditorAction items[3]; int n = editor_menu(e,items);
            for (int i=0;i<n;i++) {
                snprintf(line,sizeof(line),"%c %s",i==e->selected?'>':' ',editor_action_label(items[i]));
                text(px+6,py+8+i*16,line);
            }
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
        "LEFT/RIGHT LENGTH  X OK  O BACK", "LEFT/RIGHT SELECT  X OK  O BACK"};
    text(8,216,guides[e->mode]);
    text(8,228,"SCORE PLANE / GUI PROTOTYPE");
    DrawSync(0); VSync(0);
    PutDispEnv(&buffers[active ^ 1].disp);
    DrawOTagEnv(&buffers[active].ot[OT_SIZE-1], &buffers[active].draw);
    active ^= 1;
}
