// Device-path input regression; Lua drives actual emulated pad button replies.
#include "audio.h"
#include "editor.h"
#include "render.h"
#include "pad.h"
#include <psxgpu.h>
#include <stdio.h>

volatile unsigned input_fixture_phase, input_fixture_expected;
static Editor editor;
static Input input;
static unsigned moves, presses, releases, starts, disconnected;
static void drain(void) {
    InputSample sample;
    while(pad_read(&sample)) {
        InputFrame f=input_update(&input,sample.connected,sample.held);
        moves+=f.dx!=0; presses+=f.cross; releases+=f.cross_released; starts+=f.start;
        disconnected+=!f.connected;
    }
}
static void run(unsigned phase) {
    moves=presses=releases=starts=disconnected=0;
    unsigned polls=pad_polls,reports=pad_reports,timeouts=pad_timeouts,overflows=pad_overflows;
    input_fixture_expected=0;
    input_fixture_phase=phase;
    for(int i=0;i<(phase==5?120:480);i++) {
        editor.x=i%64;
        render_frame(&editor,1);
        // A slow main loop must retain complete taps, not just the latest held
        // state. The interrupt still collects one report per video frame.
        if(phase!=3 || i%8==7) drain();
    }
    drain();
    char line[256];
    snprintf(line,sizeof(line),"INPUT phase=%u id=%u polls=%u reports=%u timeouts=%u overflows=%u expected=%u moves=%u presses=%u releases=%u starts=%u disconnected=%u\n",
        phase,pad_id,pad_polls-polls,pad_reports-reports,pad_timeouts-timeouts,pad_overflows-overflows,
        input_fixture_expected,moves,presses,releases,starts,disconnected);
    *(const char *volatile *)0x1f802084=line;
}
int main(void) {
    editor_init(&editor); input_init(&input); render_init();
    audio_platform_init(); pad_init();
    for(int i=0;i<8;i++) { render_frame(&editor,1); drain(); }
    run(1);
    score_create(&editor.score,0,0,16);
    for(int i=0;i<SEQUENCER_VOICES;i++) {
        TileValue v=score_default(TILE_NOTE); v.pitch=36+i; v.length=1280;
        score_place_value(&editor.score,1,i,v);
    }
    audio_platform_update(&editor.score,1,1);
    run(2); run(3); run(4);
    audio_platform_update(&editor.score,0,0);
    score_init(&editor.score);
    TileId id=1;
    for(int i=0;i<16;i++) {
        Lane *lane=&editor.score.lanes[i];
        *lane=(Lane){.active=1,.x=i*6,.y=0,.length=4,.division=64};
        for(int j=0;j<4;j++) {
            lane->tiles[j]=id;
            for(int k=0;k<64;k++,id++) editor.score.tiles[id]=(Tile){score_default(TILE_NOTE),k==63?0:(TileId)(id+1),-1};
        }
    }
    audio_platform_update(&editor.score,1,1);
    run(5);
    audio_platform_update(&editor.score,0,0);
    *(const char *volatile *)0x1f802084="INPUT FIXTURE COMPLETE\n";
    *(volatile short *)0x1f802082=0;
    for(;;) VSync(0);
}
