#include "audio.h"
#include "pad.h"
#include <psxspu.h>
#include <psxetc.h>
#include <psxapi.h>
#include "wave_samples.h"

// The main thread owns the spare until it publishes it. The interrupt owns
// both buffers while one is pending, and releases the old one only after a
// whole slice has finished. No score copy needs to mask timer interrupts.
static Score snapshots[2];
static volatile int active_snapshot, pending_snapshot=-1;
static Sequencer seq;
static Audio audio;
static AudioTime clock_ticks;
static uint16_t last_counter;
static volatile int enabled;
static volatile uint32_t published_revision;
static uint32_t late_starts;
volatile uint32_t audio_service_peak, audio_interval_peak, audio_services;
volatile uint32_t audio_voice_steals, audio_skipped_notes, audio_overloads;
volatile uint32_t audio_dispatch_peak, audio_note_count, audio_first_note, audio_last_note;
volatile uint32_t audio_started, audio_control_time;
static AudioTime read_clock(void);

static int cached_volume[AUDIO_HARDWARE_VOICES], cached_pitch[SEQUENCER_VOICES];
static void start_voice(void *ctx,int slot,int bank,SoundSettings sound) {
    (void)ctx;
    for(int half=0;half<2;half++) {
        int voice=slot*2+half, wave=half?sound.wave_b:sound.wave_a;
        unsigned address=WAVE_SPU_ADDRESS+wave_offsets[bank*WAVE_COUNT+wave];
        SpuSetVoiceStartAddr(voice,address);
        SPU_CH_LOOP_ADDR(voice)=getSPUAddr(address);
        // Software amplitude retains the score's gate and 0..16000 ms times.
        // Fast attack and stationary maximum sustain hold hardware ADSR open.
        SPU_CH_ADSR1(voice)=0x00ff;
        SPU_CH_ADSR2(voice)=0x1fc0;
    }
}
static void volume(void *ctx,int slot,int a,int b) {
    (void)ctx;
    for(int half=0;half<2;half++) {
        int voice=slot*2+half, level=half?b:a;
        if(cached_volume[voice]==level) continue;
        cached_volume[voice]=level;
        SpuSetVoiceVolume(voice,level,level);
    }
}
static void pitch(void *ctx,int slot,int value) {
    (void)ctx;
    if(cached_pitch[slot]==value) return;
    cached_pitch[slot]=value;
    SpuSetVoicePitch(slot*2,value);
    SpuSetVoicePitch(slot*2+1,value);
}
static uint32_t hardware_mask(uint32_t logical) {
    uint32_t mask=0;
    for(int i=0;i<SEQUENCER_VOICES;i++) if(logical&(1u<<i)) mask|=3u<<(i*2);
    return mask;
}
static void flush(void *ctx,uint32_t starts,uint32_t stops) {
    (void)ctx;
    if(!(starts|stops)) return;
    AudioTime now=read_clock();
    // A dense slice can become stale while this callback runs. Recheck at
    // dispatch, not only at entry, so overload cannot turn into a late burst.
    for(int i=0;i<SEQUENCER_VOICES;i++) if((starts&(1u<<i)) && now-audio.voices[i].start>SEQUENCER_HZ/1000) {
        starts&=~(1u<<i); stops|=1u<<i; audio.voices[i].active=0; audio.idle_mask|=1u<<i;
        volume(NULL,i,0,0); late_starts++;
    }
    if(stops) SpuSetKey(0,hardware_mask(stops));
    if(starts) {
        SpuSetKey(1,hardware_mask(starts));
        for(int i=0;i<SEQUENCER_VOICES;i++) if(starts&(1u<<i)) {
            uint32_t late=(uint32_t)(now-audio.voices[i].start);
            if(late>audio_dispatch_peak) audio_dispatch_peak=late;
            if(!audio_note_count) audio_first_note=(uint32_t)now;
            audio_last_note=(uint32_t)now; audio_note_count++;
        }
    }
}
static AudioTime read_clock(void) {
    uint16_t counter=(uint16_t)TIMER_VALUE(2);
    clock_ticks+=(uint16_t)(counter-last_counter); last_counter=counter;
    return clock_ticks;
}
static void service(void) {
    static AudioTime previous;
    AudioTime now=read_clock();
    uint32_t interval=(uint32_t)(now-previous); previous=now;
    if(interval>audio_interval_peak) audio_interval_peak=interval;
    pad_service();
    if(enabled) {
        int pending=pending_snapshot;
        if(pending>=0 && sequencer_resync(&seq,&snapshots[pending],now)) {
            active_snapshot=pending;
            published_revision=snapshots[pending].revision;
            pending_snapshot=-1;
        }
        sequencer_service(&seq,now);
    }
    else { NoteSink sink=audio_sink(&audio); sink.advance(sink.context,now); }
    // Register fixtures use the completed control timestamp, since reading
    // the main-thread clock before readback can straddle another service.
    audio_control_time=(uint32_t)now;
    audio_voice_steals=audio.steals; audio_skipped_notes=seq.skipped+late_starts; audio_overloads=seq.overloads;
    uint32_t elapsed=(uint32_t)(read_clock()-now);
    if(elapsed>audio_service_peak) audio_service_peak=elapsed;
    audio_services++;
}
void audio_platform_init(void) {
    SpuInit();
    SpuSetCommonMasterVolume(0x3fff,0x3fff);
    SPU_FM_MODE1=SPU_FM_MODE2=SPU_NOISE_MODE1=SPU_NOISE_MODE2=0;
    SPU_REVERB_ON1=SPU_REVERB_ON2=0;
    SPU_REVERB_VOL_L=SPU_REVERB_VOL_R=0;
    SPU_CD_VOL_L=SPU_CD_VOL_R=SPU_EXT_VOL_L=SPU_EXT_VOL_R=0;
    SpuSetTransferStartAddr(WAVE_SPU_ADDRESS); SpuWrite(wave_data,sizeof(wave_data));
    SpuIsTransferCompleted(SPU_TRANSFER_WAIT);
    for(int i=0;i<AUDIO_HARDWARE_VOICES;i++) cached_volume[i]=-1;
    for(int i=0;i<SEQUENCER_VOICES;i++) { cached_pitch[i]=-1; volume(NULL,i,0,0); }
    audio_init(&audio,(AudioDriver){NULL,start_voice,volume,pitch,flush});
    // Timer 2 free-runs at CLK/8 (4,233,600 Hz). Timer 0 requests service
    // every 8,467 CPU clocks, approximately 0.25 ms, independently of VSync.
    // Retain the cadence used by the original sine backend; paired control
    // cost and the 1 ms dispatch deadline must be measured with the fixture.
    // Read elapsed hardware ticks, never count interrupts as elapsed time.
    // The 16-bit clock must be sampled within 15.48 ms; no callback, DMA wait
    // or score copy may mask interrupts for that long. Peaks expose violations
    // below that limit, but multiple missed wraps require external validation.
    EnterCriticalSection();
    TIMER_CTRL(2)=0x0200; TIMER_VALUE(2)=0;
    TIMER_CTRL(0)=0; TIMER_RELOAD(0)=8467; TIMER_VALUE(0)=0;
    InterruptCallback(IRQ_TIMER0,service);
    TIMER_CTRL(0)=0x0058; // System clock, reset at target, repeating target IRQ.
    last_counter=(uint16_t)TIMER_VALUE(2);
    ExitCriticalSection();
}
void audio_platform_update(const Score *score,int connected,int start) {
    if(!connected || (start && enabled)) {
        EnterCriticalSection();
        if(enabled) { sequencer_stop(&seq,read_clock()); enabled=0; }
        pending_snapshot=-1;
        ExitCriticalSection();
    } else if(start) {
        // The timer remains live while the immutable snapshot is prepared.
        // Nothing in the interrupt can observe this copy until publication.
        snapshots[0]=*score;
        sequencer_start(&seq,&snapshots[0],audio_sink(&audio),0);
        EnterCriticalSection();
        AudioTime now=read_clock();
        // Restart discards release tails. Zero their registers before reuse;
        // the regular stop path, in contrast, always completes its 5 ms ramp.
        for(int i=0;i<SEQUENCER_VOICES;i++) { audio.voices[i].active=0; volume(NULL,i,0,0); }
        SPU_KEY_OFF1=0xffff; SPU_KEY_OFF2=0xff;
        audio.starts=audio.stops=0;
        audio.idle_mask=AUDIO_IDLE_MASK; audio.allocation_time=UINT64_MAX;
        for(int i=0;i<seq.count;i++) seq.runners[i].next=now;
        audio_started=(uint32_t)now;
        audio_dispatch_peak=audio_note_count=audio_first_note=audio_last_note=late_starts=0;
        active_snapshot=0; pending_snapshot=-1;
        published_revision=score->revision; enabled=1;
        ExitCriticalSection();
    } else if(enabled && pending_snapshot<0 && score->revision!=published_revision) {
        // With no pending publication the active buffer cannot change during
        // this copy. Edits arriving while a buffer is pending are coalesced in
        // the editor score and copied on a later main-thread update.
        int spare=1-active_snapshot;
        snapshots[spare]=*score;
        EnterCriticalSection();
        pending_snapshot=spare;
        ExitCriticalSection();
    }
}
int audio_platform_playing(void) { return enabled; }
uint32_t audio_platform_revision(void) { return published_revision; }

AudioTime audio_platform_time(void) {
    EnterCriticalSection(); AudioTime now=read_clock(); ExitCriticalSection(); return now;
}
