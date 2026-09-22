#ifndef GPU_STUB_H
#define GPU_STUB_H
#include <stdint.h>
#include <stddef.h>
typedef struct { uint32_t tag; uint8_t r0,g0,b0,code; int16_t x0,y0,w,h; } TILE;
typedef struct { uint32_t words[4]; } SPRT_8;
typedef struct { uint32_t words[2]; } DR_TPAGE;
typedef struct { int r0,g0,b0,isbg; } DRAWENV;
typedef struct { int unused; } DISPENV;
#define setTile(p) ((p)->code=0x60)
#define setXY0(p,x,y) ((p)->x0=(x),(p)->y0=(y))
#define setWH(p,width,height) ((p)->w=(width),(p)->h=(height))
#define setRGB0(p,r,g,b) ((p)->r0=(r),(p)->g0=(g),(p)->b0=(b))
void addPrim(uint32_t *ot, TILE *p);
void ResetGraph(int mode);
void FntLoad(int x,int y);
void SetDefDrawEnv(DRAWENV *p,int x,int y,int w,int h);
void SetDefDispEnv(DISPENV *p,int x,int y,int w,int h);
void ClearOTagR(uint32_t *p,int n);
void SetDispMask(int mode);
void *FntSort(uint32_t *ot,void *p,int x,int y,const char *s);
void DrawSync(int mode);
void PutDispEnv(DISPENV *p);
void DrawOTagEnv(uint32_t *p,DRAWENV *e);
#endif
