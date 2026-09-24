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
                uint16_t rgb=vram[96][640+index];
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
static int changed_pixels(int x,int y,int w,int h,uint8_t background) {
    int changed=0;
    for(int py=y;py<y+h;py++) for(int px=x;px<x+w;px++) changed+=output[py][px]!=background;
    return changed;
}
static Editor e;
int main(void) {
    editor_init(&e); render_init();
    // An empty plane contains only the dot lattice and cursor. This catches
    // accidental restoration of persistent status or help text over the score.
    e.x=10; e.y=7; capture=1; render_frame(&e,1);
    uint8_t background=(0x16>>3)*255/31;
    for(int row=0;row<15;row++) for(int col=0;col<20;col++) {
        if(col==e.x && row==e.y) continue;
        assert(changed_pixels(col*16,row*16,16,16,background)==1);
        assert(output[row*16+8][col*16+8]!=background);
    }
    capture=0;
    assert(!score_create(&e.score,0,0,64));
    // Deliberate renderer overload coverage beyond the persistence budget.
    // Keep every original cell and tile kind; ordinary editor fixtures below
    // continue to use admitted model operations.
    for(int x=1;x<=64;x++) for(int y=0;y<64;y++) {
        TileId t=(x-1)*64+y+1;
        if(!y) e.score.lanes[0].tiles[x-1]=t;
        e.score.tiles[t]=(Tile){score_default((TileKind)(1+(x+y)%3)),y==63?0:(TileId)(t+1),-1};
        e.score.tile_generation[t]=++e.score.generation;
        e.score.tiles[t].value.pitch=49; e.score.tiles[t].value.chance=100; e.score.tiles[t].value.period=32;
    }
    for(int y=0;y<64;y++) for(int x=0;x<128;x++) {
        e.x=x; e.y=y;
        for(int mode=EDIT_PLANE;mode<EDIT_MODE_COUNT;mode++) {
            e.mode=mode; e.lane=0; e.sound_channel=SCORE_CHANNELS-1; e.candidate=mode==EDIT_DIVISION?11:mode==EDIT_CHANNEL?SCORE_CHANNELS-1:64;
            e.value=score_default(TILE_NOTE); e.value.period=32; e.value.pitch=108;
            e.value.lock_mask=3; e.value.attack=-16000; e.value.release=16000;
            e.sound_candidate=e.score.sounds[0]=(SoundSettings){16000,16000,WAVE_SINE,WAVE_SINE,0,0,0,200,0};
            e.playing=1; e.snapshot_dirty=mode%2;
            e.tile_candidate=TILE_RELATIVE; e.source_x=1; e.source_y=0;
            render_frame(&e,1);
        }
    }
    editor_init(&e);
    for(int i=0;i<16;i++) {
        assert(!score_create(&e.score,0,i,64)); assert(!score_set_channel(&e.score,i,i%SCORE_CHANNELS));
    }
    for(int corner=0;corner<4;corner++) for(int mode=0;mode<EDIT_MODE_COUNT;mode++) {
        e.x=(corner&1)?127:0; e.y=(corner&2)?63:0; e.mode=mode;
        e.value=score_default(TILE_CYCLE); e.value.period=32;
        e.candidate=mode==EDIT_DIVISION?11:mode==EDIT_CHANNEL?SCORE_CHANNELS-1:64; render_frame(&e,0);
    }
    editor_init(&e); assert(!score_create(&e.score,1,1,16));
    assert(!score_place(&e.score,2,1,TILE_CYCLE));
    for(int y=2;y<5;y++) assert(!score_place(&e.score,2,y,TILE_NOTE));
    TileId sharp=score_at(&e.score,2,2).tile;
    TileValue sharp_value=e.score.tiles[sharp].value; sharp_value.pitch=106;
    assert(!score_edit(&e.score,sharp,sharp_value));
    assert(!score_place(&e.score,3,1,TILE_PROBABILITY));
    assert(!score_place(&e.score,4,1,TILE_JUMP));
    for(int i=0;i<13;i++) assert(!score_place(&e.score,5+i,1,TILE_JUMP));
    for(int y=0;y<64;y++) { e.y=y; render_frame(&e,1); }
    assert(!score_create(&e.score,19,14,4));
    e.x=2; e.y=1; capture=1; e.mode=EDIT_PLANE; render_frame(&e,1); save("build/tests/plane.pgm");
    assert(changed_pixels(19*16,14*16,16,16,background)>1);
    // The three compact glyph sprites for A+8 stay within the Note body.
    int sharp_glyphs=0;
    for(int i=0;i<counts[5];i++) {
        SPRT *p=primitives[5][i];
        if(p->code==0x64 && p->y0==2*16+5 && p->h==5) {
            assert(p->x0>=2*16+2 && p->x0+p->w<=3*16-1);
            sharp_glyphs++;
        }
    }
    assert(sharp_glyphs==3);
    e.mode=EDIT_PICKER; render_frame(&e,1); save("build/tests/picker.pgm");
    e.mode=EDIT_PATTERN; e.value=score_default(TILE_CYCLE); e.value.period=32;
    render_frame(&e,1); save("build/tests/pattern.pgm");
    e.mode=EDIT_LENGTH; e.lane=0; e.candidate=64; render_frame(&e,1); save("build/tests/resize.pgm");
    e.mode=EDIT_LOCK_ATTACK; e.value=score_default(TILE_RELATIVE); e.value.lock_mask=3; e.value.attack=-16000;
    render_frame(&e,1); save("build/tests/lock.pgm");
    e.mode=EDIT_SOUND; e.score.sounds[0]=(SoundSettings){16000,16000,WAVE_SINE,WAVE_SINE,0,0,0,200,0};
    render_frame(&e,1); save("build/tests/sound.pgm");
    static const struct { EditorMode mode; const char *name; } sound_views[]={
        {EDIT_MAIN,"main"},{EDIT_BPM,"bpm"},{EDIT_REVERB,"reverb"},{EDIT_REVERB_SIZE,"reverb-size"},
        {EDIT_REVERB_AMOUNT,"reverb-amount"},{EDIT_SOUND_REVERB,"reverb-enable"},{EDIT_WAVES,"waves"},{EDIT_AMPLITUDE,"amplitude"},{EDIT_MIX,"mix"},{EDIT_SWEEP,"sweep"},
        {EDIT_WAVE_A,"wave-a"},{EDIT_WAVE_B,"wave-b"},{EDIT_ATTACK,"amp-attack"},
        {EDIT_RELEASE,"amp-release"},{EDIT_MIX_ATTACK,"mix-attack"},
        {EDIT_MIX_RELEASE,"mix-release"},{EDIT_PITCH_SWEEP,"pitch-sweep"},{EDIT_PITCH_DECAY,"pitch-decay"}
    };
    e.sound_candidate=e.score.sounds[0]=(SoundSettings){16000,16000,WAVE_TRIANGLE,WAVE_NOISE,500,500,-24,2000,0};
    e.playing=1; e.snapshot_dirty=1;
    for(unsigned i=0;i<sizeof(sound_views)/sizeof(*sound_views);i++) {
        e.mode=sound_views[i].mode;
        e.candidate=e.mode==EDIT_BPM?e.score.bpm:e.mode==EDIT_REVERB_SIZE?e.score.reverb.size:
            e.mode==EDIT_SOUND_REVERB?e.score.sounds[0].reverb:e.score.reverb.amount;
        for(int selected=0;selected<3;selected++) {
            e.selected=selected; render_frame(&e,1);
        }
        char path[80]; snprintf(path,sizeof(path),"build/tests/sound-%s.pgm",sound_views[i].name);
        save(path);
    }
    e.x=1; e.y=1; e.lane=0; e.sound_channel=7;
    assert(!score_set_channel(&e.score,0,7));
    static const struct { EditorMode mode; const char *name; } channel_views[]={
        {EDIT_MENU,"menu"},{EDIT_CHANNEL,"selector"},{EDIT_SOUND,"sound"},
        {EDIT_AMPLITUDE,"amplitude"},{EDIT_ATTACK,"attack"},{EDIT_SOUND_REVERB,"reverb"}
    };
    for(unsigned i=0;i<sizeof(channel_views)/sizeof(*channel_views);i++) {
        e.mode=channel_views[i].mode; e.selected=0; e.candidate=e.mode==EDIT_CHANNEL?7:1;
        render_frame(&e,1); char path[80];
        snprintf(path,sizeof(path),"build/tests/channel-%s.pgm",channel_views[i].name); save(path);
    }
    int branch=e.score.tiles[score_at(&e.score,4,1).tile].branch;
    e.x=e.score.lanes[branch].x; e.y=e.score.lanes[branch].y; e.mode=EDIT_PLANE;
    assert(score_channel(&e.score,branch)==7);
    render_frame(&e,1); save("build/tests/channel-branch.pgm");
    editor_init(&e); e.mode=EDIT_MAIN; e.playing=1; e.storage_slot=15;
    editor_refresh_capacity(&e);
    for(int status=STORAGE_UNKNOWN;status<=STORAGE_GENERATION_FULL;status++) {
        e.slot_status=status; e.card_free=status%16; e.message=storage_message(status);
        for(int selected=0;selected<5;selected++) {
            e.selected=selected; render_frame(&e,1);
        }
        char path[80]; snprintf(path,sizeof(path),"build/tests/storage-status-%d.pgm",status); save(path);
    }
    e.free_bytes=0; e.message="SCORE FULL";
    render_frame(&e,1); save("build/tests/storage-full.pgm");
    assert(render_overflows==0);
    printf("PASS: %d render frames; peak %u / 65536 bytes; no overflow or screen escape\n",frame_count,render_packet_peak);
}
