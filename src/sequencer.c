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
static void runner_start(Runner *r,int lane,AudioTime at) {
    r->active=1; r->origin=r->lane=lane; r->step=0;
    r->playing_lane=lane; r->playing_step=-1;
    r->lap=0; r->duration=0; r->next=at; r->held_count=0;
}
static void order_runners(Sequencer *s) {
    s->count=0;
    for(int i=0;i<SCORE_LANES;i++) if(s->runners[i].active) {
        int n=s->count++;
        while(n && precedes(s->score,s->runners[i].origin,s->runners[s->order[n-1]].origin)) {
            s->order[n]=s->order[n-1]; n--;
        }
        s->order[n]=i;
    }
}
void sequencer_start(Sequencer *s,const Score *score,NoteSink sink,AudioTime now) {
    memset(s,0,sizeof(*s)); s->score=score; s->sink=sink; s->playing=1; s->random=0x6d2b79f5u;
    for(int i=0;i<SCORE_LANES;i++) if(score->lanes[i].active && !score->lanes[i].source) {
        int n=s->count++;
        while(n && precedes(score,i,s->runners[n-1].origin)) { s->runners[n]=s->runners[n-1]; n--; }
        runner_start(&s->runners[n],i,now);
    }
    order_runners(s);
}
static int same_lane(const Score *old,const Score *score,int lane) {
    return score->lanes[lane].active && old->lane_generation[lane]==score->lane_generation[lane];
}
int sequencer_resync(Sequencer *s,const Score *score,AudioTime now) {
    if(s->slicing) return 0;
    const Score *old=s->score;
    // Only the 16 runner seats are reconciled here. Held locks validate their
    // births lazily under the tile budget, so publication never scans a score
    // or copies a runner's entire hold inside the audio interrupt.
    for(int i=0;i<SCORE_LANES;i++) {
        Runner *r=&s->runners[i];
        if(!r->active) continue;
        if(!same_lane(old,score,r->origin) || score->lanes[r->origin].source) {
            r->active=0; continue;
        }
        if(!same_lane(old,score,r->lane)) { r->lane=r->origin; r->step=0; }
        if(r->step>=score->lanes[r->lane].length) r->step=0;
        if(!same_lane(old,score,r->playing_lane) || r->playing_step>=score->lanes[r->playing_lane].length) {
            r->playing_lane=r->origin; r->playing_step=-1; r->held_count=0;
        }
    }
    for(int lane=0;lane<SCORE_LANES;lane++) if(score->lanes[lane].active && !score->lanes[lane].source) {
        int found=0,free_slot=-1;
        for(int i=0;i<SCORE_LANES;i++) {
            Runner *r=&s->runners[i];
            if(r->active && r->origin==lane) found=1;
            if(!r->active && free_slot<0) free_slot=i;
        }
        if(!found) runner_start(&s->runners[free_slot],lane,UINT64_MAX);
    }
    s->score=score; order_runners(s);
    // The first head is the master. A new master cannot wait for its own
    // lap, and an empty playing score must be able to accept its first lane.
    if(s->count) {
        Runner *master=&s->runners[s->order[0]];
        if(master->next==UINT64_MAX) master->next=now;
    }
    return 1;
}
void sequencer_stop(Sequencer *s,AudioTime now) {
    s->playing=0; memset(s->offs,0,sizeof(s->offs));
    if(s->sink.stop) s->sink.stop(s->sink.context,now);
}
static void offs_until(Sequencer *s,AudioTime now) {
    // At most one pending gate-off per logical note. The generation token
    // prevents an old gate from releasing a replacement after a steal.
    for(;;) {
        int first=-1;
        for(int i=0;i<SEQUENCER_VOICES;i++) if(s->offs[i].token && s->offs[i].at<=now &&
            (first<0 || s->offs[i].at<s->offs[first].at)) first=i;
        if(first<0) return;
        AudioTime at=s->offs[first].at;
        // Chord gate-offs share a deadline. Drain that group in one pass
        // instead of searching all logical slots again for each of its voices.
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
    if(v.kind==TILE_RELATIVE) { lock(&s->working,v); r->held[r->held_count]=t; r->held_generation[r->held_count++]=s->score->tile_generation[t]; }
    if(v.kind==TILE_JUMP) s->jump=s->score->tiles[t].branch;
    if(v.kind==TILE_NOTE) {
        if(now-s->slice_at>SEQUENCER_HZ/1000) { s->skipped++; return; }
        uint32_t token=s->sink.on(s->sink.context,s->slice_at,v.pitch,s->working);
        if(token) s->offs[token&31]=(NoteOff){s->slice_at+(AudioTime)(r->duration/20)*v.length,token};
    }
}
static int slice(Sequencer *s,AudioTime now,int *budget) {
    while(s->runner_index<s->count) {
        Runner *r=&s->runners[s->order[s->runner_index]];
        if(!s->visiting) {
            s->visiting=1; s->held_index=0; s->jump=-1;
            if(r->next==s->slice_at) {
                r->held_count=0; r->playing_lane=r->lane; r->playing_step=r->step;
                // Tempo and division edits change the step beginning now, not the
                // deadline of a step already sounding or its scheduled gates.
                r->duration=(uint32_t)(240u*SEQUENCER_HZ/s->score->bpm)/s->score->lanes[r->origin].division;
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
            else if(++r->step==s->score->lanes[r->lane].length) {
                r->lane=r->origin; r->step=0; r->lap++;
                if(!s->runner_index) for(int i=0;i<s->count;i++) {
                    Runner *pending=&s->runners[s->order[i]];
                    if(pending->next==UINT64_MAX) pending->next=r->next+r->duration;
                }
            }
            r->next+=r->duration;
        } else {
            while(s->held_index<r->held_count) {
                if(!*budget) return 0;
                --*budget;
                int h=s->held_index++;
                TileId t=r->held[h];
                if(r->held_generation[h]==s->score->tile_generation[t] && s->score->tiles[t].value.kind==TILE_RELATIVE)
                    lock(&s->working,s->score->tiles[t].value);
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
            for(int i=0;i<s->count;i++) if(s->runners[s->order[i]].next<at) at=s->runners[s->order[i]].next;
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
