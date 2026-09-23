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
static void small(int x,int y,const char *s,int dark) {
    const char *chars="0123456789ABCDEFG#LR%";
    while(*s) {
        const char *p=strchr(chars,*s++);
        if(p) sprite(5,x,y,(int)(p-chars)*4,dark?56:48,3,5);
        x+=4;
    }
}
static void draw_cell(const Score *s,Cell c,int x,int y) {
    if(c.kind==CELL_EMPTY) return;
    int kind=c.kind==CELL_HEAD?(s->lanes[c.lane].source?5:0):c.kind==CELL_END?7:c.kind==CELL_STEP?8:s->tiles[c.tile].value.kind==TILE_RELATIVE?6:s->tiles[c.tile].value.kind;
    char label[16]="";
    if(c.kind==CELL_HEAD) strcpy(label,s->lanes[c.lane].source?"B":"L");
    if(c.kind==CELL_TILE) {
        TileValue v=s->tiles[c.tile].value;
        if(v.kind==TILE_NOTE) snprintf(label,sizeof(label),"%s%d",score_note_name(v.pitch),v.pitch/12);
        if(v.kind==TILE_RELATIVE) snprintf(label,sizeof(label),"%s%s",v.lock_mask&LOCK_ATTACK?"A":"",v.lock_mask&LOCK_RELEASE?"R":"");
        if(v.kind==TILE_CYCLE) snprintf(label,sizeof(label),"C%d",v.period);
        if(v.kind==TILE_PROBABILITY) snprintf(label,sizeof(label),"%d",v.chance);
    }
    // Labels share the tile bucket so later panels cover them completely.
    small(x+4,y+5,label,c.kind==CELL_HEAD);
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
    if(e->mode==EDIT_LENGTH) {
        const Lane *l=&e->score.lanes[e->lane]; marker(l->x+e->candidate+1,l->y,UI_INK);
    }
    const char *status=e->message;
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
        status=p.result==SCORE_OK?"DROP OK":"INVALID DROP";
    }
    int cx=screen_x(e->x),cy=screen_y(e->y); cursor(cx,cy);
    char line[64];
    snprintf(line,sizeof(line),"PSX GRID   %03d,%02d",e->x,e->y); text(8,8,line);
    Cell cell=score_at(&e->score,e->x,e->y);
    if(cell.lane>=0) snprintf(line,sizeof(line),"LANE %02d  DIV 1/%d",cell.lane+1,score_division(&e->score,cell.lane));
    else snprintf(line,sizeof(line),"PITCH AND GATE SCORE");
    text(8,20,line);
    if(e->mode!=EDIT_PLANE && e->mode!=EDIT_MOVE) {
        EditorAction items[EDITOR_MENU_ITEMS]; int n=editor_menu(e,items);
        int width=208,height=e->mode==EDIT_MENU?n*16+16:e->mode==EDIT_PICKER?104:e->mode==EDIT_SOUND?112:editor_sound_parent(e->mode)==EDIT_SOUND?88:e->mode==EDIT_PATTERN?112:72;
        int px=clamp(cx+18,SCREEN_W-width-8),py=clamp(cy+18,192-height);
        rect(2,px,py,width,height,UI_PANEL); outline(1,px,py,width,height,UI_BORDER);
        if(e->mode==EDIT_MENU) for(int i=0;i<n;i++) {
            snprintf(line,sizeof(line),"%c %s",i==e->selected?'>':' ',editor_action_label(items[i])); text(px+6,py+8+i*16,line);
        }
        else if(e->mode==EDIT_PICKER) {
            text(px+8,py+8,"CREATE TILE");
            for(int k=1;k<TILE_KIND_COUNT;k++) {
                snprintf(line,sizeof(line),"%c %s",k==(int)e->tile_candidate?'>':' ',score_tile_label((TileKind)k)); text(px+8,py+8+k*16,line);
            }
        } else if(e->mode==EDIT_SOUND) {
            text(px+8,py+8,"SOUND / GLOBAL");
            static const char *groups[]={"WAVES","AMPLITUDE","MIX","PITCH","BACK"};
            for(int i=0;i<5;i++) {
                snprintf(line,sizeof(line),"%c %s",e->selected==i?'>':' ',groups[i]); text(px+8,py+28+i*16,line);
            }
        } else if(editor_sound_parent(e->mode)==EDIT_SOUND) {
            const SoundSettings *s=&e->score.sound;
            text(px+8,py+8,e->mode==EDIT_WAVES?"WAVES":e->mode==EDIT_AMPLITUDE?"AMPLITUDE":e->mode==EDIT_MIX?"MIX":"PITCH");
            for(int i=0;i<2;i++) {
                char marker=e->selected==i?'>':' ';
                if(e->mode==EDIT_WAVES) snprintf(line,sizeof(line),"%c WAVE %c %s",marker,'A'+i,score_wave_name(i?s->wave_b:s->wave_a));
                else if(e->mode==EDIT_AMPLITUDE) snprintf(line,sizeof(line),"%c AMP %s %d MS",marker,i?"RELEASE":"ATTACK",i?s->release:s->attack);
                else if(e->mode==EDIT_MIX) snprintf(line,sizeof(line),"%c MIX %s %d MS",marker,i?"RELEASE":"ATTACK",i?s->mix_release:s->mix_attack);
                else if(!i) snprintf(line,sizeof(line),"%c SWEEP %+d ST",marker,s->sweep);
                else snprintf(line,sizeof(line),"%c DECAY %d MS",marker,s->decay);
                text(px+8,py+28+i*16,line);
            }
            text(px+8,py+64,e->selected==2?"> BACK":"  BACK");
        } else if(e->mode==EDIT_DELETE) {
            text(px+8,py+8,"DELETE LANE / BRANCH TILES?");
            text(px+8,py+28,e->confirm?"  CANCEL":"> CANCEL"); text(px+8,py+44,e->confirm?"> DELETE":"  DELETE");
        } else if(e->mode==EDIT_PATTERN) {
            text(px+8,py+8,"CYCLE PATTERN");
            for(int i=0;i<e->value.period;i++) {
                int x=px+8+(i%8)*24,y=py+26+(i/8)*16;
                snprintf(line,sizeof(line),"%c%c",i==e->pattern_cursor?'>':' ',(e->value.pattern&((uint32_t)1<<i))?'1':'0'); text(x,y,line);
            }
            text(px+8,py+94,e->pattern_cursor==e->value.period?"> APPLY":"  APPLY");
        } else {
            switch(e->mode) {
            case EDIT_WAVE_A: snprintf(line,sizeof(line),"WAVE A %s",score_wave_name(e->sound_candidate.wave_a)); break;
            case EDIT_WAVE_B: snprintf(line,sizeof(line),"WAVE B %s",score_wave_name(e->sound_candidate.wave_b)); break;
            case EDIT_MIX_ATTACK: snprintf(line,sizeof(line),"MIX ATTACK %d MS",e->sound_candidate.mix_attack); break;
            case EDIT_MIX_RELEASE: snprintf(line,sizeof(line),"MIX RELEASE %d MS",e->sound_candidate.mix_release); break;
            case EDIT_PITCH_SWEEP: snprintf(line,sizeof(line),"PITCH SWEEP %+d ST",e->sound_candidate.sweep); break;
            case EDIT_PITCH_DECAY: snprintf(line,sizeof(line),"PITCH DECAY %d MS",e->sound_candidate.decay); break;
            case EDIT_ATTACK: snprintf(line,sizeof(line),"AMP ATTACK %d MS",e->sound_candidate.attack); break;
            case EDIT_RELEASE: snprintf(line,sizeof(line),"AMP RELEASE %d MS",e->sound_candidate.release); break;
            case EDIT_LOCK_ATTACK_ENABLE: snprintf(line,sizeof(line),"ATTACK %s",e->value.lock_mask&LOCK_ATTACK?"ENABLED":"DISABLED"); break;
            case EDIT_LOCK_RELEASE_ENABLE: snprintf(line,sizeof(line),"RELEASE %s",e->value.lock_mask&LOCK_RELEASE?"ENABLED":"DISABLED"); break;
            case EDIT_LOCK_ATTACK: snprintf(line,sizeof(line),"ATTACK %+d MS %s",e->value.attack,e->value.lock_mask&LOCK_ATTACK?"":"OFF"); break;
            case EDIT_LOCK_RELEASE: snprintf(line,sizeof(line),"RELEASE %+d MS %s",e->value.release,e->value.lock_mask&LOCK_RELEASE?"":"OFF"); break;
            case EDIT_LENGTH: snprintf(line,sizeof(line),"LANE LENGTH  %d",e->candidate); break;
            case EDIT_DIVISION: snprintf(line,sizeof(line),"DIVISION  1/%d",score_divisions[e->candidate]); break;
            case EDIT_PITCH: snprintf(line,sizeof(line),"PITCH  %s%d",score_note_name(e->value.pitch),e->value.pitch/12); break;
            case EDIT_DURATION: snprintf(line,sizeof(line),"NOTE LENGTH  %d.%02d",e->value.length/20,e->value.length%20*5); break;
            case EDIT_PERIOD: snprintf(line,sizeof(line),"CYCLE PERIOD  %d",e->value.period); break;
            default: snprintf(line,sizeof(line),"CHANCE  %d / 100",e->value.chance); break;
            }
            text(px+8,py+8,line);
            text(px+8,py+28,e->mode==EDIT_ATTACK || e->mode==EDIT_RELEASE || e->mode==EDIT_LOCK_ATTACK || e->mode==EDIT_LOCK_RELEASE || e->mode==EDIT_MIX_ATTACK || e->mode==EDIT_MIX_RELEASE || e->mode==EDIT_PITCH_DECAY?"L/R 1 MS  U/D 100 MS":(e->mode==EDIT_PITCH || e->mode==EDIT_PITCH_SWEEP)?"L/R NOTE  U/D OCTAVE":e->mode==EDIT_DURATION?"L/R .05  U/D 1 STEP":"D-PAD CHANGE");
            text(px+8,py+48,"X APPLY  O DISCARD");
        }
    }
    text(8,200,connected?status:"CONNECT PAD 1 / RELEASE BUTTONS");
    text(8,216,e->mode==EDIT_PLANE?"X TAP MENU / HOLD + D-PAD MOVE":e->mode==EDIT_MOVE?"RELEASE X DROP  O CANCEL":"D-PAD SELECT  X OK  O BACK");
    text(8,228,e->playing?(e->snapshot_dirty?"PLAYING / APPLYING EDITS":"PLAYING / START TO STOP"):"STOPPED / START TO PLAY");
    DR_TPAGE *page=packet(sizeof(DR_TPAGE));
    if(page) { setDrawTPage(page,0,0,getTPage(0,0,640,0)); addPrim(&buffers[active].ot[7],page); }
    DrawSync(0); VSync(0); PutDispEnv(&buffers[active^1].disp);
    DrawOTagEnv(&buffers[active].ot[OT_SIZE-1],&buffers[active].draw); active^=1;
}
