#include "score.h"
#include <string.h>
const int score_divisions[] = {1,2,3,4,6,8,12,16,24,32,48,64};
// A single scratch score makes multi-object edits atomic without heap allocation
// or large console-stack frames. Model calls are synchronous and non-reentrant.
static Score scratch;
static uint8_t occupied[SCORE_HEIGHT][SCORE_WIDTH];
static int valid(const Score *s, int i) { return i >= 0 && i < SCORE_LANES && s->lanes[i].active; }
void score_init(Score *s) { memset(s,0,sizeof(*s)); s->sound=(SoundSettings){5,5}; }
TileValue score_default(TileKind k) { TileValue v = {k,48,20,4,50,1,0,0,0}; return v; }
const char *score_note_name(int p) { static const char *n[]={"C","C#","D","D#","E","F","F#","G","G#","A","A#","B"}; return n[p%12]; }
static int value_valid(TileValue v) {
    return v.kind > TILE_NONE && v.kind < TILE_KIND_COUNT && v.pitch >= 0 && v.pitch <= 108 &&
        v.length >= 5 && v.length <= 1280 && v.period >= 2 && v.period <= 32 && v.chance >= 0 && v.chance <= 100 && v.lock_mask >= 0 && v.lock_mask <= 3 &&
        v.attack >= -SOUND_MAX_MS && v.attack <= SOUND_MAX_MS &&
        v.release >= -SOUND_MAX_MS && v.release <= SOUND_MAX_MS &&
        ((v.lock_mask & LOCK_ATTACK) || !v.attack) && ((v.lock_mask & LOCK_RELEASE) || !v.release);
}
ScoreResult score_edit(Score *s, TileId id, TileValue v) {
    if (!id || id > SCORE_TILE_CAPACITY || s->tiles[id].value.kind != v.kind || !value_valid(v)) return SCORE_INVALID;
    s->tiles[id].value=v; s->revision++; return SCORE_OK;
}
static int owner(const Score *s, TileId id) {
    for (int i=0;i<SCORE_LANES;i++) if (valid(s,i)) for (int j=0;j<s->lanes[i].length;j++)
        for (TileId t=s->lanes[i].tiles[j];t;t=s->tiles[t].next) if(t==id) return i;
    return -1;
}
int score_division(const Score *s, int i) {
    for(int n=0;n<SCORE_LANES && valid(s,i);n++) {
        if(!s->lanes[i].source) return s->lanes[i].division;
        i=owner(s,s->lanes[i].source);
    }
    return 16;
}
ScoreResult score_set_division(Score *s,int i,int d) {
    if(!valid(s,i) || s->lanes[i].source) return SCORE_INVALID;
    for(int j=0;j<SCORE_DIVISIONS;j++) if(d==score_divisions[j]) { s->lanes[i].division=d; s->revision++; return SCORE_OK; }
    return SCORE_INVALID;
}
Cell score_at(const Score *s,int x,int y) {
    Cell c={CELL_EMPTY,-1,-1,0,0};
    for(int i=0;i<SCORE_LANES;i++) {
        const Lane *l=&s->lanes[i];
        if(!l->active || x<l->x || x>l->x+l->length+1 || y<l->y) continue;
        int step=x-l->x-1, depth=y-l->y;
        if(!depth && (step==-1 || step==l->length)) return (Cell){step==-1?CELL_HEAD:CELL_END,i,step,0,0};
        if(step<0 || step>=l->length) continue;
        TileId t=l->tiles[step];
        for(int d=0;t && d<depth;d++) t=s->tiles[t].next;
        if(t || !depth) return (Cell){t?CELL_TILE:CELL_STEP,i,step,depth,t};
    }
    return c;
}
Cell score_resolve(const Score *s,int x,int y) {
    Cell c=score_at(s,x,y);
    if(c.kind!=CELL_EMPTY) return c;
    if(y>0) { c=score_at(s,x,y-1); if(c.kind==CELL_TILE) { c.kind=CELL_STEP; c.depth++; c.tile=0; return c; } }
    return (Cell){CELL_EMPTY,-1,-1,0,0};
}
static ScoreResult mark(int x,int y) {
    if(x<0 || x>=SCORE_WIDTH || y<0 || y>=SCORE_HEIGHT) return SCORE_BOUNDS;
    if(occupied[y][x]) return SCORE_COLLISION;
    occupied[y][x]=1; return SCORE_OK;
}
static ScoreResult validate(const Score *s) {
    memset(occupied,0,sizeof(occupied));
    for(int i=0;i<SCORE_LANES;i++) if(valid(s,i)) {
        const Lane *l=&s->lanes[i];
        if(l->length<1 || l->length>SCORE_STEPS) return SCORE_BOUNDS;
        for(int j=-1;j<=l->length;j++) {
            ScoreResult r=mark(l->x+j+1,l->y); if(r) return r;
            if(j<0 || j==l->length) continue;
            int d=0;
            for(TileId t=l->tiles[j];t;t=s->tiles[t].next) if(d++) {
                r=mark(l->x+j+1,l->y+d-1); if(r) return r;
            }
        }
        int parent=i;
        for(int n=0;s->lanes[parent].source;n++) {
            parent=owner(s,s->lanes[parent].source);
            if(parent<0) return SCORE_INVALID;
            if(parent==i || n>=SCORE_LANES) return SCORE_CYCLE;
        }
    }
    return SCORE_OK;
}
static ScoreResult commit(Score *s) { ScoreResult r=validate(&scratch); if(!r) { scratch.revision=s->revision+1; *s=scratch; } return r; }
static int new_lane(Score *s,int x,int y,int n,TileId source) {
    for(int i=0;i<SCORE_LANES;i++) if(!valid(s,i)) {
        Lane *l=&s->lanes[i]; memset(l,0,sizeof(*l));
        l->active=1; l->x=x; l->y=y; l->length=n; l->division=16; l->source=source; return i;
    }
    return -1;
}
ScoreResult score_can_create(const Score *s,int x,int y,int n) {
    scratch=*s; if(new_lane(&scratch,x,y,n,0)<0) return SCORE_FULL; return validate(&scratch);
}
ScoreResult score_create(Score *s,int x,int y,int n) { ScoreResult r=score_can_create(s,x,y,n); if(!r) { scratch.revision=s->revision+1; *s=scratch; } return r; }
ScoreResult score_can_resize(const Score *s,int i,int n) {
    if(!valid(s,i)) return SCORE_INVALID;
    if(n<1 || n>SCORE_STEPS) return SCORE_BOUNDS;
    for(int j=n;j<s->lanes[i].length;j++) if(s->lanes[i].tiles[j]) return SCORE_TILES;
    scratch=*s; scratch.lanes[i].length=n; return validate(&scratch);
}
ScoreResult score_resize(Score *s,int i,int n) { ScoreResult r=score_can_resize(s,i,n); if(!r) { scratch.revision=s->revision+1; *s=scratch; } return r; }
static TileId *link_at(Score *s,Cell c) {
    TileId *p=&s->lanes[c.lane].tiles[c.step];
    for(int d=0;d<c.depth && *p;d++) p=&s->tiles[*p].next;
    return p;
}
static void erase_lane(Score *s,int i);
static void erase_tile(Score *s,TileId id) {
    Tile *t=&s->tiles[id];
    if(t->value.kind==TILE_JUMP) erase_lane(s,t->branch);
    memset(t,0,sizeof(*t));
}
static void erase_lane(Score *s,int i) {
    Lane *l=&s->lanes[i];
    for(int j=0;j<l->length;j++) for(TileId t=l->tiles[j],next;t;t=next) { next=s->tiles[t].next; erase_tile(s,t); }
    memset(l,0,sizeof(*l));
}
ScoreResult score_delete(Score *s,int i) {
    if(!valid(s,i)) return SCORE_INVALID;
    TileId source=s->lanes[i].source;
    if(source) {
        int p=owner(s,source);
        for(int j=0;j<s->lanes[p].length;j++) for(TileId *t=&s->lanes[p].tiles[j];*t;t=&s->tiles[*t].next)
            if(*t==source) { *t=s->tiles[source].next; erase_tile(s,source); s->revision++; return SCORE_OK; }
    }
    erase_lane(s,i); s->revision++; return SCORE_OK;
}
ScoreResult score_remove(Score *s,int x,int y) {
    Cell c=score_at(s,x,y); if(c.kind!=CELL_TILE) return SCORE_INVALID;
    TileId *p=link_at(s,c); *p=s->tiles[c.tile].next; erase_tile(s,c.tile); s->revision++; return SCORE_OK;
}
static ScoreResult insert(Score *s,Cell c,TileValue v) {
    if(!value_valid(v) || (c.kind!=CELL_STEP && c.kind!=CELL_END)) return SCORE_INVALID;
    Lane *l=&s->lanes[c.lane];
    if(c.kind==CELL_END) { if(l->length==SCORE_STEPS) return SCORE_BOUNDS; l->length++; }
    TileId id=1; while(id<=SCORE_TILE_CAPACITY && s->tiles[id].value.kind) id++;
    if(id>SCORE_TILE_CAPACITY) return SCORE_FULL;
    TileId *p=link_at(s,c); s->tiles[id]=(Tile){v,*p,-1}; *p=id;
    if(v.kind==TILE_JUMP) {
        // Keep one clear row below every existing stack so the branch head
        // cannot look like another tile in an unrelated chord.
        int bottom=0;
        for(int i=0;i<SCORE_LANES;i++) if(valid(s,i)) {
            if(s->lanes[i].y>bottom) bottom=s->lanes[i].y;
            for(int j=0;j<s->lanes[i].length;j++) {
                int y=s->lanes[i].y;
                for(TileId t=s->lanes[i].tiles[j];t;t=s->tiles[t].next,y++) if(y>bottom) bottom=y;
            }
        }
        int b=new_lane(s,0,bottom+2,4,id); if(b<0) return SCORE_FULL;
        s->tiles[id].branch=b;
        for(int y=bottom+2;y<SCORE_HEIGHT;y++) for(int x=0;x<SCORE_WIDTH-5;x++) {
            s->lanes[b].x=x; s->lanes[b].y=y;
            if(validate(s)==SCORE_OK) return SCORE_OK;
        }
        return SCORE_BOUNDS;
    }
    return SCORE_OK;
}
ScoreResult score_place_value(Score *s,int x,int y,TileValue v) {
    scratch=*s; ScoreResult r=insert(&scratch,score_resolve(s,x,y),v); return r?r:commit(s);
}
ScoreResult score_place(Score *s,int x,int y,TileKind k) { return score_place_value(s,x,y,score_default(k)); }
void score_copy(const Score *s,int x,int y,Clipboard *b) {
    Cell c=score_at(s,x,y); if(c.kind!=CELL_TILE) return;
    Clipboard copy={0};
    for(TileId t=c.tile;t;t=s->tiles[t].next) if(s->tiles[t].value.kind!=TILE_JUMP) copy.values[copy.count++]=s->tiles[t].value;
    if(copy.count) *b=copy;
}
ScoreResult score_paste(Score *s,int x,int y,const Clipboard *b) {
    if(b->count<1 || b->count>SCORE_HEIGHT) return SCORE_INVALID;
    scratch=*s;
    for(int i=0;i<b->count;i++) {
        if(b->values[i].kind==TILE_JUMP) return SCORE_INVALID;
        ScoreResult r=insert(&scratch,score_resolve(&scratch,x,y+i),b->values[i]); if(r) return r;
    }
    return commit(s);
}
static ScoreResult move(const Score *s,int sx,int sy,int x,int y) {
    Cell a=score_at(s,sx,sy), b=score_resolve(s,x,y);
    scratch=*s;
    if(a.kind!=CELL_HEAD && a.kind!=CELL_TILE) return SCORE_INVALID;
    if(sx==x && sy==y) return SCORE_OK;
    if(a.kind==CELL_HEAD) { scratch.lanes[a.lane].x=x; scratch.lanes[a.lane].y=y; return validate(&scratch); }
    if(b.kind!=CELL_TILE && b.kind!=CELL_STEP && b.kind!=CELL_END) return SCORE_INVALID;
    TileId *from=link_at(&scratch,a), tail=a.tile;
    int same=a.lane==b.lane && a.step==b.step;
    // Within a stack the target depth is the final index after extraction.
    // Across steps the complete suffix is detached and inserted as one chain.
    if(same) { *from=scratch.tiles[tail].next; }
    else { *from=0; while(scratch.tiles[tail].next) tail=scratch.tiles[tail].next; }
    if(b.kind==CELL_END) { if(scratch.lanes[b.lane].length==SCORE_STEPS) return SCORE_BOUNDS; scratch.lanes[b.lane].length++; }
    TileId *to=link_at(&scratch,b); scratch.tiles[tail].next=*to; *to=a.tile;
    return validate(&scratch);
}
MovePlan score_plan_move(const Score *s,int sx,int sy,int x,int y) { return (MovePlan){sx,sy,x,y,move(s,sx,sy,x,y)}; }
ScoreResult score_apply_move(Score *s,MovePlan p) { if(p.sx==p.x && p.sy==p.y) return move(s,p.sx,p.sy,p.x,p.y); ScoreResult r=move(s,p.sx,p.sy,p.x,p.y); if(!r) { scratch.revision=s->revision+1; *s=scratch; } return r; }
const char *score_tile_label(TileKind k) { static const char *v[]={"EMPTY","NOTE","CYCLE GATE","PROBABILITY GATE","JUMP","RELATIVE LOCK"}; return k>=0 && k<TILE_KIND_COUNT?v[k]:"?"; }
const char *score_message(ScoreResult r) { static const char *v[]={"","LIMIT / EDGE","COLLISION","CAPACITY FULL","REMOVE TRAILING TILES FIRST","INVALID CELL","BRANCH CYCLE"}; return v[r]; }

ScoreResult score_set_sound(Score *s,SoundSettings sound) {
    if(sound.attack<0 || sound.attack>SOUND_MAX_MS || sound.release<0 || sound.release>SOUND_MAX_MS) return SCORE_INVALID;
    s->sound=sound; s->revision++; return SCORE_OK;
}
