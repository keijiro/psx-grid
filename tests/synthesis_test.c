#include "audio.h"
#include "audio_tables.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

static Audio audio;
static int gains[SEQUENCER_VOICES][2], pitches[SEQUENCER_VOICES], banks[SEQUENCER_VOICES];
static SoundSettings captured[SEQUENCER_VOICES];
static uint32_t started, stopped;
static void start(void *ctx,int slot,int bank,SoundSettings sound) {
    (void)ctx; banks[slot]=bank; captured[slot]=sound;
}
static void volume(void *ctx,int slot,int a,int b) {
    (void)ctx; assert(a>=0 && b>=0 && a+b<=AUDIO_LEVEL);
    gains[slot][0]=a; gains[slot][1]=b;
}
static void pitch(void *ctx,int slot,int value) {
    (void)ctx; assert(value>0 && value<16384); pitches[slot]=value;
}
static void flush(void *ctx,uint32_t on,uint32_t off) {
    (void)ctx; assert(!(on&off)); started=on; stopped=off;
}
static NoteSink reset(void) {
    memset(gains,0,sizeof(gains));
    audio_init(&audio,(AudioDriver){NULL,start,volume,pitch,flush});
    return audio_sink(&audio);
}
static double ideal_register(int bank,double note) {
    const int lengths[]={2688,1344,672,336,168,84,84,84,84,84};
    return 440*exp2((note-57)/12)*lengths[bank]/(1<<(bank>5?bank-5:0))/44100*4096;
}
static void sweeps(void) {
    const int decays[]={0,1,7,200,2000};
    double worst=0, quantization=0, approximation=0;
    unsigned samples=0;
    // Exercise production arithmetic at nonuniform and dense onset times.
    // A large absolute origin also detects accidental 32-bit elapsed clocks.
    AudioTime origin=UINT64_C(0x100000000)+17;
    for(int note=0;note<=108;note++) for(int depth=-24;depth<=24;depth++)
    for(unsigned d=0;d<sizeof(decays)/sizeof(*decays);d++) {
        NoteSink sink=reset();
        SoundSettings sound={0,16000,WAVE_SAW,WAVE_TRIANGLE,120,280,depth,decays[d]};
        sink.on(sink.context,origin,note,sound);
        AudioTime duration=audio_ms(decays[d]);
        int previous=0;
        for(int i=0;i<=513;i++) {
            AudioTime elapsed=i==513?duration+1:duration*(unsigned)i/512;
            sink.advance(sink.context,origin+elapsed);
            double snap=duration && elapsed<duration?(exp(-8*(double)elapsed/duration)-exp(-8))/(1-exp(-8)):0;
            double n=fmax(0,fmin(108,note+depth*snap));
            double ideal=ideal_register(banks[0],n);
            double error=fabs(1200*log2(pitches[0]/ideal));
            double rounded=fabs(1200*log2(round(ideal)/ideal));
            // Inspect the generated fixed-point path before its final SPU
            // register rounding, separately from the production output above.
            double continuous=audio_pitch[banks[0]*109+note]/256.0;
            if(depth && duration && elapsed<duration) {
                uint32_t x=(uint32_t)(elapsed*65536/duration);
                unsigned index=x>>6, fraction=x&63;
                int snap_q16=audio_snap[index]-(audio_snap[index]-audio_snap[index+1])*fraction/64;
                int position=note*65536+depth*snap_q16;
                if(position<0) position=0;
                if(position>108*65536) position=108*65536;
                index=(unsigned)position>>16;
                const uint32_t *table=&audio_pitch[banks[0]*109];
                uint32_t value=table[index];
                if(index<108) value+=(table[index+1]-value)*(((unsigned)position&65535)>>4)/4096;
                continuous=value/256.0;
            }
            double approx=fabs(1200*log2(continuous/ideal));
            if(approx>approximation) approximation=approx;
            assert(approx<1.123);
            if(error>worst) worst=error;
            if(rounded>quantization) quantization=rounded;
            assert(error<2);
            if(i && depth>0) assert(pitches[0]<=previous);
            if(i && depth<0) assert(pitches[0]>=previous);
            if(elapsed>=duration) assert(pitches[0]==audio.voices[0].base_pitch);
            previous=pitches[0]; samples++;
        }
    }
    printf("PASS: %u sweep samples, all 109 notes/49 depths, decays 0/1/7/200/2000 ms; max total %.6f cents, ideal register quantization %.6f cents, pre-register approximation %.6f cents\n",samples,worst,quantization,approximation);
}
static void envelopes(void) {
    const int times[]={0,1,120,500};
    for(int wave=0;wave<5;wave++) for(int a=0;a<4;a++) for(int r=0;r<4;r++) {
        NoteSink sink=reset();
        SoundSettings sound={0,16000,wave,wave,times[a],times[r],24,2000};
        uint32_t token=sink.on(sink.context,0,48,sound);
        AudioTime attack=audio_ms(times[a]), release=audio_ms(times[r]);
        for(int ms=0;ms<=1001;ms++) {
            AudioTime t=audio_ms(ms);
            if(ms==3) sink.off(sink.context,t,token);
            sink.advance(sink.context,t);
            int mix=attack && t<attack?t*AUDIO_LEVEL/attack:
                release && t<attack+release?(attack+release-t)*AUDIO_LEVEL/release:0;
            assert(gains[0][0]+gains[0][1]==audio.voices[0].level);
            assert(gains[0][1]==audio.voices[0].level*mix/AUDIO_LEVEL);
            assert(captured[0].wave_a==wave && captured[0].wave_b==wave);
        }
        sink.advance(sink.context,audio_ms(16003));
        assert(!audio.voices[0].active && !gains[0][0] && !gains[0][1]);
    }
    // Gate duration neither restarts the mix nor the signed pitch envelope.
    for(int gate=1;gate<=1001;gate+=1000) {
        NoteSink sink=reset();
        SoundSettings sound={100,500,WAVE_NOISE,WAVE_SQUARE,120,280,-24,2000};
        uint32_t token=sink.on(sink.context,0,48,sound);
        sink.advance(sink.context,audio_ms(gate));
        int before=pitches[0], level=audio.voices[0].level;
        sink.off(sink.context,audio_ms(gate),token);
        sink.advance(sink.context,audio_ms(gate));
        assert(pitches[0]==before && audio.voices[0].level==level);
        sink.advance(sink.context,audio_ms(gate+250));
        assert(pitches[0]>=before && audio.voices[0].level<=level);
        sink.advance(sink.context,audio_ms(gate+500));
        assert(!audio.voices[0].active && !gains[0][0] && !gains[0][1]);
    }
    NoteSink sink=reset();
    SoundSettings sound={0,0,WAVE_SINE,WAVE_NOISE,0,0,0,0};
    uint32_t token=sink.on(sink.context,0,48,sound);
    sink.off(sink.context,0,token); sink.advance(sink.context,0);
    assert(!started && stopped==1 && !gains[0][0] && !gains[0][1]);
    puts("PASS: all mix zero-time combinations, equal waves, long tails, short/long gates and late-start rejection");
}
static void snapshots(void) {
    static Score score, updated;
    static Sequencer seq;
    score_init(&score); assert(!score_create(&score,0,0,1));
    SoundSettings old={0,400,WAVE_SAW,WAVE_NOISE,23,97,-13,777};
    assert(!score_set_sound(&score,old));
    TileValue lock=score_default(TILE_RELATIVE); lock.lock_mask=3; lock.attack=7; lock.release=-19;
    assert(!score_place_value(&score,1,0,lock));
    assert(!score_place_value(&score,1,1,score_default(TILE_NOTE)));
    NoteSink sink=reset(); sequencer_start(&seq,&score,sink,0); sequencer_service(&seq,0);
    SoundSettings resolved=old; resolved.attack=7; resolved.release=381;
    assert(!memcmp(&audio.voices[0].sound,&resolved,sizeof(resolved)));
    SoundSettings fresh={0,600,WAVE_SQUARE,WAVE_TRIANGLE,0,500,24,1};
    updated=score; assert(!score_set_sound(&updated,fresh));
    assert(sequencer_resync(&seq,&updated,1));
    AudioTime next=SEQUENCER_HZ/8;
    sequencer_service(&seq,next);
    fresh.attack=7; fresh.release=581;
    assert(!memcmp(&audio.voices[0].sound,&resolved,sizeof(resolved)));
    assert(!memcmp(&captured[0],&resolved,sizeof(resolved)));
    assert(!memcmp(&captured[1],&fresh,sizeof(fresh)));
    sink.stop(sink.context,next); sink.advance(sink.context,next+audio_ms(5));
    assert(audio.idle_mask==AUDIO_IDLE_MASK && stopped==3);
    puts("PASS: amplitude locks preserve synthesis fields; sounding notes retain complete settings snapshots");
}
int main(void) { sweeps(); envelopes(); snapshots(); }
