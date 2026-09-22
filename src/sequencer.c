#include "sequencer.h"
#include <string.h>
static int bound(int value) { return value<0?0:value>SOUND_MAX_MS?SOUND_MAX_MS:value; }
static void lock(SoundSettings *sound, TileValue v) {
    if(v.lock_mask&LOCK_ATTACK) sound->attack=bound(sound->attack+v.attack);
    if(v.lock_mask&LOCK_RELEASE) sound->release=bound(sound->release+v.release);
}
static uint32_t random_next(Sequencer *s) {
    uint32_t x=s->random; x^=x<<13; x^=x>>17; x^=x<<5; return s->random=x;
}
static int precedes(const Score *s,int a,int b) {
    const Lane *x=&s->lanes[a],*y=&s->lanes[b];
    return x->y!=y->y?x->y<y->y:x->x!=y->x?x->x<y->x:a<b;
}
void sequencer_start(Sequencer *s,const Score *score,NoteSink sink,AudioTime now) {
    memset(s,0,sizeof(*s)); s->score=score; s->sink=sink; s->playing=1; s->random=0x6d2b79f5u;
    for(int i=0;i<SCORE_LANES;i++) if(score->lanes[i].active && !score->lanes[i].source) {
        int n=s->count++;
        while(n && precedes(score,i,s->runners[n-1].origin)) { s->runners[n]=s->runners[n-1]; n--; }
        Runner *r=&s->runners[n]; memset(r,0,sizeof(*r));
        r->origin=r->lane=i; r->playing_step=-1; r->duration=2*SEQUENCER_HZ/score->lanes[i].division; r->next=now;
    }
}
void sequencer_stop(Sequencer *s,AudioTime now) {
    s->playing=0; memset(s->offs,0,sizeof(s->offs));
    if(s->sink.stop) s->sink.stop(s->sink.context,now);
}
static void offs_until(Sequencer *s,AudioTime now) {
    // At most one pending gate-off per physical voice. The generation token
    // prevents an old gate from releasing a replacement after a steal.
    for(;;) {
        int first=-1;
        for(int i=0;i<SEQUENCER_VOICES;i++) if(s->offs[i].token && s->offs[i].at<=now &&
            (first<0 || s->offs[i].at<s->offs[first].at)) first=i;
        if(first<0) return;
        AudioTime at=s->offs[first].at;
        // Chord gate-offs share a deadline. Drain that group in one pass
        // instead of searching all 24 slots again for each of its voices.
        for(int i=0;i<SEQUENCER_VOICES;i++) if(s->offs[i].token && s->offs[i].at==at) {
            uint32_t token=s->offs[i].token; s->offs[i].token=0;
            s->sink.off(s->sink.context,at,token);
        }
    }
}
static void tile_event(Sequencer *s,Runner *r,TileId t,AudioTime now) {
    TileValue v=s->score->tiles[t].value;
    if(v.kind==TILE_CYCLE && !(v.pattern&((uint32_t)1<<(r->lap%v.period)))) { s->cursor=0; return; }
    if(v.kind==TILE_PROBABILITY) {
        uint32_t draw=random_next(s);
        if(v.chance==0 || (v.chance<100 && draw%100>=(unsigned)v.chance)) { s->cursor=0; return; }
    }
    if(v.kind==TILE_RELATIVE) { lock(&s->working,v); r->held[r->held_count++]=t; }
    if(v.kind==TILE_JUMP) s->jump=s->score->tiles[t].branch;
    if(v.kind==TILE_NOTE) {
        if(now-s->slice_at>SEQUENCER_HZ/1000) { s->skipped++; return; }
        uint32_t token=s->sink.on(s->sink.context,s->slice_at,v.pitch,s->working);
        if(token) s->offs[token&31]=(NoteOff){s->slice_at+(AudioTime)(r->duration/20)*v.length,token};
    }
}
static int slice(Sequencer *s,AudioTime now,int *budget) {
    while(s->runner_index<s->count) {
        Runner *r=&s->runners[s->runner_index];
        if(!s->visiting) {
            s->visiting=1; s->held_index=0; s->jump=-1;
            if(r->next==s->slice_at) {
                r->held_count=0; r->playing_lane=r->lane; r->playing_step=r->step;
                s->cursor=s->score->lanes[r->lane].tiles[r->step];
            }
        }
        if(r->next==s->slice_at) {
            while(s->cursor) {
                if(!*budget) return 0;
                --*budget;
                TileId t=s->cursor; s->cursor=s->score->tiles[t].next;
                tile_event(s,r,t,now);
            }
            if(s->jump>=0) { r->lane=s->jump; r->step=0; }
            else if(++r->step==s->score->lanes[r->lane].length) { r->lane=r->origin; r->step=0; r->lap++; }
            r->next+=r->duration;
        } else {
            while(s->held_index<r->held_count) {
                if(!*budget) return 0;
                --*budget;
                lock(&s->working,s->score->tiles[r->held[s->held_index++]].value);
            }
        }
        s->visiting=0; s->runner_index++;
    }
    s->slicing=0; return 1;
}
void sequencer_service(Sequencer *s,AudioTime now) {
    int work=0,budget=SEQUENCER_TILE_BUDGET;
    if(s->playing) for(;;) {
        if(!s->slicing) {
            AudioTime at=UINT64_MAX;
            for(int i=0;i<s->count;i++) if(s->runners[i].next<at) at=s->runners[i].next;
            if(at>now) break;
            if(work++==SEQUENCER_BUDGET) { s->overloads++; break; }
            offs_until(s,at);
            s->slice_at=at; s->working=s->score->sound; s->slicing=1; s->runner_index=s->visiting=0;
        }
        // Both time slices and tile visits are bounded. Dense stacks resume
        // from this exact cursor on later interrupts, including held-lock
        // replay. Gates and PRNG draws occur once; late note-ons are dropped.
        // This avoids a 4096-tile score monopolizing an interrupt or losing
        // clock wraps, without resetting its musical state under overload.
        if(!slice(s,now,&budget)) { s->overloads++; break; }
    }
    offs_until(s,now);
    if(s->sink.advance) s->sink.advance(s->sink.context,now);
}
