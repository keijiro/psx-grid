#include "render.h"
#include <psxgpu.h>
#include <assert.h>
#include <stdio.h>
#include <string.h>
extern volatile unsigned render_packet_peak, render_overflows;
static uint32_t *current_ot;
static void *primitives[8][2048];
static int counts[8], frame_count;
static uint16_t vram[512][1024];
static uint8_t output[240][320];
static int capture;
_Static_assert(sizeof(TILE)==16 && sizeof(SPRT)==20 && sizeof(DR_TPAGE)==8, "SDK packet sizes");
void addPrim(uint32_t *ot,void *packet) {
    int depth = ot-current_ot;
    assert(depth>=0 && depth<8 && counts[depth]<2048);
    primitives[depth][counts[depth]++]=packet;
    if ((((DR_TPAGE *)packet)->code>>24)==0xe1) return;
    TILE *p=packet;
    assert(p->r0==p->g0 && p->g0==p->b0);
    int w=p->w,h=p->h;
    if(p->code==0x64) {
        SPRT *s=packet; w=s->w; h=s->h;
        assert(s->u0+w<=256 && s->v0+h<=64);
        assert(s->clut==((64<<6)|(640>>4)));
    } else assert(p->code==0x60);
    assert(p->x0>=0 && p->y0>=0 && w>0 && h>0);
    assert(p->x0+w<=320 && p->y0+h<=240);
}
int LoadImage(const RECT *r,const uint32_t *data) {
    assert(r->x==640 && ((r->y==0 && r->w==64 && r->h==64) || (r->y==64 && r->w==16 && r->h==1)));
    const uint16_t *src=(const uint16_t *)data;
    for(int y=0;y<r->h;y++) for(int x=0;x<r->w;x++) vram[r->y+y][r->x+x]=*src++;
    if(r->y==64) {
        assert(vram[64][640]==0);
        for(int i=1;i<8;i++) {
            uint16_t rgb=vram[64][640+i]; assert(rgb);
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
        TILE *t=packet; SPRT *s=packet;
        int textured=t->code==0x64;
        if(textured) assert(page==getTPage(0,0,640,0));
        if(!capture) continue;
        int w=textured?s->w:t->w,h=textured?s->h:t->h;
        for(int y=0;y<h;y++) for(int x=0;x<w;x++) {
            int gray=(t->r0>>3)*255/31;
            if(textured) {
                int u=s->u0+x,v=s->v0+y;
                int index=(vram[v][640+u/4]>>((u%4)*4))&15;
                uint16_t rgb=vram[64][640+index];
                if(!rgb) continue;
                assert((rgb&31)==((rgb>>5)&31) && (rgb&31)==((rgb>>10)&31));
                gray=(rgb&31)*255/31;
            }
            output[t->y0+y][t->x0+x]=gray;
        }
    }
}
static void save(const char *path) {
    FILE *f=fopen(path,"wb"); assert(f);
    fprintf(f,"P5\n320 240\n255\n"); fwrite(output,1,sizeof(output),f); fclose(f);
}
int main(void) {
    Editor e; editor_init(&e); render_init();
    for(int i=0;i<16;i++) {
        assert(score_create(&e.score,0,i,64)==SCORE_OK);
        for(int x=1;x<=64;x++) assert(score_place(&e.score,x,i,(TileKind)(1+x%6))==SCORE_OK);
    }
    e.message=score_message(SCORE_COLLISION);
    for(int y=0;y<64;y++) for(int x=0;x<128;x++) {
        e.x=x; e.y=y;
        for(int mode=EDIT_PLANE;mode<=EDIT_PICKER;mode++) {
            e.mode=mode; e.lane=0; e.candidate=64; e.tile_candidate=(TileKind)(1+(x%6));
            render_frame(&e,1);
        }
    }
    render_frame(&e,0);
    // Sparse lanes expose the maximum rail workload, including clipped heads.
    for(int i=0;i<SCORE_LANES;i++) memset(e.score.lanes[i].tiles,0,SCORE_STEPS);
    for(int y=0;y<64;y++) for(int x=0;x<128;x++) {
        e.x=x; e.y=y;
        for(int mode=EDIT_PLANE;mode<=EDIT_PICKER;mode++) {
            e.mode=mode; e.tile_candidate=(TileKind)(1+x%6); render_frame(&e,x%2);
        }
    }
    for(int corner=0;corner<4;corner++) for(int k=TILE_NOTE;k<TILE_KIND_COUNT;k++) {
        e.x=(corner&1)?127:0; e.y=(corner&2)?63:0;
        e.mode=EDIT_PICKER; e.tile_candidate=(TileKind)k; render_frame(&e,0);
    }
    editor_init(&e); assert(score_create(&e.score,1,1,10)==SCORE_OK);
    for(int k=TILE_NOTE;k<TILE_KIND_COUNT;k++) assert(score_place(&e.score,k+1,1,(TileKind)k)==SCORE_OK);
    render_frame(&e,1); e.x=7; e.y=1; capture=1; render_frame(&e,1); save("build/tests/plane.pgm");
    e.mode=EDIT_PICKER; e.x=8;
    for(int k=TILE_NOTE;k<TILE_KIND_COUNT;k++) { e.tile_candidate=(TileKind)k; render_frame(&e,1); }
    save("build/tests/picker.pgm");
    e.mode=EDIT_LENGTH; e.lane=0; e.candidate=64; render_frame(&e,1); save("build/tests/resize.pgm");
    assert(render_overflows==0);
    printf("PASS: %d render frames; peak %u / 32768 bytes; no overflow or screen escape\n",frame_count,render_packet_peak);
}
