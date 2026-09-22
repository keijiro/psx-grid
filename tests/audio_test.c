#include "audio.h"
#include "editor.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static Score score, snapshot;
static Sequencer seq;
static Audio audio;
static Editor editor;
typedef struct { AudioTime at; int pitch; SoundSettings sound; } Event;
static Event events[4096];
static AudioTime offs[4096];
static int count, off_count, slot, levels[24], pitches[24], starts, stops;
static uint32_t serial;
static uint32_t note(void *ctx,AudioTime at,int pitch,SoundSettings sound) {
    (void)ctx; assert(count<4096); events[count++]=(Event){at,pitch,sound};
    return (++serial<<5)|(slot++%24);
}
static void off(void *ctx,AudioTime at,uint32_t token) { (void)ctx; (void)token; offs[off_count++]=at; }
static void stop(void *ctx,AudioTime at) { (void)ctx; (void)at; }
static NoteSink trace={NULL,note,off,stop,NULL};
static void base(int length) { score_init(&score); assert(!score_create(&score,0,0,length)); count=off_count=slot=0; }
static void put(int x,int y,TileValue v) { assert(!score_place_value(&score,x,y,v)); }
static TileValue relative(int a,int r) { TileValue v=score_default(TILE_RELATIVE); v.lock_mask=3; v.attack=a; v.release=r; return v; }
static void start(void) { snapshot=score; sequencer_start(&seq,&snapshot,trace,0); sequencer_service(&seq,0); }
static void timing(void) {
    for(int d=0;d<SCORE_DIVISIONS;d++) {
        base(16); assert(!score_set_division(&score,0,score_divisions[d]));
        for(int i=1;i<=16;i++) put(i,0,score_default(TILE_NOTE));
        start(); uint32_t duration=2*SEQUENCER_HZ/score_divisions[d];
        for(int i=1;i<2048;i++) sequencer_service(&seq,(AudioTime)i*duration);
        assert(count==2048 && off_count==2047);
        assert(events[2047].at==(AudioTime)2047*2*SEQUENCER_HZ/score_divisions[d]);
        assert(offs[2046]==events[2047].at);
    }
    base(2); TileValue v=score_default(TILE_NOTE); v.length=5; put(1,0,v);
    v.length=45; put(1,1,v); start(); AudioTime duration=SEQUENCER_HZ/8;
    sequencer_service(&seq,duration/4); assert(off_count==1 && offs[0]==duration/4);
    sequencer_service(&seq,duration); sequencer_service(&seq,duration*2);
    sequencer_service(&seq,duration*9/4); assert(off_count==3 && offs[1]==duration*9/4);
    base(1); start(); assert(seq.playing && !count);
    sequencer_stop(&seq,0); assert(!seq.playing);
}
static void locks(void) {
    base(2); put(1,0,score_default(TILE_NOTE)); put(1,1,relative(100,-5));
    put(1,2,score_default(TILE_NOTE)); put(1,3,relative(16000,16000));
    put(1,4,relative(-100,-100)); put(1,5,score_default(TILE_NOTE));
    start(); assert(count==3);
    assert(events[0].sound.attack==5 && events[0].sound.release==5);
    assert(events[1].sound.attack==105 && events[1].sound.release==0);
    assert(events[2].sound.attack==15900 && events[2].sound.release==15900);
    sequencer_service(&seq,SEQUENCER_HZ/4); // Drain two boundaries; no accumulation.
    assert(events[count-3].sound.attack==5);
    base(2); assert(!score_set_division(&score,0,8)); put(1,0,relative(100,200));
    assert(!score_create(&score,0,4,4)); assert(!score_set_division(&score,1,16));
    for(int i=1;i<=4;i++) put(i,4,score_default(TILE_NOTE));
    start(); assert(count==1 && events[0].sound.attack==105);
    sequencer_service(&seq,SEQUENCER_HZ/8); assert(events[1].sound.attack==105);
    sequencer_service(&seq,SEQUENCER_HZ/4); assert(events[2].sound.attack==5);
    sequencer_service(&seq,3*SEQUENCER_HZ/8); assert(events[3].sound.attack==5);
    sequencer_service(&seq,SEQUENCER_HZ/2); assert(events[4].sound.release==205);
    // Lower held locks must never affect fresh notes above them.
    base(2); put(1,0,score_default(TILE_NOTE)); put(2,0,score_default(TILE_NOTE));
    assert(!score_create(&score,0,4,1)); assert(!score_set_division(&score,1,8)); put(1,4,relative(100,100));
    start(); sequencer_service(&seq,SEQUENCER_HZ/8); assert(events[1].sound.attack==5);
    // A failed gate keeps the locks above it, never those below it.
    base(1); put(1,0,relative(30,40)); TileValue gate=score_default(TILE_PROBABILITY); gate.chance=0;
    put(1,1,gate); put(1,2,relative(500,500)); put(1,3,score_default(TILE_NOTE));
    assert(!score_create(&score,0,6,1)); put(1,6,score_default(TILE_NOTE)); start();
    assert(count==1 && events[0].sound.attack==35 && seq.runners[0].held_count==1);
    uint32_t random=seq.random; sequencer_service(&seq,1); assert(seq.random==random);
    // Snapshot values and base are immutable after committed editor changes.
    assert(!score_set_sound(&score,(SoundSettings){999,999}));
    assert(!score_remove(&score,1,0)); sequencer_service(&seq,SEQUENCER_HZ/8);
    assert(events[1].sound.attack==35);
}
static void gates_branches(void) {
    base(1); TileValue gate=score_default(TILE_CYCLE); gate.period=3; gate.pattern=5;
    put(1,0,gate); put(1,1,score_default(TILE_NOTE)); start();
    for(int i=1;i<6;i++) sequencer_service(&seq,(AudioTime)i*SEQUENCER_HZ/8);
    assert(count==4 && events[1].at==SEQUENCER_HZ/4);
    for(int chance=0;chance<=100;chance+=50) {
        base(1); gate=score_default(TILE_PROBABILITY); gate.chance=chance; put(1,0,gate); put(1,1,score_default(TILE_NOTE));
        start(); for(int i=1;i<100;i++) sequencer_service(&seq,(AudioTime)i*SEQUENCER_HZ/8);
        int n=count; uint32_t random=seq.random; Event saved[100]; memcpy(saved,events,n*sizeof(Event));
        count=off_count=0; sequencer_start(&seq,&snapshot,trace,0);
        for(int i=0;i<100;i++) sequencer_service(&seq,(AudioTime)i*SEQUENCER_HZ/8);
        assert(count==n && random==seq.random && !memcmp(events,saved,n*sizeof(Event)));
        assert(chance==50 ? n>20 && n<80 : n==chance);
    }
    base(2); put(1,0,score_default(TILE_JUMP)); int first=score.tiles[score_at(&score,1,0).tile].branch;
    assert(!score_apply_move(&score,score_plan_move(&score,score.lanes[first].x,score.lanes[first].y,20,10)));
    // Two reached jumps: the lower destination wins, but a note below both sounds.
    put(1,1,score_default(TILE_JUMP)); int second=score.tiles[score_at(&score,1,1).tile].branch;
    put(1,2,score_default(TILE_NOTE)); assert(!score_resize(&score,second,1));
    Lane b=score.lanes[second]; put(b.x+1,b.y,score_default(TILE_JUMP));
    int child=score.tiles[score_at(&score,b.x+1,b.y).tile].branch;
    assert(!score_resize(&score,child,1)); b=score.lanes[child];
    TileValue v=score_default(TILE_NOTE); v.pitch=72; put(b.x+1,b.y,v);
    start(); assert(seq.count==1 && count==1 && seq.runners[0].lane==second && first!=second);
    sequencer_service(&seq,SEQUENCER_HZ/8); assert(seq.runners[0].lane==child);
    sequencer_service(&seq,SEQUENCER_HZ/4); assert(events[1].pitch==72 && seq.runners[0].lane==0 && seq.runners[0].lap==1);
    sequencer_service(&seq,3*SEQUENCER_HZ/8); assert(events[2].pitch==48);
    // Order is the origin head position, including x ties, not pool ID.
    base(1); assert(!score_create(&score,8,0,1)); put(9,0,relative(100,100)); put(1,0,score_default(TILE_NOTE));
    assert(!score_apply_move(&score,score_plan_move(&score,0,0,0,4))); start();
    assert(seq.runners[0].origin==1 && events[0].sound.attack==105);
}
static void driver_start(void *ctx,int voice,int pitch) { (void)ctx; pitches[voice]=pitch; }
static void driver_volume(void *ctx,int voice,int level) { (void)ctx; assert(level>=0 && level<=AUDIO_LEVEL); levels[voice]=level; }
static void driver_flush(void *ctx,uint32_t on,uint32_t off_bits) { (void)ctx; assert(!(on&off_bits)); starts+=(on!=0); stops+=(off_bits!=0); }
static void voices(void) {
    audio_init(&audio,(AudioDriver){NULL,driver_start,driver_volume,driver_flush}); NoteSink sink=audio_sink(&audio);
    uint32_t tokens[24];
    for(int i=0;i<24;i++) tokens[i]=sink.on(sink.context,0,48+i,(SoundSettings){100,200});
    sink.advance(sink.context,audio_ms(50)); assert(levels[0]>=255 && levels[0]<=256 && starts==1);
    sink.off(sink.context,audio_ms(50),tokens[0]); sink.advance(sink.context,audio_ms(150));
    assert(levels[0]>=127 && levels[0]<=129);
    uint32_t replacement=sink.on(sink.context,audio_ms(150),80,(SoundSettings){0,5});
    assert((replacement&31)==0 && audio.steals==1);
    sink.off(sink.context,audio_ms(160),tokens[0]); assert(!audio.voices[0].releasing);
    replacement=sink.on(sink.context,audio_ms(160),81,(SoundSettings){0,0}); assert((replacement&31)==1);
    sink.off(sink.context,audio_ms(160),replacement); sink.advance(sink.context,audio_ms(160)); assert(!audio.voices[1].active && levels[1]==0);
    sink.on(sink.context,audio_ms(161),82,(SoundSettings){16000,16000}); assert(audio.voices[1].pitch==82);
    sink.stop(sink.context,audio_ms(200)); sink.advance(sink.context,audio_ms(205));
    for(int i=0;i<24;i++) assert(!audio.voices[i].active && levels[i]==0);
    assert(stops>0);
    uint32_t old=sink.on(sink.context,audio_ms(1000),48,(SoundSettings){0,0});
    sink.advance(sink.context,audio_ms(1000)); sink.off(sink.context,audio_ms(1000),old);
    uint32_t fresh=sink.on(sink.context,audio_ms(1000),60,(SoundSettings){0,0});
    assert((fresh&31)==(old&31) && fresh!=old);
    sink.off(sink.context,audio_ms(1000),old); sink.advance(sink.context,audio_ms(1000));
    assert(audio.voices[fresh&31].active && levels[fresh&31]==AUDIO_LEVEL);
    sink.stop(sink.context,audio_ms(1001)); sink.advance(sink.context,audio_ms(1006));
    uint32_t long_note=sink.on(sink.context,0,48,(SoundSettings){16000,16000});
    sink.advance(sink.context,audio_ms(8000)); assert(levels[0]>=255 && levels[0]<=256);
    sink.off(sink.context,audio_ms(8000),long_note);
    sink.advance(sink.context,audio_ms(24000)); assert(!audio.voices[0].active && !levels[0]);
    // Sequencer gate-offs carry the release captured at note-on.
    base(2); score.sound=(SoundSettings){0,500}; put(1,0,score_default(TILE_NOTE)); put(2,0,relative(0,-500));
    snapshot=score; sequencer_start(&seq,&snapshot,sink,0); sequencer_service(&seq,0);
    sequencer_service(&seq,SEQUENCER_HZ/8); assert(audio.voices[0].end==SEQUENCER_HZ/8+audio_ms(500));
    sequencer_stop(&seq,SEQUENCER_HZ/8); sequencer_service(&seq,SEQUENCER_HZ/8+audio_ms(5)); assert(!audio.voices[0].active);
}
static void overload(void) {
    base(1); put(1,0,score_default(TILE_NOTE)); start();
    AudioTime now=100*SEQUENCER_HZ/8;
    sequencer_service(&seq,now); assert(seq.overloads==1 && count==1 && seq.skipped==2);
    for(int i=0;i<49;i++) sequencer_service(&seq,now);
    assert(seq.runners[0].lap==101 && count==2 && events[1].at==now && seq.skipped==99);
    sequencer_stop(&seq,now); sequencer_service(&seq,now+SEQUENCER_HZ); assert(count==2);
}
static void model_editor(void) {
    base(4); TileValue v=relative(-16000,16000); put(1,0,v); TileId id=score_at(&score,1,0).tile;
    Clipboard clip={0}; score_copy(&score,1,0,&clip); assert(!score_paste(&score,2,0,&clip));
    v.attack=42; assert(!score_edit(&score,id,v)); assert(score.tiles[score_at(&score,2,0).tile].value.attack==-16000);
    assert(!score_apply_move(&score,score_plan_move(&score,1,0,3,0))); assert(score_at(&score,3,0).tile==id);
    snapshot=score; v.attack=16001; assert(score_edit(&score,id,v)==SCORE_INVALID && !memcmp(&score,&snapshot,sizeof(score)));
    v=relative(1,2); v.lock_mask=0; assert(score_edit(&score,id,v)==SCORE_INVALID);
    assert(score_set_sound(&score,(SoundSettings){-1,0})==SCORE_INVALID);
    assert(score_set_sound(&score,(SoundSettings){0,16001})==SCORE_INVALID);
    editor_init(&editor); assert(!score_create(&editor.score,0,0,2)); assert(!score_place(&editor.score,1,0,TILE_RELATIVE));
    editor.x=1; editor.y=0; editor.target=score_at(&editor.score,1,0).tile;
    editor.value=score_default(TILE_RELATIVE); editor.mode=EDIT_LOCK_ATTACK_ENABLE;
    editor_update(&editor,(InputFrame){.connected=1,.dx=1,.cross=1});
    assert(editor.score.tiles[editor.target].value.lock_mask==LOCK_ATTACK);
    editor.mode=EDIT_LOCK_ATTACK; editor_update(&editor,(InputFrame){.connected=1,.dy=-1,.dx=1,.cross=1});
    assert(editor.score.tiles[editor.target].value.attack==101);
    editor.mode=EDIT_LOCK_ATTACK_ENABLE; editor_update(&editor,(InputFrame){.connected=1,.dx=-1,.cross=1});
    assert(!editor.score.tiles[editor.target].value.lock_mask && !editor.score.tiles[editor.target].value.attack);
    for(int mode=0;mode<EDIT_MODE_COUNT;mode++) {
        editor.mode=mode; editor.gesture=0; editor.value=relative(33,44); snapshot=editor.score;
        editor_update(&editor,(InputFrame){.connected=1,.start=1});
        assert(!memcmp(&editor.score,&snapshot,sizeof(snapshot)));
    }
    editor.mode=EDIT_ATTACK; editor.sound_candidate=(SoundSettings){0,0};
    editor_update(&editor,(InputFrame){.connected=1,.dy=-1,.cross=1}); assert(editor.score.sound.attack==100);
    editor.mode=EDIT_RELEASE; editor.sound_candidate=(SoundSettings){9,16000};
    editor_update(&editor,(InputFrame){.connected=1,.circle=1}); assert(editor.score.sound.release==0);
    Input input; input_init(&input); input_update(&input,1,0);
    assert(input_update(&input,1,INPUT_START).start);
    for(int i=0;i<90;i++) assert(!input_update(&input,1,INPUT_START).start);
    assert(!input_update(&input,0,0).start); assert(!input_update(&input,1,INPUT_START).start);
    input_update(&input,1,0); assert(input_update(&input,1,INPUT_START).start);
}
static void dense_and_rollback(void) {
    base(64);
    TileId id=1;
    for(int j=0;j<64;j++) {
        score.lanes[0].tiles[j]=id;
        for(int k=0;k<64;k++,id++) score.tiles[id]=(Tile){relative(1,-1),k==63?0:(TileId)(id+1),-1};
    }
    Clipboard clip={.count=1,.values={0}}; clip.values[0]=relative(12,34);
    assert(!score_remove(&score,64,63));
    clip.count=2; clip.values[1]=relative(56,78); snapshot=score;
    assert(score_paste(&score,65,0,&clip)==SCORE_BOUNDS); assert(!memcmp(&score,&snapshot,sizeof(score)));
    // The first insert consumes the last pool slot; the second must roll back.
    score.lanes[0].length=63; // Restore a valid score by erasing the final stack.
    for(int k=0;k<64;k++) memset(&score.tiles[4033+k],0,sizeof(Tile));
    score.lanes[0].tiles[63]=0;
    for(int k=0;k<63;k++) score.tiles[4033+k]=(Tile){relative(0,0),k==62?0:(TileId)(4034+k),-1};
    assert(!score_create(&score,66,0,1)); score.lanes[1].tiles[0]=4033;
    snapshot=score;
    assert(score_paste(&score,64,0,&clip)==SCORE_FULL); assert(!memcmp(&score,&snapshot,sizeof(score)));
    // Splitting a stack and later replaying its held locks must preserve order.
    base(2); for(int k=0;k<63;k++) put(1,k,relative(1,1)); put(1,63,score_default(TILE_NOTE));
    assert(!score_create(&score,8,0,1)); put(9,0,score_default(TILE_NOTE));
    assert(!score_set_division(&score,1,32));
    snapshot=score; sequencer_start(&seq,&snapshot,trace,0);
    sequencer_service(&seq,0); assert(seq.slicing && count==0);
    sequencer_service(&seq,0); assert(seq.slicing && count==1 && events[0].sound.attack==68);
    sequencer_service(&seq,0); assert(!seq.slicing && count==2 && events[1].sound.release==68);
    for(int i=0;i<3;i++) sequencer_service(&seq,SEQUENCER_HZ/16);
    assert(count==3 && events[2].sound.attack==68);
    for(int i=0;i<3;i++) sequencer_service(&seq,SEQUENCER_HZ/8);
    assert(events[count-1].sound.attack==5);
}
int main(void) {
    timing(); locks(); gates_branches(); voices(); overload(); model_editor(); dense_and_rollback();
    printf("PASS: sequencer timing, locks, gates, branches, voices, overload and START; Score %zu, Sequencer %zu, Audio %zu bytes\n",sizeof(Score),sizeof(Sequencer),sizeof(Audio));
}
