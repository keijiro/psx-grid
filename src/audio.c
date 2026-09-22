#include "audio.h"
#include <string.h>
AudioTime audio_ms(int ms) { return (uint32_t)ms*(SEQUENCER_HZ/1000)+(uint32_t)ms*(SEQUENCER_HZ%1000)/1000; }
static int level_at(const AudioVoice *v,AudioTime now) {
    if(!v->active) return 0;
    if(v->releasing) {
        if(now>=v->end) return 0;
        // Discard seven clock bits (30.3 us) before multiplying, keeping
        // envelope arithmetic in 32 bits on the MIPS I interrupt path.
        uint32_t left=(uint32_t)(v->end-now)>>7, span=(uint32_t)(v->end-v->release_at)>>7;
        int level=span?(int)((left*(unsigned)v->release_level+span-1)/span):v->release_level;
        return level?level:v->release_level?1:0;
    }
    AudioTime attack=audio_ms(v->sound.attack), elapsed=now-v->start;
    if(!attack || elapsed>=attack) return AUDIO_LEVEL;
    int level=(int)(((uint32_t)elapsed>>7)*AUDIO_LEVEL/((uint32_t)attack>>7));
    return level<AUDIO_LEVEL?level:AUDIO_LEVEL-1;
}
static void advance(void *ctx,AudioTime now) {
    Audio *a=ctx;
    if(a->idle_mask==0xffffff && !(a->starts|a->stops)) return;
    for(int i=0;i<SEQUENCER_VOICES;i++) {
        AudioVoice *v=&a->voices[i];
        if(!v->active) continue;
        v->level=level_at(v,now);
        if(a->starts&(1u<<i)) a->driver.start(a->driver.context,i,v->pitch);
        a->driver.volume(a->driver.context,i,v->level);
        if(v->releasing && now>=v->end) { v->active=0; a->idle_mask|=1u<<i; a->stops|=1u<<i; a->starts&=~(1u<<i); }
    }
    a->driver.flush(a->driver.context,a->starts,a->stops);
    a->starts=a->stops=0;
}
static uint32_t on(void *ctx,AudioTime now,int pitch,SoundSettings sound) {
    Audio *a=ctx; int slot=0;
    // Reclaim tails once per event time, not once per note in a chord. The
    // idle mask keeps the common allocation path independent of polyphony.
    if(a->allocation_time!=now) {
        a->allocation_time=now;
        for(int i=0;i<SEQUENCER_VOICES;i++) {
            AudioVoice *v=&a->voices[i];
            if(v->active && v->releasing && now>=v->end) {
                v->active=0; a->idle_mask|=1u<<i; a->stops|=1u<<i;
                a->driver.volume(a->driver.context,i,0);
            }
        }
    }
    if(a->idle_mask) {
        uint32_t mask=a->idle_mask;
        if(!(mask&0xffff)) { slot+=16; mask>>=16; }
        if(!(mask&0xff)) { slot+=8; mask>>=8; }
        if(!(mask&0xf)) { slot+=4; mask>>=4; }
        if(!(mask&3)) { slot+=2; mask>>=2; }
        if(!(mask&1)) slot++;
        a->idle_mask&=~(1u<<slot);
    } else {
        int quiet=-1,quiet_level=AUDIO_LEVEL+1;
        for(int i=0;i<SEQUENCER_VOICES;i++) {
            AudioVoice *v=&a->voices[i];
            if(v->releasing) {
                int level=level_at(v,now);
                if(level<quiet_level) { quiet=i; quiet_level=level; }
            }
            if(v->start<a->voices[slot].start) slot=i;
        }
        if(quiet>=0) slot=quiet;
        a->steals++;
    }
    AudioVoice *v=&a->voices[slot];
    uint32_t generation=(v->generation+1)&0x07ffffffu;
    if(!generation) generation=1;
    v->start=now; v->generation=generation; v->sound=sound;
    v->active=1; v->releasing=0; v->level=0; v->pitch=pitch;
    // Batch one final key-on per slot. A stolen slot must not receive key-off
    // in the same batch, whose register priority would suppress its new note.
    a->stops&=~(1u<<slot); a->starts|=1u<<slot;
    return generation<<5 | (uint32_t)slot;
}
static void release(Audio *a,int i,AudioTime now,int ms) {
    AudioVoice *v=&a->voices[i];
    v->release_level=level_at(v,now); v->release_at=now; v->end=now+audio_ms(ms); v->releasing=1;
}
static void off(void *ctx,AudioTime now,uint32_t token) {
    Audio *a=ctx; unsigned i=token&31;
    if(i>=SEQUENCER_VOICES) return;
    AudioVoice *v=&a->voices[i];
    if(v->active && !v->releasing && v->generation==(token>>5)) {
        release(a,(int)i,now,v->sound.release);
        if(!v->sound.release) {
            v->active=0; a->idle_mask|=1u<<i; a->stops|=1u<<i; a->starts&=~(1u<<i);
            a->driver.volume(a->driver.context,(int)i,0);
        }
    }
}
static void stop(void *ctx,AudioTime now) {
    Audio *a=ctx;
    for(int i=0;i<SEQUENCER_VOICES;i++) if(a->voices[i].active) release(a,i,now,5);
}
void audio_init(Audio *a,AudioDriver driver) { memset(a,0,sizeof(*a)); a->driver=driver; a->idle_mask=0xffffff; a->allocation_time=UINT64_MAX; }
NoteSink audio_sink(Audio *a) { return (NoteSink){a,on,off,stop,advance}; }
