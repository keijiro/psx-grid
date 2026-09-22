#include "render.h"
#include <psxgpu.h>
#include <assert.h>
#include <stdio.h>
#include <string.h>
extern volatile unsigned render_packet_peak, render_overflows;
void addPrim(uint32_t *ot,TILE *p) {
    (void)ot;
    assert(p->x0>=0 && p->y0>=0 && p->w>0 && p->h>0);
    assert(p->x0+p->w<=320 && p->y0+p->h<=240);
}
void *FntSort(uint32_t *ot,void *p,int x,int y,const char *s) {
    (void)ot; assert(x>=0 && y>=0 && x+strlen(s)*8<=320 && y+8<=240);
    size_t n=strlen(s)*sizeof(SPRT_8)+sizeof(DR_TPAGE);
    memset(p,0,n); return (char *)p+n;
}
void ResetGraph(int mode) {(void)mode;}
void SetVideoMode(int mode) {(void)mode;}
void SetDispMask(int mode) {(void)mode;}
void DrawSync(int mode) {(void)mode;}
void VSync(int mode) {(void)mode;}
void FntLoad(int x,int y) {(void)x;(void)y;}
void SetDefDrawEnv(DRAWENV *p,int x,int y,int w,int h) {(void)p;(void)x;(void)y;(void)w;(void)h;}
void SetDefDispEnv(DISPENV *p,int x,int y,int w,int h) {(void)p;(void)x;(void)y;(void)w;(void)h;}
void ClearOTagR(uint32_t *p,int n) {memset(p,0,n*sizeof(*p));}
void PutDispEnv(DISPENV *p) {(void)p;}
void DrawOTagEnv(uint32_t *p,DRAWENV *e) {(void)p;(void)e;}
int main(void) {
    Editor e; editor_init(&e); render_init();
    for(int i=0;i<16;i++) {
        assert(score_create(&e.score,0,i,64)==SCORE_OK);
        for(int x=1;x<=64;x++) assert(score_tile(&e.score,x,i,1)==SCORE_OK);
    }
    e.message=score_message(SCORE_COLLISION);
    for(int y=0;y<64;y++) for(int x=0;x<128;x++) {
        e.x=x; e.y=y;
        for(int mode=EDIT_PLANE;mode<=EDIT_DELETE;mode++) {
            e.mode=mode; e.lane=0; e.candidate=64;
            render_frame(&e,1);
        }
    }
    render_frame(&e,0);
    assert(render_overflows==0);
    printf("PASS: 32769 render frames; peak %u / 32768 bytes; no overflow or screen escape\n",render_packet_peak);
}
