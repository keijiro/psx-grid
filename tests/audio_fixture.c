// Standalone development fixture; never compiled into the editor executable.
#include "audio.h"
#include "editor.h"
#include "render.h"
#include <psxetc.h>
#include <psxgpu.h>
#include <psxapi.h>
#include <psxspu.h>
#include <psxpad.h>
#include <stdio.h>
#include <stdarg.h>
extern volatile uint32_t audio_service_peak, audio_interval_peak, audio_services;
extern volatile uint32_t audio_voice_steals, audio_skipped_notes, audio_overloads;
extern volatile uint32_t audio_dispatch_peak, audio_note_count, audio_first_note, audio_last_note;
extern volatile uint32_t audio_started;
static Editor editor;
static uint32_t capture[256];
static uint8_t pads[2][34];
static void log_message(const char *format,...) {
    char message[256]; va_list args;
    va_start(args,format); vsnprintf(message,sizeof(message),format,args); va_end(args);
    // The emulator's BIOS character output can duplicate characters when
    // preempted by a timer IRQ. Submit a complete diagnostic through Redux's
    // message port, leaving interrupts enabled while formatting the text.
    *(const char * volatile *)0x1f802084=message;
}
static void report(const char *phase) {
    EnterCriticalSection();
    uint32_t v[]={audio_services,audio_service_peak,audio_interval_peak,audio_voice_steals,
        audio_skipped_notes,audio_overloads,SPU_CH_ADSR_VOL(0),SPU_CH_VOL_L(0),
        audio_dispatch_peak,audio_note_count,audio_last_note-audio_first_note};
    unsigned volumes=0;
    for(int i=0;i<24;i++) volumes|=SPU_CH_VOL_L(i)|SPU_CH_VOL_R(i);
    audio_service_peak=audio_interval_peak=0;
    ExitCriticalSection();
    log_message("AUDIO %s: calls=%u cost=%u interval=%u steals=%u skipped=%u overloads=%u env=%u vol=%u volumes=%u\n",
        phase,(unsigned)v[0],(unsigned)v[1],(unsigned)v[2],(unsigned)v[3],(unsigned)v[4],(unsigned)v[5],(unsigned)v[6],(unsigned)v[7],volumes);
    log_message("DISPATCH %s: peak=%u count=%u span=%u\n",phase,(unsigned)v[8],(unsigned)v[9],(unsigned)v[10]);
}
static void frames(int count) {
    for(int i=0;i<count;i++) { editor.x=i%128; editor.y=(i/4)%64; render_frame(&editor,1); }
}
int main(void) {
    editor_init(&editor); render_init();
    InitPAD(pads[0],sizeof(pads[0]),pads[1],sizeof(pads[1])); StartPAD();
    audio_platform_init();
    frames(60); report("idle");
    score_create(&editor.score,0,0,16);
    for(int i=1;i<=16;i++) score_place(&editor.score,i,0,TILE_NOTE);
    audio_platform_update(&editor.score,1,1); frames(180); report("C4 loop");
    audio_platform_update(&editor.score,0,0); frames(2); report("stopped");
    score_init(&editor.score); score_create(&editor.score,0,0,16);
    for(int i=0;i<24;i++) {
        TileValue chord=score_default(TILE_NOTE); chord.pitch=36+i;
        score_place_value(&editor.score,1,i,chord);
    }
    audio_platform_update(&editor.score,1,1); frames(6); report("24-note chord");
    audio_platform_update(&editor.score,0,0); frames(2);
    // Capture voice 1's decoded signal through the actual SPU DMA read path.
    // Its ring is unordered here; this checks signal presence, not continuity.
    for(int octave=0;octave<=9;octave+=octave?5:4) {
        score_init(&editor.score); score_create(&editor.score,0,0,1);
        TileValue note=score_default(TILE_NOTE); note.pitch=octave*12; note.length=1280;
        score_place_value(&editor.score,1,0,note); score_place_value(&editor.score,1,1,note);
        audio_platform_update(&editor.score,1,1); frames(12);
        SpuSetTransferStartAddr(0x800); SpuRead(capture,sizeof(capture)); SpuIsTransferCompleted(SPU_TRANSFER_WAIT);
        int peak=0; const int16_t *pcm=(const int16_t *)capture;
        for(int j=0;j<512;j++) { int n=pcm[j]<0?-pcm[j]:pcm[j]; if(n>peak) peak=n; }
        log_message("CAPTURE C%d peak=%d env=%u\n",octave,peak,(unsigned)SPU_CH_ADSR_VOL(1));
        audio_platform_update(&editor.score,0,0); frames(2);
    }
    const int times[]={0,1,5,100};
    for(int i=0;i<4;i++) {
        score_init(&editor.score); score_create(&editor.score,0,0,1);
        score_set_division(&editor.score,0,1);
        score_set_sound(&editor.score,(SoundSettings){times[i],times[i]});
        TileValue note=score_default(TILE_NOTE); note.length=5;
        score_place_value(&editor.score,1,0,note); audio_platform_update(&editor.score,1,1);
        uint32_t peak=0,zero=0; AudioTime gate=SEQUENCER_HZ/2;
        while(!zero) {
            AudioTime elapsed=audio_platform_time()-audio_started;
            unsigned level=SPU_CH_VOL_L(0);
            if(!peak && level==AUDIO_LEVEL) peak=(uint32_t)elapsed;
            if(elapsed>=gate && !level) zero=(uint32_t)(elapsed-gate)+1;
            if(elapsed>gate+audio_ms(times[i]+100)) break;
        }
        log_message("ENVELOPE request=%d attack_ticks=%u release_ticks=%u\n",times[i],(unsigned)peak,(unsigned)(zero?zero-1:0));
        audio_platform_update(&editor.score,0,0); frames(2);
    }
    // Valid worst-density score: 16 disjoint four-step lanes, each with
    // 64-deep stacks, filling all 4096 slots. No allocation or model shortcut
    // is used by playback; construction alone bypasses slow UI transactions.
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
    audio_platform_update(&editor.score,1,1); frames(12); report("4096 tiles");
    audio_platform_update(&editor.score,0,0); frames(2); report("final stop");
    log_message("AUDIO FIXTURE COMPLETE\n");
    // PCSX-Redux development exit port; -testmode makes this terminate the
    // emulator. Physical hardware must use the ordinary editor executable.
    *(volatile int16_t *)0x1f802082=0;
    for(;;) VSync(0);
}
