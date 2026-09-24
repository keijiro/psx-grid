#include "render.h"
#include <psxgpu.h>
#include <psxetc.h>
#include <stdio.h>
#include <string.h>
#include <stddef.h>
#include "ui_style.h"
#include "ui_atlas.h"
// A full 20-by-15 view can contain 300 labeled tiles and their rail dots.
// Both buffers still fit in main RAM at the measured 64 KiB packet budget.
#define PACKET_BYTES 65536
#define OT_SIZE 8
// OT traverses high depths first and reverses insertion within each bucket.
// 7: texture setup/lattice, 6: rails, 5: tiles, 4: endpoint, 3: cursor,
// 2: panels and shadows, 1: underlines, 0: text.
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
    setRGB0(p,128,128,128); setClut(p,640,96);
    addPrim(&buffers[active].ot[depth],p);
}
static int glyph(unsigned char ch) { return ch >= 32 && ch <= 126 ? ch-32 : '?'-32; }
static void text(int x, int y, const char *s) {
    while (*s) {
        int i = glyph((unsigned char)*s++), advance = ui_advance[i];
        if (i != 0) sprite(0,x,y,(i%32)*8,24+(i/32)*8,advance-1,7);
        x += advance;
    }
}
static int text_width(const char *s) {
    int width=0;
    while(*s) width+=ui_advance[glyph((unsigned char)*s++)];
    return width;
}
static void clipped_text(int x,int y,const char *s,int right) {
    while(*s) {
        int i=glyph((unsigned char)*s++),advance=ui_advance[i];
        if(x+advance>right) break;
        if(i) sprite(0,x,y,(i%32)*8,24+(i/32)*8,advance-1,7);
        x+=advance;
    }
}
static void clipped_panel(int x,int y,int w,int h,int gray) {
    // Match the reference's diagonal cuts at native resolution. Scanline
    // strips keep the panel and its shadow on the same integer pixel edge.
    const int top=9,bottom=9;
    for(int i=0;i<top;i++) rect(2,x+top-i,y+i,w-top+i,1,gray);
    rect(2,x,y+top,w,h-top-bottom,gray);
    for(int i=0;i<bottom;i++) rect(2,x,y+h-bottom+i,w-i-1,1,gray);
}
static void menu_panel(int x,int y,int w,int h) {
    clipped_panel(x,y,w,h,UI_PANEL);
    clipped_panel(x+4,y+4,w,h,UI_SHADOW);
}
static void menu_row(const Editor *e,const EditorRow *row,int index,int x,int y,int width) {
    char value[48];
    editor_row_value(e,row->id,value,sizeof(value));
    int right=x+width;
    int value_x=right-text_width(value);
    clipped_text(x,y,row->label,value[0]?value_x-6:right);
    if(value[0]) clipped_text(value_x,y,value,right);
    if(index==e->selected) rect(1,x,y+9,width,1,UI_INK);
}
static void sound_menu(const Editor *e,const EditorRow *rows,int x,int y,int w) {
    // The reference's AMP/MIX pair fits beside each other at atlas size;
    // the extra pitch and reverb fields continue below it within 240 lines.
    static const int yy[]={29,43,57,75,89,103,75,89,103,123,137,151,165};
    const int left=x+15,right=x+151,col=118;
    for(int i=0;i<13;i++) {
        int sx=(i>=6 && i<=8)?right:left;
        int sy=y+yy[i];
        if(rows[i].kind==ROW_HEADING) {
            int rule_end=(i==6)?x+w-15:(i==3)?x+133:x+w-15;
            clipped_text(sx,sy,rows[i].label,rule_end);
            rect(1,sx,sy+10,62,1,UI_RULE);
        } else menu_row(e,&rows[i],i,sx,sy,(i>=4 && i<=8)?col:w-30);
    }
}
static void draw_menu(const Editor *e) {
    EditorRow rows[EDITOR_ROWS];
    int count=editor_rows(e,rows);
    int sound=e->mode==EDIT_SOUND;
    int pattern=e->mode==EDIT_PATTERN;
    int picker=e->mode==EDIT_PICKER;
    int confirm=e->mode==EDIT_DELETE;
    int width=sound?286:pattern?238:confirm?226:picker?220:0;
    char value[48];
    if(!width) {
        int need=0;
        for(int i=0;i<count;i++) {
            editor_row_value(e,rows[i].id,value,sizeof(value));
            int row_width=text_width(rows[i].label)+text_width(value)+(value[0]?24:0);
            if(row_width>need) need=row_width;
        }
        width=need+30;
        if(width<194) width=194;
        if(width>286) width=286;
    }
    int visible=count>9?9:count;
    int alert=e->message && e->message[0];
    // Leave ten pixels below the last selection underline, rather than
    // reserving a full unused row. Status and message lines add their own space.
    int bottom=35+(visible-1)*UI_MENU_ROW+10;
    if(sound) bottom=165+10;
    else if(pattern) bottom=39+((e->score.tiles[e->target].value.period-1)/8)*18+10;
    else if(picker) bottom=32+(TILE_KIND_COUNT-2)*UI_MENU_ROW+10;
    else if(confirm) bottom=11+7;
    int height=bottom+10;
    if(e->mode==EDIT_MAIN) height+=UI_MENU_ROW;
    if(alert) height+=14;
    int x=(SCREEN_W-width)/2,y=(SCREEN_H-height)/2;
    menu_panel(x,y,width,height);
    char title[40];
    if(e->mode==EDIT_MAIN) strcpy(title,"JACQUARD / MAIN");
    else if(e->mode==EDIT_REVERB) strcpy(title,"REVERB / GLOBAL");
    else if(sound) snprintf(title,sizeof(title),"SOUND CH %d",e->sound_channel+1);
    else if(pattern) strcpy(title,"CYCLE PATTERN");
    else if(picker) strcpy(title,"CREATE TILE");
    else if(confirm) strcpy(title,e->target?"DELETE JUMP BRANCH?":
        e->score.lanes[e->lane].source?"DELETE BRANCH LANE?":"DELETE LANE?");
    else strcpy(title,"MENU");
    clipped_text(x+UI_MENU_MARGIN,y+11,title,x+width-UI_MENU_MARGIN);
    if(sound) sound_menu(e,rows,x,y,width);
    else if(pattern) {
        TileValue v=e->score.tiles[e->target].value;
        for(int i=0;i<v.period;i++) {
            int gx=x+16+(i%8)*26,gy=y+39+(i/8)*18;
            char digit[2]={v.pattern&((uint32_t)1<<i)?'1':'0',0};
            text(gx+8,gy,digit);
            if(i==e->pattern_cursor) rect(1,gx+5,gy+9,13,1,UI_INK);
        }
    } else if(picker) {
        for(int k=1;k<TILE_KIND_COUNT;k++) {
            int py=y+32+(k-1)*UI_MENU_ROW;
            clipped_text(x+16,py,score_tile_label((TileKind)k),x+width-16);
            if(k==(int)e->tile_candidate) rect(1,x+16,py+9,width-32,1,UI_INK);
        }
    } else if(!confirm) {
        int first=0;
        if(e->selected>=visible) first=e->selected-visible+1;
        if(first>count-visible) first=count-visible;
        for(int i=first;i<first+visible;i++) {
            int py=y+35+(i-first)*UI_MENU_ROW;
            menu_row(e,&rows[i],i,x+15,py,width-30);
        }
        if(e->mode==EDIT_MAIN) {
            char status[64];
            snprintf(status,sizeof(status),"%s",storage_message(e->slot_status));
            if(e->card_free>=0) snprintf(status,sizeof(status),"%s  %d BLK",storage_message(e->slot_status),e->card_free);
            clipped_text(x+15,y+height-(alert?31:17),status,x+width-15);
        }
    }
    if(alert) clipped_text(x+15,y+height-17,e->message,x+width-15);
}
static void tile(int depth, int x, int y, int kind) {
    sprite(depth,x,y,kind*16,0,16,16);
}
static void cursor(int x, int y) {
    // The body spans x+2..14 and y+1..14. Brackets at x+1..15 and
    // y+0..15 touch its straight edges without covering the body.
    const int width = 15, height = 16, arm = 4;
    x++;
    for (int j=0;j<2;j++) for (int i=0;i<2;i++) {
        rect(3,x+i*(width-arm),y+j*(height-1),arm,1,UI_INK);
        rect(3,x+i*(width-1),y+j*(height-arm),1,arm,UI_INK);
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
    // 4-bit texels occupy 64x96 VRAM words at x=640. CLUT sits immediately
    // below; neither overlaps framebuffers (x=0..319, y=0..479), and UVs
    // stay inside one texture page. Index zero alone is transparent.
    RECT atlas = {640,0,64,96}, clut = {640,96,16,1};
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
static void small(int x,int y,const char *s,int dark,int shift,int first_shift) {
    const char *chars="0123456789ABCDEFG+LR%h";
    // Tight spacing after the compact plus keeps sharp labels inside the tile body.
    int width=0,advance=0,compact=0;
    for(const char *p=s;*p;p++) {
        advance=*p=='+' || (compact && *p>='0' && *p<='9')?3:4;
        width+=advance;
        compact=*p=='+' || (compact && *p>='0' && *p<='9');
    }
    if(!width) return;
    width-=advance-3;
    int inset=(CELL_SIZE-width)/2;
    x+=(inset<2?2:inset)+shift;
    compact=0;
    while(*s) {
        char ch=*s++;
        const char *p=strchr(chars,ch);
        if(p) sprite(5,x+first_shift,y,(int)(p-chars)*4,dark?56:48,3,5);
        first_shift=0;
        x+=ch=='+' || (compact && ch>='0' && ch<='9')?3:4;
        compact=ch=='+' || (compact && ch>='0' && ch<='9');
    }
}
static void draw_cell(const Score *s,Cell c,int x,int y) {
    if(c.kind==CELL_EMPTY) return;
    int kind=c.kind==CELL_HEAD?(s->lanes[c.lane].source?5:0):c.kind==CELL_END?7:c.kind==CELL_STEP?8:s->tiles[c.tile].value.kind==TILE_RELATIVE?6:s->tiles[c.tile].value.kind;
    char label[16]="";
    int shift=0,first_shift=0;
    if(c.kind==CELL_HEAD) {
        if(s->lanes[c.lane].source) strcpy(label,"B");
        else { snprintf(label,sizeof(label),"Ch%d",score_channel(s,c.lane)+1); shift=1; }
    }
    if(c.kind==CELL_TILE) {
        TileValue v=s->tiles[c.tile].value;
        if(v.kind==TILE_NOTE) {
            snprintf(label,sizeof(label),"%s%d",score_note_name(v.pitch),v.pitch/12);
            for(char *p=label;*p;p++) if(*p=='#') *p='+';
            // The plus and octave already align; only the note letter needs a nudge.
            if(strchr(label,'+')) first_shift=1;
            else shift=1;
        }
        if(v.kind==TILE_RELATIVE) snprintf(label,sizeof(label),"%s%s",v.lock_mask&LOCK_ATTACK?"A":"",v.lock_mask&LOCK_RELEASE?"R":"");
        if(v.kind==TILE_CYCLE) snprintf(label,sizeof(label),"C%d",v.period);
        if(v.kind==TILE_PROBABILITY) snprintf(label,sizeof(label),"%d",v.chance);
    }
    // Labels share the tile bucket so later panels cover them completely.
    small(x,y+5,label,c.kind==CELL_HEAD,shift,first_shift);
    tile(5,x,y,kind);
}
static int screen_x(int x) { return VIEW_X+clamp(x-camera_x,VIEW_COLS-1)*CELL_SIZE; }
static int screen_y(int y) { return VIEW_Y+clamp(y-camera_y,VIEW_ROWS-1)*CELL_SIZE; }
static int visible(int x,int y) { return x>=camera_x && x<camera_x+VIEW_COLS && y>=camera_y && y<camera_y+VIEW_ROWS; }
static void marker(int x,int y,int gray) { outline(4,screen_x(x)+1,screen_y(y)+1,14,14,gray); }
void render_frame(const Editor *e, int connected) {
    camera_x=follow(camera_x,e->x,VIEW_COLS,SCORE_WIDTH);
    camera_y=follow(camera_y,e->y,VIEW_ROWS,SCORE_HEIGHT);
    used=0; ClearOTagR(buffers[active].ot,OT_SIZE);
    for(int row=0;row<VIEW_ROWS;row++) for(int col=0;col<VIEW_COLS;col++) {
        int x=VIEW_X+col*CELL_SIZE,y=VIEW_Y+row*CELL_SIZE;
        Cell c=score_at(&e->score,camera_x+col,camera_y+row);
        rect(7,x+8,y+8,1,1,UI_DOT);
        if(c.kind==CELL_EMPTY) continue;
        const Lane *l=&e->score.lanes[c.lane];
        if(!c.depth) for(int dx=0;dx<CELL_SIZE;dx+=4) {
            int logical=(camera_x+col-l->x)*CELL_SIZE+dx-8;
            if(logical>=0 && logical<=(l->length+1)*CELL_SIZE) rect(6,x+dx,y+8,1,1,UI_RAIL);
        }
        if(c.depth) rect(6,x+8,y,1,16,UI_RAIL);
        draw_cell(&e->score,c,x,y);
    }
    for(int i=0;i<SCORE_LANES;i++) if(e->score.lanes[i].active) {
        const Lane *l=&e->score.lanes[i];
        for(int j=0;j<l->length;j++) {
            int d=0;
            for(TileId t=l->tiles[j];t;t=e->score.tiles[t].next,d++) if(e->score.tiles[t].value.kind==TILE_JUMP) {
                const Lane *b=&e->score.lanes[e->score.tiles[t].branch];
                int sx=l->x+j+1,sy=l->y+d;
                if(!visible(sx,sy) && !visible(b->x,b->y)) continue;
                int x1=screen_x(sx)+8,y1=screen_y(sy)+8,x2=screen_x(b->x)+8,y2=screen_y(b->y)+8;
                rect(6,x1< x2?x1:x2,y1,(x1<x2?x2-x1:x1-x2)+1,1,UI_BORDER);
                rect(6,x2,y1<y2?y1:y2,1,(y1<y2?y2-y1:y1-y2)+1,UI_BORDER);
                if(!visible(b->x,b->y)) marker(b->x,b->y,UI_INK);
                if(!visible(sx,sy)) marker(sx,sy,UI_INK);
            }
        }
    }
    if(e->mode==EDIT_MOVE) {
        MovePlan p=score_plan_move(&e->score,e->source_x,e->source_y,e->x,e->y);
        Cell source=score_at(&e->score,e->source_x,e->source_y);
        int gray=p.result==SCORE_OK?UI_INK:UI_RAIL;
        marker(e->source_x,e->source_y,UI_BORDER);
        for(int row=0;row<VIEW_ROWS;row++) for(int col=0;col<VIEW_COLS;col++) {
            int x=camera_x+col,y=camera_y+row;
            Cell c=score_at(&e->score,x-e->x+e->source_x,y-e->y+e->source_y);
            int carried=source.kind==CELL_HEAD?c.lane==source.lane:
                c.kind==CELL_TILE && c.lane==source.lane && c.step==source.step && c.depth>=source.depth;
            Cell dest=score_resolve(&e->score,e->x,e->y);
            if(source.kind==CELL_TILE && dest.lane==source.lane && dest.step==source.step)
                carried=x==e->x && y==e->y;
            if(carried) outline(4,VIEW_X+col*16+2,VIEW_Y+row*16+2,12,12,gray);
        }
    }
    int cx=screen_x(e->x),cy=screen_y(e->y); cursor(cx,cy);
    if(e->mode!=EDIT_PLANE && e->mode!=EDIT_MOVE) draw_menu(e);
    (void)connected;
    DR_TPAGE *page=packet(sizeof(DR_TPAGE));
    if(page) { setDrawTPage(page,0,0,getTPage(0,0,640,0)); addPrim(&buffers[active].ot[7],page); }
    DrawSync(0); VSync(0); PutDispEnv(&buffers[active^1].disp);
    DrawOTagEnv(&buffers[active].ot[OT_SIZE-1],&buffers[active].draw); active^=1;
}
