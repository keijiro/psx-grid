#include "score.h"
#include "editor.h"
#include "input.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static void unchanged(Score *s, Score old, ScoreResult actual, ScoreResult expected) {
    assert(actual == expected); assert(memcmp(s,&old,sizeof(old)) == 0);
}
static void model_test(void) {
    Score s; score_init(&s);
    assert(score_at(&s,1,1).kind == CELL_EMPTY);
    assert(score_create(&s,0,0,1) == SCORE_OK);
    assert(score_at(&s,0,0).kind == CELL_HEAD);
    assert(score_at(&s,1,0).kind == CELL_STEP);
    assert(score_at(&s,2,0).kind == CELL_END);
    Score old = s;
    unchanged(&s,old,score_create(&s,2,0,1),SCORE_COLLISION);
    unchanged(&s,old,score_create(&s,127,1,1),SCORE_BOUNDS);
    unchanged(&s,old,score_create(&s,-1,1,1),SCORE_BOUNDS);
    unchanged(&s,old,score_create(&s,0,64,1),SCORE_BOUNDS);
    unchanged(&s,old,score_place(&s,0,0,TILE_NOTE),SCORE_INVALID);
    unchanged(&s,old,score_resize(&s,-1,1),SCORE_INVALID);
    assert(score_create(&s,4,0,1)==SCORE_OK);
    old = s;
    unchanged(&s,old,score_resize(&s,0,3),SCORE_COLLISION);
    assert(score_resize(&s,0,2)==SCORE_OK);
    assert(score_place(&s,2,0,TILE_NOTE)==SCORE_OK);
    assert(score_at(&s,2,0).kind==CELL_TILE);
    old = s;
    unchanged(&s,old,score_resize(&s,0,1),SCORE_TILES);
    unchanged(&s,old,score_resize(&s,0,0),SCORE_BOUNDS);
    unchanged(&s,old,score_place(&s,2,0,TILE_NOTE),SCORE_INVALID);
    assert(score_remove(&s,2,0)==SCORE_OK);
    assert(score_resize(&s,0,1)==SCORE_OK);
    assert(score_delete(&s,0)==SCORE_OK);
    assert(score_create(&s,61,63,64)==SCORE_OK);
    assert(score_at(&s,126,63).kind==CELL_END);
    assert(score_delete(&s,0)==SCORE_OK);
    assert(score_create(&s,62,63,64)==SCORE_OK);
    assert(score_at(&s,127,63).kind==CELL_END);
    old=s;
    unchanged(&s,old,score_resize(&s,0,65),SCORE_BOUNDS);
    unchanged(&s,old,score_create(&s,63,62,64),SCORE_BOUNDS);
    score_init(&s);
    for(int i=0;i<SCORE_LANES;i++) assert(score_create(&s,0,i,64)==SCORE_OK);
    old=s;
    unchanged(&s,old,score_create(&s,0,20,1),SCORE_FULL);
    for(int i=0;i<SCORE_LANES;i++) for(int x=1;x<=64;x++) assert(score_place(&s,x,i,TILE_NOTE)==SCORE_OK);
    for(int i=0;i<SCORE_LANES;i++) assert(score_delete(&s,i)==SCORE_OK);
    Score empty; score_init(&empty); assert(memcmp(&s,&empty,sizeof(s))==0);
}
static void input_test(void) {
    Input i; input_init(&i);
    input_update(&i,1,0);
    InputFrame f=input_update(&i,1,INPUT_RIGHT); assert(f.dx==1);
    for(int n=1;n<INPUT_DELAY;n++) assert(input_update(&i,1,INPUT_RIGHT).dx==0);
    assert(input_update(&i,1,INPUT_RIGHT).dx==1);
    for(int n=1;n<INPUT_INTERVAL;n++) assert(input_update(&i,1,INPUT_RIGHT).dx==0);
    assert(input_update(&i,1,INPUT_RIGHT).dx==1);
    assert(input_update(&i,1,INPUT_LEFT).dx==-1);
    f=input_update(&i,1,INPUT_RIGHT|INPUT_DOWN); assert(f.dx==1 && f.dy==0);
    f=input_update(&i,1,INPUT_LEFT|INPUT_RIGHT|INPUT_UP); assert(f.dx==0 && f.dy==-1);
    f=input_update(&i,1,INPUT_UP|INPUT_DOWN); assert(!f.dx && !f.dy);
    assert(input_update(&i,1,INPUT_CROSS).cross);
    assert(!input_update(&i,1,INPUT_CROSS).cross);
    input_reset_repeat(&i);
    assert(!input_update(&i,1,INPUT_RIGHT|INPUT_CROSS).cross);
    assert(!input_update(&i,0,INPUT_CROSS).connected);
    assert(!input_update(&i,1,INPUT_CROSS|INPUT_RIGHT).cross);
    for(int n=0;n<60;n++) { f=input_update(&i,1,INPUT_CROSS|INPUT_RIGHT); assert(!f.cross && !f.dx); }
    input_update(&i,1,0); assert(input_update(&i,1,INPUT_CROSS).cross);
}
static void step(Editor *e,int dx,int dy,int cross,int circle) {
    InputFrame f={1,dx,dy,cross,circle}; editor_update(e,f);
}
static void editor_test(void) {
    Editor e; editor_init(&e);
    EditorAction menu[3]; assert(editor_menu(&e,menu)==2 && menu[0]==ACTION_CREATE);
    step(&e,0,0,1,0); assert(e.mode==EDIT_MENU && !e.score.lanes[0].active);
    step(&e,1,0,0,0); assert(e.x==1);
    step(&e,0,0,1,0); assert(e.mode==EDIT_PLANE && e.score.lanes[0].length==16);
    assert(editor_menu(&e,menu)==3 && menu[0]==ACTION_LENGTH && menu[1]==ACTION_DELETE);
    step(&e,1,0,0,0); assert(editor_menu(&e,menu)==3 && menu[0]==ACTION_PLACE);
    step(&e,0,0,1,0); step(&e,0,0,1,0); assert(e.mode==EDIT_PICKER);
    step(&e,0,0,1,0); assert(score_at(&e.score,2,1).kind==CELL_TILE);
    assert(editor_menu(&e,menu)==3 && menu[0]==ACTION_REMOVE);
    step(&e,0,0,1,0); step(&e,0,0,1,0); assert(score_at(&e.score,2,1).kind==CELL_STEP);
    step(&e,0,0,1,0); step(&e,0,1,0,0); step(&e,0,0,1,0);
    assert(e.mode==EDIT_LENGTH);
    Score old=e.score;
    step(&e,1,0,0,0); assert(e.candidate==17 && memcmp(&old,&e.score,sizeof(old))==0);
    step(&e,0,0,0,1); assert(e.mode==EDIT_PLANE && memcmp(&old,&e.score,sizeof(old))==0);
    step(&e,-1,0,0,0); step(&e,0,0,1,0); step(&e,0,0,1,0);
    step(&e,-1,0,0,0); step(&e,0,0,1,0); assert(e.score.lanes[0].length==15 && e.x==1);
    step(&e,0,0,1,0); step(&e,0,1,0,0); step(&e,0,0,1,0);
    assert(e.mode==EDIT_DELETE && !e.confirm);
    step(&e,0,0,1,0); assert(e.score.lanes[0].active);
    step(&e,0,0,1,0); step(&e,0,1,0,0); step(&e,0,0,1,0);
    step(&e,1,0,0,0); step(&e,0,0,0,1); assert(e.score.lanes[0].active);
    step(&e,0,0,1,0); step(&e,0,1,0,0); step(&e,0,0,1,0);
    step(&e,1,0,0,0); step(&e,0,0,1,0); assert(!e.score.lanes[0].active && e.x==1);
    for(int n=0;n<200;n++) step(&e,1,1,0,0);
    assert(e.x==127 && e.y==63);
    step(&e,0,0,1,0); step(&e,0,0,1,0); assert(!e.score.lanes[0].active);
    for(int n=0;n<200;n++) step(&e,-1,-1,0,0);
    assert(!e.x && !e.y);
    InputFrame disconnected={0,1,1,1,0}; editor_update(&e,disconnected); assert(e.mode==EDIT_PLANE && e.x==0);
    e.x=100; e.y=60;
    for(int n=0;n<1000;n++) {
        step(&e,0,0,1,0); step(&e,0,0,1,0);
        assert(e.score.lanes[0].active);
        step(&e,0,0,1,0); step(&e,0,1,0,0); step(&e,0,0,1,0);
        step(&e,1,0,0,0); step(&e,0,0,1,0); assert(!e.score.lanes[0].active);
    }
}
static void visual_kind_test(void) {
    Score s; score_init(&s); assert(score_create(&s,0,0,8)==SCORE_OK);
    Score old=s;
    for(int k=-2;k<=TILE_KIND_COUNT+2;k++) {
        if(k>TILE_NONE && k<TILE_KIND_COUNT) continue;
        unchanged(&s,old,score_place(&s,1,0,(TileKind)k),SCORE_INVALID);
    }
    for(int k=TILE_NOTE;k<TILE_KIND_COUNT;k++) {
        old=s;
        unchanged(&s,old,score_place(&s,0,0,(TileKind)k),SCORE_INVALID);
        unchanged(&s,old,score_place(&s,9,0,(TileKind)k),SCORE_INVALID);
        unchanged(&s,old,score_place(&s,10,0,(TileKind)k),SCORE_INVALID);
        assert(score_place(&s,8,0,(TileKind)k)==SCORE_OK);
        assert(s.lanes[0].tiles[7]==k);
        old=s;
        unchanged(&s,old,score_place(&s,8,0,(TileKind)k),SCORE_INVALID);
        unchanged(&s,old,score_resize(&s,0,7),SCORE_TILES);
        assert(score_remove(&s,8,0)==SCORE_OK);
    }
    assert(score_place(&s,8,0,TILE_JUMP)==SCORE_OK);
    assert(score_delete(&s,0)==SCORE_OK);
    assert(score_create(&s,0,0,8)==SCORE_OK);
    for(int i=0;i<SCORE_STEPS;i++) assert(s.lanes[0].tiles[i]==TILE_NONE);
}
// Use the same frame boundary/reset contract as main, so holding Cross across
// both menu transitions and reconnecting in the picker are tested end to end.
static void buttons(Editor *e,Input *i,int connected,uint16_t held) {
    EditorMode before=e->mode;
    editor_update(e,input_update(i,connected,held));
    if(before!=e->mode) input_reset_repeat(i);
}
static void press(Editor *e,Input *i,uint16_t held) {
    buttons(e,i,1,0); buttons(e,i,1,held);
}
static void picker_test(void) {
    Editor e; Input input; editor_init(&e); input_init(&input);
    buttons(&e,&input,1,0);
    press(&e,&input,INPUT_CROSS); press(&e,&input,INPUT_CROSS);
    assert(e.score.lanes[0].active);
    press(&e,&input,INPUT_RIGHT);
    for(int k=TILE_NOTE;k<TILE_KIND_COUNT;k++) {
        Score before=e.score; int x=e.x,y=e.y;
        press(&e,&input,INPUT_CROSS); press(&e,&input,INPUT_CROSS);
        assert(e.mode==EDIT_PICKER && e.tile_candidate==e.last_tile);
        for(int n=0;n<60;n++) buttons(&e,&input,1,INPUT_CROSS);
        assert(e.mode==EDIT_PICKER && memcmp(&before,&e.score,sizeof(before))==0);
        while((int)e.tile_candidate<k) press(&e,&input,INPUT_DOWN);
        press(&e,&input,INPUT_RIGHT); assert(e.x==x && e.y==y);
        buttons(&e,&input,0,0);
        for(int n=0;n<60;n++) buttons(&e,&input,1,INPUT_CROSS|INPUT_DOWN);
        assert(e.mode==EDIT_PICKER && (int)e.tile_candidate==k);
        press(&e,&input,INPUT_CROSS);
        assert(e.mode==EDIT_PLANE && (int)e.last_tile==k);
        assert(e.score.lanes[0].tiles[k-1]==k);
        press(&e,&input,INPUT_RIGHT);
    }
    Score before=e.score;
    press(&e,&input,INPUT_CROSS); press(&e,&input,INPUT_CROSS);
    press(&e,&input,INPUT_UP);
    for(int n=1;n<INPUT_DELAY;n++) buttons(&e,&input,1,INPUT_UP);
    assert(e.tile_candidate==TILE_PROBABILITY);
    buttons(&e,&input,1,INPUT_UP); assert(e.tile_candidate==TILE_CYCLE);
    press(&e,&input,INPUT_CIRCLE);
    assert(e.mode==EDIT_MENU && e.selected==0 && e.last_tile==TILE_JUMP);
    assert(memcmp(&before,&e.score,sizeof(before))==0);
    press(&e,&input,INPUT_CROSS); assert(e.tile_candidate==TILE_JUMP);
    press(&e,&input,INPUT_DOWN); assert(e.tile_candidate==TILE_JUMP);
    for(int n=0;n<10;n++) press(&e,&input,INPUT_UP);
    assert(e.tile_candidate==TILE_NOTE);
    press(&e,&input,INPUT_CIRCLE); press(&e,&input,INPUT_CIRCLE);
    assert(e.mode==EDIT_PLANE && memcmp(&before,&e.score,sizeof(before))==0);
    for(int n=0;n<100;n++) press(&e,&input,INPUT_RIGHT);
    for(int n=0;n<100;n++) press(&e,&input,INPUT_LEFT);
    assert(memcmp(&before,&e.score,sizeof(before))==0);
}
int main(void) { model_test(); input_test(); editor_test(); visual_kind_test(); picker_test(); puts("PASS: score, input, editor (including 1000 edit cycles)"); }
