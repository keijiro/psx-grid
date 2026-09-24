#include "render.h"
#include <psxgpu.h>
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "ui_style.h"

extern volatile unsigned render_packet_peak, render_overflows;
static uint32_t *current_ot;
static void *primitives[8][2048];
static int counts[8], frame_count;
static uint16_t vram[512][1024];
static uint8_t output[240][320];
static int capture;
_Static_assert(sizeof(TILE)==16 && sizeof(SPRT)==20 && sizeof(DR_TPAGE)==8, "SDK packet sizes");

void addPrim(uint32_t *ot,void *packet) {
    int depth=(int)(ot-current_ot);
    assert(depth>=0 && depth<8 && counts[depth]<2048);
    primitives[depth][counts[depth]++]=packet;
    if ((((DR_TPAGE *)packet)->code>>24)==0xe1) return;
    TILE *p=packet;
    assert(p->r0==p->g0 && p->g0==p->b0);
    int w=p->w,h=p->h;
    if(p->code==0x64) {
        SPRT *s=packet; w=s->w; h=s->h;
        assert(s->u0+w<=256 && s->v0+h<=96);
        assert(s->clut==((96<<6)|(640>>4)));
    } else assert(p->code==0x60);
    assert(p->x0>=0 && p->y0>=0 && w>0 && h>0);
    assert(p->x0+w<=320 && p->y0+h<=240);
}
int LoadImage(const RECT *r,const uint32_t *data) {
    assert(r->x==640 && ((r->y==0 && r->w==64 && r->h==96) || (r->y==96 && r->w==16 && r->h==1)));
    const uint16_t *src=(const uint16_t *)data;
    for(int y=0;y<r->h;y++) for(int x=0;x<r->w;x++) vram[r->y+y][r->x+x]=*src++;
    if(r->y==96) {
        assert(vram[96][640]==0);
        for(int i=1;i<8;i++) {
            uint16_t rgb=vram[96][640+i]; assert(rgb);
            assert((rgb&31)==((rgb>>5)&31) && (rgb&31)==((rgb>>10)&31));
        }
    }
    return 0;
}
void ResetGraph(int mode) {(void)mode;}
void SetVideoMode(int mode) {(void)mode;}
void SetDispMask(int mode) {(void)mode;}
void DrawSync(int mode) {(void)mode;}
void VSync(int mode) {(void)mode;}
void SetDefDrawEnv(DRAWENV *p,int x,int y,int w,int h) {(void)p;(void)x;(void)y;(void)w;(void)h;}
void SetDefDispEnv(DISPENV *p,int x,int y,int w,int h) {(void)p;(void)x;(void)y;(void)w;(void)h;}
void ClearOTagR(uint32_t *p,int n) {memset(p,0,n*sizeof(*p)); current_ot=p; memset(counts,0,sizeof(counts));}
void PutDispEnv(DISPENV *p) {(void)p;}
void DrawOTagEnv(uint32_t *p,DRAWENV *e) {
    assert(p==current_ot+7); frame_count++;
    assert(e->r0==e->g0 && e->g0==e->b0);
    if(capture) memset(output,(e->r0>>3)*255/31,sizeof(output));
    int page=-1;
    for(int depth=7;depth>=0;depth--) for(int i=counts[depth]-1;i>=0;i--) {
        void *packet=primitives[depth][i];
        if((((DR_TPAGE *)packet)->code>>24)==0xe1) {
            page=((DR_TPAGE *)packet)->code&0x1ff; assert(page==getTPage(0,0,640,0)); continue;
        }
        TILE *t=packet; SPRT *s=packet; int textured=t->code==0x64;
        if(textured) assert(page==getTPage(0,0,640,0));
        if(!capture) continue;
        int w=textured?s->w:t->w,h=textured?s->h:t->h;
        for(int y=0;y<h;y++) for(int x=0;x<w;x++) {
            int gray=(t->r0>>3)*255/31;
            if(textured) {
                int u=s->u0+x,v=s->v0+y;
                int index=(vram[v][640+u/4]>>((u%4)*4))&15;
                uint16_t rgb=vram[96][640+index]; if(!rgb) continue;
                assert((rgb&31)==((rgb>>5)&31) && (rgb&31)==((rgb>>10)&31)); gray=(rgb&31)*255/31;
            }
            output[t->y0+y][t->x0+x]=gray;
        }
    }
}
static void save(const char *path) {
    FILE *f=fopen(path,"wb"); assert(f);
    fprintf(f,"P5\n320 240\n255\n"); fwrite(output,1,sizeof(output),f); fclose(f);
}
static Editor e;
static void draw(const char *name) {
    render_frame(&e,1);
    if(name) { char path[256]; snprintf(path,sizeof(path),"build/tests/%s.pgm",name); save(path); }
}
static void sound_selection(int *x,int *y) {
    int found=0;
    for(int i=0;i<counts[1];i++) {
        TILE *p=primitives[1][i];
        if(p->w!=190) continue;
        *x=p->x0; *y=p->y0; found++;
    }
    assert(found==1);
}
static void assert_menu_edge(void) {
    for(int depth=0;depth<=2;depth++) for(int i=0;i<counts[depth];i++) {
        TILE *p=primitives[depth][i];
        int h=p->code==0x64?((SPRT *)p)->h:p->h;
        assert(p->y0>=UI_MENU_EDGE && p->y0+h<=SCREEN_H-UI_MENU_EDGE);
    }
}
static int tile_at(int x,int y) { return score_at(&e.score,x,y).tile; }

int main(void) {
    editor_init(&e); render_init(); capture=1;

    // A sparse plane must keep its single cursor separate from the lattice.
    e.x=10; e.y=7; draw("plane");
    uint8_t background=(0x16>>3)*255/31;
    for(int row=0;row<15;row++) for(int col=0;col<20;col++) {
        if(col==e.x && row==e.y) continue;
        int changed=0;
        for(int y=row*16;y<(row+1)*16;y++) for(int x=col*16;x<(col+1)*16;x++) changed+=output[y][x]!=background;
        assert(changed==1);
    }

    e.mode=EDIT_MAIN; e.selected=0; draw("main");
    e.selected=5; draw("main-last-row");
    for(int status=STORAGE_UNKNOWN;status<=STORAGE_GENERATION_FULL;status++) {
        e.slot_status=status; e.card_free=status%16; e.message=storage_message(status);
        e.selected=4; draw(NULL);
    }
    e.message="";
    e.mode=EDIT_REVERB; e.selected=1; draw("reverb");

    // Exercise the renderer's full 300-cell view; bypass model admission here
    // because the fixture intentionally exceeds persistence and lane limits.
    for(int lane=0;lane<SCORE_LANES;lane++) {
        Lane *l=&e.score.lanes[lane]; l->active=1; l->x=lane*8; l->y=0;
        l->length=64; l->division=16; l->channel=lane%SCORE_CHANNELS;
    }
    for(int i=0;i<SCORE_TILE_CAPACITY;i++) {
        TileValue v=score_default((TileKind)(1+i%3));
        v.pitch=108; v.length=1280; v.period=32; v.chance=100;
        v.lock_mask=3; v.attack=-16000; v.release=16000;
        e.score.tiles[i+1]=(Tile){v,0,-1};
    }
    for(int lane=0;lane<SCORE_LANES;lane++) for(int step=0;step<64;step++) {
        e.score.lanes[lane].tiles[step]=(TileId)(1+lane*64+step);
    }
    e.mode=EDIT_PLANE;
    for(int y=0;y<64;y++) for(int x=0;x<128;x++) {
        e.x=x; e.y=y;
        draw(NULL);
    }
    for(int corner=0;corner<4;corner++) {
        e.x=(corner&1)?127:0; e.y=(corner&2)?63:0;
        e.mode=EDIT_MENU; e.selected=0; draw("context-corner");
        e.mode=EDIT_SOUND; e.sound_channel=7; e.selected=12;
        e.score.sounds[7]=(SoundSettings){16000,16000,WAVE_TRIANGLE,WAVE_NOISE,500,500,-24,2000,1};
        draw("sound-corner");
        e.mode=EDIT_PICKER; draw("picker-corner");
        e.mode=EDIT_PATTERN; e.pattern_cursor=31; draw("pattern-corner");
        e.mode=EDIT_DELETE; e.target=tile_at(0,1); draw("delete-corner");
    }
    e.mode=EDIT_SOUND; e.x=1; e.y=1; e.sound_channel=7; e.selected=1;
    draw("sound-top");
    assert_menu_edge();
    int top_x,top_y,bottom_x,bottom_y;
    sound_selection(&top_x,&top_y);
    e.selected=12;
    draw("sound");
    assert_menu_edge();
    sound_selection(&bottom_x,&bottom_y);
    assert(top_x==bottom_x && top_y<bottom_y && bottom_y<240);
    e.mode=EDIT_PATTERN; e.pattern_cursor=31; draw("pattern");
    e.mode=EDIT_PICKER; draw("picker");
    e.mode=EDIT_DELETE; e.target=tile_at(0,1); draw("delete");

    assert(render_overflows==0);
    printf("PASS: %d render frames; peak %u / 65536 bytes; no overflow or screen escape\n",frame_count,render_packet_peak);
}
