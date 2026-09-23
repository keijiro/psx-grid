// Standalone development fixture; never compiled into the editor executable.
#include "audio.h"
#include "pad.h"
#include "editor.h"
#include "render.h"
#include <psxetc.h>
#include <psxgpu.h>
#include <psxapi.h>
#include <psxspu.h>
#include <stdio.h>
#include <stdarg.h>
extern volatile uint32_t audio_service_peak, audio_interval_peak, audio_services;
extern volatile uint32_t audio_voice_steals, audio_skipped_notes, audio_overloads;
extern volatile uint32_t audio_dispatch_peak, audio_note_count, audio_first_note, audio_last_note;
extern volatile uint32_t audio_started;
static Editor editor;
static uint32_t capture[256];
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
    for(int i=0;i<count;i++) { editor.x=i%128; editor.y=(i/4)%64; render_frame(&editor,1); InputSample sample; while(pad_read(&sample)) {} }
}
static void publish_revisions(const char *phase,int count) {
    uint32_t copy_peak=0,adopt_peak=0;
    int adopted=0;
    TileId tile=editor.score.lanes[0].tiles[0];
    for(int i=0;i<count;i++) {
        // The dense fixture also reverses all heads to exercise the full
        // runner reconciliation and execution-order sort at every adoption.
        if(editor.score.lanes[SCORE_LANES-1].active)
            for(int lane=0;lane<SCORE_LANES;lane++) editor.score.lanes[lane].x=(i%2?lane:SCORE_LANES-1-lane)*6;
        TileValue value=editor.score.tiles[tile].value;
        value.pitch=value.pitch==48?55:48;
        score_edit(&editor.score,tile,value);
        AudioTime begin=audio_platform_time();
        audio_platform_update(&editor.score,1,0);
        uint32_t copied=(uint32_t)(audio_platform_time()-begin);
        if(copied>copy_peak) copy_peak=copied;
        while(audio_platform_revision()!=editor.score.revision && audio_platform_time()-begin<SEQUENCER_HZ/2)
            audio_platform_update(&editor.score,1,0);
        uint32_t elapsed=(uint32_t)(audio_platform_time()-begin);
        if(elapsed>adopt_peak) adopt_peak=elapsed;
        adopted+=audio_platform_revision()==editor.score.revision;
        frames(1);
    }
    log_message("LIVE %s: edits=%d adopted=%d copy=%u adoption=%u playing=%d\n",
        phase,count,adopted,(unsigned)copy_peak,(unsigned)adopt_peak,audio_platform_playing());
    report(phase);
}
static void stop_pending(int disconnect) {
    int pending=0;
    uint32_t before=0;
    for(int i=0;i<32 && !pending;i++) {
        TileId tile=editor.score.lanes[0].tiles[0];
        TileValue value=editor.score.tiles[tile].value;
        value.pitch=value.pitch==48?55:48;
        score_edit(&editor.score,tile,value);
        audio_platform_update(&editor.score,1,0);
        // Freeze only the adoption window after the large copy has finished.
        // This makes the pending-stop case observable without masking the
        // timer during a score copy or depending on emulator host scheduling.
        uint16_t mask=IRQ_MASK;
        IRQ_MASK=mask&~(1u<<IRQ_TIMER0);
        before=audio_platform_revision();
        pending=before!=editor.score.revision;
        if(pending) audio_platform_update(&editor.score,!disconnect,!disconnect);
        IRQ_MASK=mask;
    }
    frames(2);
    log_message("LIVE %s: pending=%d playing=%d unchanged=%d\n",
        disconnect?"pending disconnect":"pending stop",pending,audio_platform_playing(),audio_platform_revision()==before);
    report(disconnect?"pending disconnect":"pending stop");
}
static void coalesce_revision(void) {
    int pending=0,deferred=0;
    TileId tile=editor.score.lanes[0].tiles[0];
    for(int i=0;i<32 && !pending;i++) {
        TileValue value=editor.score.tiles[tile].value;
        value.pitch=value.pitch==48?55:48;
        score_edit(&editor.score,tile,value);
        audio_platform_update(&editor.score,1,0);
        uint16_t mask=IRQ_MASK;
        IRQ_MASK=mask&~(1u<<IRQ_TIMER0);
        uint32_t before=audio_platform_revision();
        pending=before!=editor.score.revision;
        if(pending) {
            value.pitch=value.pitch==48?55:48;
            score_edit(&editor.score,tile,value);
            audio_platform_update(&editor.score,1,0);
            deferred=audio_platform_revision()==before;
        }
        IRQ_MASK=mask;
    }
    // No more edits follow. The ordinary main-loop update must eventually
    // submit the revision that arrived while both buffers belonged to audio.
    AudioTime begin=audio_platform_time();
    while(audio_platform_revision()!=editor.score.revision && audio_platform_time()-begin<SEQUENCER_HZ/2)
        audio_platform_update(&editor.score,1,0);
    log_message("LIVE coalesced: pending=%d deferred=%d adopted=%d playing=%d\n",
        pending,deferred,audio_platform_revision()==editor.score.revision,audio_platform_playing());
}
int main(void) {
    editor_init(&editor); render_init();
    audio_platform_init(); pad_init();
    frames(60); report("idle");
    score_create(&editor.score,0,0,16);
    for(int i=1;i<=16;i++) score_place(&editor.score,i,0,TILE_NOTE);
    audio_platform_update(&editor.score,1,1); frames(180); report("C4 loop");
    audio_platform_update(&editor.score,0,0); frames(2); report("stopped");
    score_init(&editor.score); score_create(&editor.score,0,0,1);
    score_set_division(&editor.score,0,64);
    score_place(&editor.score,1,0,TILE_NOTE);
    audio_platform_update(&editor.score,1,1); frames(4);
    unsigned original_pitch=SPU_CH_FREQ(0);
    publish_revisions("live pitch",25); frames(4);
    log_message("LIVE pitch registers: before=%u after=%u\n",original_pitch,(unsigned)SPU_CH_FREQ(0));
    coalesce_revision();
    stop_pending(0);
    audio_platform_update(&editor.score,1,1); frames(4);
    stop_pending(1);
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
        editor.score.lane_generation[i]=++editor.score.generation;
        for(int j=0;j<4;j++) {
            lane->tiles[j]=id;
            for(int k=0;k<64;k++,id++) {
                editor.score.tiles[id]=(Tile){score_default(TILE_NOTE),k==63?0:(TileId)(id+1),-1};
                editor.score.tile_generation[id]=++editor.score.generation;
            }
        }
    }
    audio_platform_update(&editor.score,1,1); frames(12); report("4096 tiles");
    publish_revisions("live 4096 tiles",32);
    audio_platform_update(&editor.score,0,0); frames(2); report("final stop");
    log_message("AUDIO FIXTURE COMPLETE\n");
    // PCSX-Redux development exit port; -testmode makes this terminate the
    // emulator. Physical hardware must use the ordinary editor executable.
    *(volatile int16_t *)0x1f802082=0;
    for(;;) VSync(0);
}
