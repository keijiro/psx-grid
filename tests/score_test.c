#include "editor.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static Score s,before;
static Editor e;
static void snapshot(void) { before=s; }
static void unchanged(ScoreResult r) { assert(r!=SCORE_OK); assert(!memcmp(&s,&before,sizeof(s))); }
static TileId id(int x,int y) { return score_at(&s,x,y).tile; }
static void base(void) { score_init(&s); assert(!score_create(&s,0,0,4)); }
static void model(void) {
    base();
    assert(!score_place(&s,1,0,TILE_CYCLE)); assert(!score_place(&s,1,1,TILE_NOTE)); assert(!score_place(&s,1,2,TILE_NOTE));
    TileId gate=id(1,0),a=id(1,1),b=id(1,2);
    assert(score_at(&s,1,2).depth==2 && a!=b);
    TileValue v=s.tiles[a].value; assert(v.pitch==48 && v.length==20);
    v.pitch=108; v.length=1280; assert(!score_edit(&s,a,v));
    snapshot(); v.pitch=109; unchanged(score_edit(&s,a,v)); v.pitch=0; v.length=4; unchanged(score_edit(&s,a,v));
    v.length=5; assert(!score_edit(&s,a,v));
    v=s.tiles[gate].value; v.pattern=0x80000001u; v.period=32; assert(!score_edit(&s,gate,v));
    v.period=2; assert(!score_edit(&s,gate,v)); assert(s.tiles[gate].value.pattern==0x80000001u);
    snapshot(); v.period=1; unchanged(score_edit(&s,gate,v)); v.period=33; unchanged(score_edit(&s,gate,v));
    assert(!score_place(&s,2,0,TILE_PROBABILITY)); TileId prob=id(2,0);
    v=s.tiles[prob].value; v.chance=0; assert(!score_edit(&s,prob,v)); v.chance=100; assert(!score_edit(&s,prob,v));
    snapshot(); v.chance=101; unchanged(score_edit(&s,prob,v)); v.chance=-1; unchanged(score_edit(&s,prob,v));
    snapshot(); unchanged(score_place(&s,1,0,TILE_NOTE)); unchanged(score_resize(&s,0,1));
    MovePlan plan=score_plan_move(&s,1,1,1,2); assert(!memcmp(&s,&before,sizeof(s))); assert(!score_apply_move(&s,plan));
    assert(id(1,1)==b && id(1,2)==a);
    assert(!score_apply_move(&s,score_plan_move(&s,1,2,1,0))); assert(id(1,0)==a && id(1,1)==gate);
    assert(!score_remove(&s,1,1)); assert(id(1,1)==b && !s.tiles[gate].value.kind);
    assert(!score_create(&s,10,0,4));
    assert(!score_apply_move(&s,score_plan_move(&s,1,0,11,0))); assert(!id(1,0) && id(11,0)==a && id(11,1)==b);
    assert(!score_place(&s,1,0,TILE_NOTE)); TileId top=id(1,0);
    assert(!score_apply_move(&s,score_plan_move(&s,11,0,1,0))); assert(id(1,0)==a && id(1,1)==b && id(1,2)==top);
    Clipboard clip={0}; score_copy(&s,1,0,&clip); assert(clip.count==3);
    assert(!score_paste(&s,3,0,&clip)); assert(id(3,0)!=a);
    v=s.tiles[a].value; v.pitch=70; assert(!score_edit(&s,a,v)); assert(s.tiles[id(3,0)].value.pitch==0);
    assert(!score_place(&s,5,0,TILE_NOTE)); assert(s.lanes[0].length==5);
    snapshot(); unchanged(score_apply_move(&s,score_plan_move(&s,0,0,10,0))); unchanged(score_apply_move(&s,score_plan_move(&s,0,0,127,63)));
    assert(!score_apply_move(&s,score_plan_move(&s,0,0,0,0))); assert(!memcmp(&s,&before,sizeof(s)));
    assert(!score_apply_move(&s,score_plan_move(&s,0,0,0,10))); assert(id(1,10)==a);
    snapshot(); unchanged(score_create(&s,1,11,4));
    assert(!score_set_division(&s,0,3)); snapshot(); unchanged(score_set_division(&s,0,5));
    base(); assert(!score_place(&s,1,0,TILE_JUMP)); TileId jump=id(1,0); int branch=s.tiles[jump].branch;
    assert(s.lanes[branch].length==4 && s.lanes[branch].source==jump);
    int bx=s.lanes[branch].x,by=s.lanes[branch].y;
    assert(!score_set_division(&s,0,24)); assert(score_division(&s,branch)==24);
    snapshot(); unchanged(score_set_division(&s,branch,8));
    assert(!score_place(&s,bx+1,by,TILE_JUMP)); int child=s.tiles[id(bx+1,by)].branch;
    snapshot(); unchanged(score_apply_move(&s,score_plan_move(&s,1,0,bx+2,by)));
    unchanged(score_apply_move(&s,score_plan_move(&s,1,0,s.lanes[child].x+1,s.lanes[child].y)));
    Clipboard old=clip; score_copy(&s,1,0,&clip); assert(!memcmp(&clip,&old,sizeof(clip)));
    assert(!score_place(&s,1,1,TILE_NOTE)); score_copy(&s,1,0,&clip); assert(clip.count==1 && clip.values[0].kind==TILE_NOTE);
    assert(!score_apply_move(&s,score_plan_move(&s,bx,by,20,10))); assert(s.lanes[child].x==0 && s.lanes[0].x==0);
    assert(!score_delete(&s,branch)); assert(!s.lanes[branch].active && !s.lanes[child].active && s.tiles[id(1,0)].value.kind==TILE_NOTE);
    assert(!score_place(&s,2,0,TILE_JUMP)); branch=s.tiles[id(2,0)].branch;
    assert(!score_remove(&s,2,0)); assert(!s.lanes[branch].active);
    base(); for(int i=1;i<16;i++) assert(!score_create(&s,0,i*2,4));
    snapshot(); unchanged(score_place(&s,1,0,TILE_JUMP)); unchanged(score_create(&s,20,0,4));
    score_init(&s); assert(!score_create(&s,0,63,4)); snapshot(); unchanged(score_place(&s,1,63,TILE_JUMP));
    assert(!score_place(&s,1,63,TILE_NOTE)); snapshot(); unchanged(score_place(&s,1,64,TILE_NOTE));
    // Fill the actual pool with a 64-by-64 chord matrix; the unused lane still
    // has room, isolating tile capacity from geometric rejection.
    score_init(&s); assert(!score_create(&s,0,0,64)); assert(!score_create(&s,70,0,4));
    for(int x=1;x<=64;x++) for(int y=0;y<64;y++) assert(!score_place(&s,x,y,TILE_NOTE));
    snapshot(); unchanged(score_place(&s,71,0,TILE_NOTE)); unchanged(score_paste(&s,71,0,&clip));
    assert(!score_remove(&s,1,63));
    Clipboard pair={0}; score_copy(&s,1,61,&pair); assert(pair.count==2);
    snapshot(); unchanged(score_paste(&s,71,0,&pair));
    assert(!score_place(&s,71,0,TILE_NOTE));
    base(); assert(!score_place(&s,1,0,TILE_JUMP));
    branch=s.tiles[id(1,0)].branch;
    assert(!score_place(&s,s.lanes[branch].x+1,s.lanes[branch].y,TILE_JUMP));
    assert(!score_delete(&s,0));
    for(int i=0;i<SCORE_LANES;i++) assert(!s.lanes[i].active);
    for(int i=1;i<=SCORE_TILE_CAPACITY;i++) assert(!s.tiles[i].value.kind);
    for(int i=0;i<1000;i++) {
        assert(!score_create(&s,0,0,4)); assert(!score_place(&s,1,0,TILE_NOTE));
        assert(s.tiles[id(1,0)].value.pitch==48); assert(!score_delete(&s,0));
    }
    base(); assert(!score_place(&s,1,0,TILE_NOTE)); assert(!score_place(&s,1,1,TILE_NOTE)); score_copy(&s,1,0,&clip);
    assert(!score_create(&s,10,63,4)); snapshot(); unchanged(score_paste(&s,11,63,&clip));
    assert(!score_create(&s,6,0,4)); snapshot(); unchanged(score_place(&s,5,0,TILE_NOTE));
}
static void generations(void) {
    base();
    uint32_t lane=s.lane_generation[0];
    assert(lane);
    assert(!score_place(&s,1,0,TILE_NOTE));
    TileId tile=id(1,0);
    uint32_t birth=s.tile_generation[tile];
    assert(birth>lane);
    TileValue value=s.tiles[tile].value; value.pitch=60;
    assert(!score_edit(&s,tile,value));
    assert(!score_apply_move(&s,score_plan_move(&s,1,0,2,0)));
    assert(!score_apply_move(&s,score_plan_move(&s,0,0,0,4)));
    assert(s.tile_generation[tile]==birth && s.lane_generation[0]==lane);
    assert(!score_remove(&s,2,4)); assert(!score_place(&s,2,4,TILE_NOTE));
    assert(id(2,4)==tile && s.tile_generation[tile]>birth);
    birth=s.tile_generation[tile];
    assert(!score_delete(&s,0)); assert(!score_create(&s,0,0,4));
    assert(s.lane_generation[0]>birth);
    assert(!score_place(&s,1,0,TILE_NOTE));
    assert(id(1,0)==tile && s.tile_generation[tile]>birth);

    // Failed compound edits must roll back births as well as geometry. A
    // jump needs two births, so exhausting the second must not consume one.
    base(); s.generation=UINT32_MAX-1; snapshot();
    unchanged(score_place(&s,1,0,TILE_JUMP));
    assert(!score_place(&s,1,0,TILE_NOTE));
    assert(s.tile_generation[id(1,0)]==UINT32_MAX);
    snapshot(); unchanged(score_place(&s,2,0,TILE_NOTE));
    unchanged(score_create(&s,10,0,4));
    value=s.tiles[id(1,0)].value; value.pitch=72;
    assert(!score_edit(&s,id(1,0),value));
    assert(!score_apply_move(&s,score_plan_move(&s,1,0,2,0)));
    assert(!score_remove(&s,2,0)); snapshot();
    unchanged(score_place(&s,2,0,TILE_NOTE));
}
static Input input;
static void frame(int connected,int held) { editor_update(&e,input_update(&input,connected,(uint16_t)held)); }
static void tap(int key) { frame(1,key); frame(1,0); }
static void action(EditorAction action) {
    tap(INPUT_CROSS); assert(e.mode==EDIT_MENU);
    EditorAction items[EDITOR_MENU_ITEMS]; int n=editor_menu(&e,items),i=0;
    while(i<n && items[i]!=action) i++; assert(i<n);
    for(int j=0;j<i;j++) tap(INPUT_DOWN);
    tap(INPUT_CROSS);
}
static void controls(void) {
    editor_init(&e); input_init(&input); frame(1,0);
    frame(1,INPUT_CROSS); assert(e.mode==EDIT_PLANE); frame(1,INPUT_CROSS); assert(e.mode==EDIT_PLANE);
    frame(1,0); assert(e.mode==EDIT_MENU); tap(INPUT_CROSS); assert(e.score.lanes[0].active && e.mode==EDIT_PLANE);
    tap(INPUT_RIGHT); action(ACTION_PLACE); assert(e.mode==EDIT_PICKER); tap(INPUT_CROSS); assert(e.mode==EDIT_PLANE);
    TileId a=score_at(&e.score,e.x,e.y).tile; assert(a);
    action(ACTION_PITCH); tap(INPUT_RIGHT); tap(INPUT_UP); tap(INPUT_CROSS); assert(e.score.tiles[a].value.pitch==61);
    action(ACTION_DURATION); tap(INPUT_UP); tap(INPUT_RIGHT); tap(INPUT_CROSS); assert(e.score.tiles[a].value.length==41);
    action(ACTION_PITCH); tap(INPUT_RIGHT); tap(INPUT_CIRCLE); tap(INPUT_CIRCLE); assert(e.score.tiles[a].value.pitch==61);
    tap(INPUT_DOWN); action(ACTION_PLACE); tap(INPUT_CROSS); assert(e.score.tiles[score_at(&e.score,e.x,e.y).tile].value.pitch==61);
    e.y=1; before=e.score;
    frame(1,INPUT_CROSS|INPUT_RIGHT); assert(e.mode==EDIT_MOVE && e.source_x==2 && e.x==3);
    assert(!memcmp(&before,&e.score,sizeof(before))); frame(1,0); assert(e.mode==EDIT_PLANE && score_at(&e.score,3,1).tile==a);
    before=e.score; frame(1,INPUT_CROSS|INPUT_UP); frame(1,0); assert(e.x==3 && e.y==1 && !memcmp(&before,&e.score,sizeof(before)));
    frame(1,INPUT_CROSS|INPUT_RIGHT); frame(1,INPUT_CROSS|INPUT_CIRCLE); frame(1,0); assert(e.x==3 && !memcmp(&before,&e.score,sizeof(before)));
    frame(1,INPUT_CROSS|INPUT_RIGHT); frame(0,0); assert(e.mode==EDIT_PLANE && e.x==3);
    frame(1,INPUT_CROSS|INPUT_RIGHT); frame(1,INPUT_RIGHT); frame(1,INPUT_RIGHT); assert(e.x==3);
    frame(1,0); tap(INPUT_RIGHT); assert(e.x==4);
    frame(1,INPUT_CROSS|INPUT_RIGHT); frame(1,0); assert(e.mode==EDIT_PLANE && e.x==5);
    Input i; input_init(&i); input_update(&i,1,0); InputFrame f=input_update(&i,1,INPUT_CROSS|INPUT_RIGHT);
    assert(f.cross && f.cross_held && f.dx==1);
    for(int j=1;j<INPUT_DELAY;j++) assert(!input_update(&i,1,INPUT_CROSS|INPUT_RIGHT).dx);
    assert(input_update(&i,1,INPUT_CROSS|INPUT_RIGHT).dx==1);
    for(int j=1;j<INPUT_INTERVAL;j++) assert(!input_update(&i,1,INPUT_CROSS|INPUT_RIGHT).dx);
    assert(input_update(&i,1,INPUT_CROSS|INPUT_RIGHT).dx==1);
    f=input_update(&i,1,0); assert(f.cross_released && !f.cross_held);
    input_reset_repeat(&i); assert(input_update(&i,1,INPUT_RIGHT).dx==1);
    input_reset_repeat(&i);
    for(int j=1;j<INPUT_DELAY;j++) assert(!input_update(&i,1,INPUT_RIGHT).dx);
    assert(input_update(&i,1,INPUT_RIGHT).dx==1);
    input_reset_repeat(&i); assert(input_update(&i,1,INPUT_DOWN).dy==1);
    e.x=3; e.y=1; action(ACTION_COPY); assert(e.clipboard.count==2);
    e.x=4; action(ACTION_PASTE); assert(score_at(&e.score,4,1).tile!=a);
    e.x=1; action(ACTION_DELETE); assert(!e.confirm); tap(INPUT_CROSS); assert(e.score.lanes[0].active);
    tap(INPUT_CIRCLE);
    e.x=6; e.y=1; action(ACTION_PLACE); tap(INPUT_DOWN); tap(INPUT_CROSS);
    action(ACTION_PATTERN); tap(INPUT_CROSS); assert(e.value.pattern==0);
    tap(INPUT_DOWN); assert(e.pattern_cursor==4); tap(INPUT_CROSS);
    assert(e.score.tiles[score_at(&e.score,6,1).tile].value.pattern==0);
}
int main(void) { model(); generations(); controls(); puts("PASS: model transactions, properties, stacks, branches, capacity and input/editor gestures"); }
