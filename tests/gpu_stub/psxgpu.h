#ifndef GPU_STUB_H
#define GPU_STUB_H
#include <stdint.h>
#include <stddef.h>
typedef struct { uint32_t tag; uint8_t r0,g0,b0,code; int16_t x0,y0,w,h; } TILE;
typedef struct { uint32_t tag; uint8_t r0,g0,b0,code; int16_t x0,y0; uint8_t u0,v0; uint16_t clut; int16_t w,h; } SPRT;
typedef struct { uint32_t tag, code; } DR_TPAGE;
typedef struct { int16_t x,y,w,h; } RECT;
typedef struct { int r0,g0,b0,isbg; } DRAWENV;
typedef struct { int unused; } DISPENV;
#define setTile(p) ((p)->code=0x60)
#define setSprt(p) ((p)->code=0x64)
#define setXY0(p,x,y) ((p)->x0=(x),(p)->y0=(y))
#define setWH(p,width,height) ((p)->w=(width),(p)->h=(height))
#define setUV0(p,u,v) ((p)->u0=(u),(p)->v0=(v))
#define setRGB0(p,r,g,b) ((p)->r0=(r),(p)->g0=(g),(p)->b0=(b))
#define getTPage(tp,abr,x,y) (((x)>>6)|(((y)&256)>>4)|((tp)<<7)|((abr)<<5))
#define setClut(p,x,y) ((p)->clut=((y)<<6)|((x)>>4))
#define setDrawTPage(p,dfe,dtd,page) ((p)->code=0xe1000000|(page))
void addPrim(uint32_t *ot, void *p);
void ResetGraph(int mode);
int LoadImage(const RECT *rect,const uint32_t *data);
void SetDefDrawEnv(DRAWENV *p,int x,int y,int w,int h);
void SetDefDispEnv(DISPENV *p,int x,int y,int w,int h);
void ClearOTagR(uint32_t *p,int n);
void SetDispMask(int mode);
void DrawSync(int mode);
void PutDispEnv(DISPENV *p);
void DrawOTagEnv(uint32_t *p,DRAWENV *e);
#endif
