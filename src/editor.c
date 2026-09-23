#include "editor.h"
#include <string.h>
static int clamp(int x,int min,int max) { return x<min?min:x>max?max:x; }
void editor_init(Editor *e) {
    memset(e,0,sizeof(*e)); score_init(&e->score); e->x=e->y=1; e->message="";
    e->last_tile=e->tile_candidate=TILE_NOTE; e->last_note=score_default(TILE_NOTE);
}
int editor_menu(const Editor *e,EditorAction items[EDITOR_MENU_ITEMS]) {
    Cell c=score_resolve(&e->score,e->x,e->y); int n=0;
    if(c.kind==CELL_EMPTY) items[n++]=ACTION_CREATE;
    if(c.kind==CELL_STEP || c.kind==CELL_END) { items[n++]=ACTION_PLACE; items[n++]=ACTION_PASTE; }
    if(c.kind==CELL_TILE) {
        TileKind k=e->score.tiles[c.tile].value.kind;
        if(k==TILE_NOTE) { items[n++]=ACTION_PITCH; items[n++]=ACTION_DURATION; }
        if(k==TILE_CYCLE) { items[n++]=ACTION_PERIOD; items[n++]=ACTION_PATTERN; }
        if(k==TILE_RELATIVE) {
            items[n++]=ACTION_LOCK_ATTACK_ENABLE; items[n++]=ACTION_LOCK_ATTACK;
            items[n++]=ACTION_LOCK_RELEASE_ENABLE; items[n++]=ACTION_LOCK_RELEASE;
        }
        if(k==TILE_PROBABILITY) items[n++]=ACTION_CHANCE;
        items[n++]=ACTION_COPY; items[n++]=ACTION_REMOVE;
    }
    if(c.kind==CELL_HEAD) {
        items[n++]=ACTION_LENGTH;
        if(!e->score.lanes[c.lane].source) { items[n++]=ACTION_DIVISION; items[n++]=ACTION_CHANNEL; items[n++]=ACTION_SOUND; }
        items[n++]=ACTION_DELETE;
    }
    items[n++]=ACTION_CLOSE; return n;
}
const char *editor_action_label(EditorAction a) {
    static const char *labels[]={"NEW LANE","CREATE TILE","DELETE TILE","LANE LENGTH","DELETE LANE","CLOSE","COPY STACK","PASTE STACK","PITCH","NOTE LENGTH","CYCLE PERIOD","CYCLE PATTERN","CHANCE","STEP DIVISION","SOUND","CHANNEL","ATTACK ENABLE","ATTACK OFFSET","RELEASE ENABLE","RELEASE OFFSET"};
    return labels[a];
}
EditorMode editor_sound_parent(EditorMode mode) {
    switch(mode) {
    case EDIT_WAVE_A: case EDIT_WAVE_B: return EDIT_WAVES;
    case EDIT_ATTACK: case EDIT_RELEASE: return EDIT_AMPLITUDE;
    case EDIT_MIX_ATTACK: case EDIT_MIX_RELEASE: return EDIT_MIX;
    case EDIT_PITCH_SWEEP: case EDIT_PITCH_DECAY: return EDIT_SWEEP;
    case EDIT_WAVES: case EDIT_AMPLITUDE: case EDIT_MIX: case EDIT_SWEEP: return EDIT_SOUND;
    default: return EDIT_MENU;
    }
}
static void cancel_move(Editor *e) { e->x=e->source_x; e->y=e->source_y; e->mode=EDIT_PLANE; e->gesture=0; e->message="CANCELLED"; }
static void finish(Editor *e,ScoreResult r) { e->message=score_message(r); if(!r) e->mode=EDIT_PLANE; }
void editor_update(Editor *e,InputFrame f) {
    if(!f.connected) { if(e->gesture || e->mode==EDIT_MOVE) cancel_move(e); return; }
    if(f.select) {
        int close=e->mode==EDIT_MAIN;
        if(e->gesture || e->mode==EDIT_MOVE) cancel_move(e);
        e->gesture=0; e->mode=close?EDIT_PLANE:EDIT_MAIN; e->selected=0; e->message="";
        return;
    }
    if(e->mode>=EDIT_MAIN) {
        EditorMode parent=e->mode==EDIT_MAIN?EDIT_PLANE:e->mode==EDIT_SOUND_REVERB?EDIT_SOUND:
            (e->mode==EDIT_REVERB_SIZE || e->mode==EDIT_REVERB_AMOUNT)?EDIT_REVERB:EDIT_MAIN;
        if(f.circle) { e->mode=parent; e->selected=0; return; }
        if(e->mode==EDIT_MAIN || e->mode==EDIT_REVERB) {
            e->selected=clamp(e->selected+f.dy,0,2);
            if(f.cross) {
                if(e->selected==2) e->mode=parent;
                else if(e->mode==EDIT_MAIN) {
                    e->mode=e->selected?EDIT_REVERB:EDIT_BPM; e->candidate=e->score.bpm;
                } else {
                    e->mode=e->selected?EDIT_REVERB_AMOUNT:EDIT_REVERB_SIZE;
                    e->candidate=e->selected?e->score.reverb.amount:e->score.reverb.size;
                }
                e->selected=0;
            }
        } else {
            int min=e->mode==EDIT_BPM?SCORE_MIN_BPM:0;
            int max=e->mode==EDIT_BPM?SCORE_MAX_BPM:e->mode==EDIT_REVERB_AMOUNT?100:e->mode==EDIT_REVERB_SIZE?2:1;
            int step=(e->mode==EDIT_BPM || e->mode==EDIT_REVERB_AMOUNT)?10:1;
            e->candidate=clamp(e->candidate+f.dx-f.dy*step,min,max);
            if(f.cross) {
                if(e->mode==EDIT_BPM) score_set_bpm(&e->score,e->candidate);
                else if(e->mode==EDIT_SOUND_REVERB) {
                    SoundSettings sound=e->score.sounds[e->sound_channel]; sound.reverb=e->candidate; score_set_sound(&e->score,e->sound_channel,sound);
                } else {
                    ReverbSettings reverb=e->score.reverb;
                    if(e->mode==EDIT_REVERB_SIZE) reverb.size=e->candidate; else reverb.amount=e->candidate;
                    score_set_reverb(&e->score,reverb);
                }
                e->mode=parent; e->selected=0;
            }
        }
        return;
    }
    if(e->mode==EDIT_PLANE || e->mode==EDIT_MOVE) {
        // Capture before applying this frame's direction, including simultaneous X.
        if(f.cross && e->mode==EDIT_PLANE) {
            e->gesture=1; e->directed=0; e->source_x=e->x; e->source_y=e->y;
        }
        if(f.circle && e->gesture) { cancel_move(e); return; }
        if(e->gesture && (f.dx || f.dy)) {
            e->directed=1;
            Cell c=score_at(&e->score,e->source_x,e->source_y);
            if(c.kind==CELL_TILE || c.kind==CELL_HEAD) e->mode=EDIT_MOVE;
        }
        e->x=clamp(e->x+f.dx,0,SCORE_WIDTH-1); e->y=clamp(e->y+f.dy,0,SCORE_HEIGHT-1);
        if(f.dx || f.dy) e->message="";
        if(f.cross_released && e->gesture) {
            e->gesture=0;
            if(e->mode==EDIT_MOVE) {
                ScoreResult r=score_apply_move(&e->score,score_plan_move(&e->score,e->source_x,e->source_y,e->x,e->y));
                if(r) { e->x=e->source_x; e->y=e->source_y; }
                e->mode=EDIT_PLANE; e->message=score_message(r);
            } else if(!e->directed) { e->mode=EDIT_MENU; e->selected=0; e->message=""; }
        }
        return;
    }
    // A menu confirmation never arms the plane's press/release gesture.
    e->gesture=0;
    if(f.circle) { e->mode=e->mode==EDIT_MENU?EDIT_PLANE:editor_sound_parent(e->mode); e->selected=0; e->message=""; return; }
    if(e->mode==EDIT_SOUND || editor_sound_parent(e->mode)==EDIT_SOUND) {
        static const EditorMode groups[]={EDIT_WAVES,EDIT_AMPLITUDE,EDIT_MIX,EDIT_SWEEP};
        static const EditorMode fields[][2]={{EDIT_WAVE_A,EDIT_WAVE_B},{EDIT_ATTACK,EDIT_RELEASE},
            {EDIT_MIX_ATTACK,EDIT_MIX_RELEASE},{EDIT_PITCH_SWEEP,EDIT_PITCH_DECAY}};
        int root=e->mode==EDIT_SOUND, last=root?5:2;
        e->selected=clamp(e->selected+f.dy,0,last);
        if(f.cross) {
            if(e->selected==last) e->mode=editor_sound_parent(e->mode);
            else if(root && e->selected==4) { e->mode=EDIT_SOUND_REVERB; e->candidate=e->score.sounds[e->sound_channel].reverb; }
            else if(root) e->mode=groups[e->selected];
            else {
                int group=0; while(groups[group]!=e->mode) group++;
                e->mode=fields[group][e->selected]; e->sound_candidate=e->score.sounds[e->sound_channel];
            }
            e->selected=0;
        }
        return;
    }
    if(editor_sound_parent(e->mode)!=EDIT_MENU) {
        int *v=NULL, min=0, max=SOUND_MAX_MS, step=100;
        switch(e->mode) {
        case EDIT_ATTACK: v=&e->sound_candidate.attack; break;
        case EDIT_RELEASE: v=&e->sound_candidate.release; break;
        case EDIT_WAVE_A: v=&e->sound_candidate.wave_a; max=WAVE_COUNT-1; step=1; break;
        case EDIT_WAVE_B: v=&e->sound_candidate.wave_b; max=WAVE_COUNT-1; step=1; break;
        case EDIT_MIX_ATTACK: v=&e->sound_candidate.mix_attack; max=SOUND_MAX_MIX_MS; break;
        case EDIT_MIX_RELEASE: v=&e->sound_candidate.mix_release; max=SOUND_MAX_MIX_MS; break;
        case EDIT_PITCH_SWEEP: v=&e->sound_candidate.sweep; min=-SOUND_MAX_SWEEP; max=SOUND_MAX_SWEEP; step=12; break;
        case EDIT_PITCH_DECAY: v=&e->sound_candidate.decay; max=SOUND_MAX_DECAY_MS; break;
        default: break;
        }
        if(v) *v=clamp(*v+f.dx-f.dy*step,min,max);
        if(f.cross) finish(e,score_set_sound(&e->score,e->sound_channel,e->sound_candidate));
        return;
    }
    if(e->mode==EDIT_PICKER) {
        e->tile_candidate=(TileKind)clamp(e->tile_candidate+f.dy,1,TILE_KIND_COUNT-1);
        if(f.cross) {
            TileValue v=e->tile_candidate==TILE_NOTE?e->last_note:score_default(e->tile_candidate);
            ScoreResult r=score_place_value(&e->score,e->x,e->y,v);
            if(!r) { e->last_tile=e->tile_candidate; if(v.kind==TILE_NOTE) e->last_note=v; }
            finish(e,r);
        }
        return;
    }
    if(e->mode==EDIT_DELETE) {
        e->confirm=clamp(e->confirm+f.dy+f.dx,0,1);
        if(f.cross) {
            if(e->confirm) finish(e,e->target?score_remove(&e->score,e->x,e->y):score_delete(&e->score,e->lane));
            else e->mode=EDIT_MENU;
        }
        return;
    }
    if(e->mode==EDIT_CHANNEL) {
        e->candidate=clamp(e->candidate+f.dx-f.dy,0,SCORE_CHANNELS-1);
        if(f.cross) finish(e,score_set_channel(&e->score,e->lane,e->candidate));
        return;
    }
    if(e->mode==EDIT_LENGTH || e->mode==EDIT_DIVISION) {
        e->candidate=clamp(e->candidate+f.dx-f.dy,e->mode==EDIT_LENGTH?1:0,e->mode==EDIT_LENGTH?64:SCORE_DIVISIONS-1);
        if(f.cross) finish(e,e->mode==EDIT_LENGTH?score_resize(&e->score,e->lane,e->candidate):score_set_division(&e->score,e->lane,score_divisions[e->candidate]));
        return;
    }
    if(e->mode!=EDIT_MENU) {
        if(e->mode==EDIT_LOCK_ATTACK_ENABLE || e->mode==EDIT_LOCK_RELEASE_ENABLE) {
            int bit=e->mode==EDIT_LOCK_ATTACK_ENABLE?LOCK_ATTACK:LOCK_RELEASE;
            if(f.dx || f.dy) {
                if(f.dx-f.dy>0) e->value.lock_mask|=bit;
                else { e->value.lock_mask&=~bit; if(bit==LOCK_ATTACK) e->value.attack=0; else e->value.release=0; }
            }
        }
        if(e->mode==EDIT_LOCK_ATTACK && (e->value.lock_mask&LOCK_ATTACK))
            e->value.attack=clamp(e->value.attack+f.dx-f.dy*100,-SOUND_MAX_MS,SOUND_MAX_MS);
        if(e->mode==EDIT_LOCK_RELEASE && (e->value.lock_mask&LOCK_RELEASE))
            e->value.release=clamp(e->value.release+f.dx-f.dy*100,-SOUND_MAX_MS,SOUND_MAX_MS);
        if(e->mode==EDIT_PITCH) e->value.pitch=clamp(e->value.pitch+f.dx-f.dy*12,0,108);
        if(e->mode==EDIT_DURATION) e->value.length=clamp(e->value.length+f.dx-f.dy*20,5,1280);
        if(e->mode==EDIT_PERIOD) e->value.period=clamp(e->value.period+f.dx-f.dy,2,32);
        if(e->mode==EDIT_CHANCE) e->value.chance=clamp(e->value.chance+f.dx-f.dy,0,100);
        if(e->mode==EDIT_PATTERN) {
            int p=e->pattern_cursor;
            if(p==e->value.period) { if(f.dy<0) p=e->value.period-1; }
            else { p=clamp(p+f.dx+f.dy*8,0,e->value.period); }
            e->pattern_cursor=p;
            if(f.cross && p<e->value.period) { e->value.pattern^=(uint32_t)1<<p; return; }
        }
        if(f.cross) {
            ScoreResult r=score_edit(&e->score,e->target,e->value);
            if(!r && e->value.kind==TILE_NOTE) e->last_note=e->value;
            finish(e,r);
        }
        return;
    }
    EditorAction items[EDITOR_MENU_ITEMS]; int count=editor_menu(e,items);
    e->selected=clamp(e->selected+f.dy,0,count-1);
    if(!f.cross) return;
    Cell c=score_at(&e->score,e->x,e->y); e->lane=c.lane; e->target=c.tile;
    if(c.tile) e->value=e->score.tiles[c.tile].value;
    switch(items[e->selected]) {
    case ACTION_CREATE: finish(e,score_create(&e->score,e->x,e->y,SCORE_INITIAL_LENGTH)); break;
    case ACTION_PLACE: e->tile_candidate=e->last_tile; e->mode=EDIT_PICKER; break;
    case ACTION_PASTE: finish(e,score_paste(&e->score,e->x,e->y,&e->clipboard)); break;
    case ACTION_COPY: score_copy(&e->score,e->x,e->y,&e->clipboard); e->mode=EDIT_PLANE; break;
    case ACTION_REMOVE:
        if(e->value.kind!=TILE_JUMP) { finish(e,score_remove(&e->score,e->x,e->y)); break; }
        /* A jump owns a whole subtree, so use the same explicit confirmation as a lane. */
        e->confirm=0; e->mode=EDIT_DELETE; break;
    case ACTION_DELETE: e->confirm=0; e->mode=EDIT_DELETE; break;
    case ACTION_LENGTH: e->candidate=e->score.lanes[c.lane].length; e->mode=EDIT_LENGTH; break;
    case ACTION_DIVISION:
        e->candidate=0; while(score_divisions[e->candidate]!=score_division(&e->score,c.lane)) e->candidate++;
        e->mode=EDIT_DIVISION; break;
    case ACTION_PITCH: e->mode=EDIT_PITCH; break;
    case ACTION_DURATION: e->mode=EDIT_DURATION; break;
    case ACTION_PERIOD: e->mode=EDIT_PERIOD; break;
    case ACTION_PATTERN: e->pattern_cursor=0; e->mode=EDIT_PATTERN; break;
    case ACTION_CHANCE: e->mode=EDIT_CHANCE; break;
    case ACTION_CHANNEL: e->candidate=score_channel(&e->score,c.lane); e->mode=EDIT_CHANNEL; break;
    case ACTION_SOUND: e->sound_channel=score_channel(&e->score,c.lane); e->selected=0; e->mode=EDIT_SOUND; break;
    case ACTION_LOCK_ATTACK_ENABLE: e->mode=EDIT_LOCK_ATTACK_ENABLE; break;
    case ACTION_LOCK_RELEASE_ENABLE: e->mode=EDIT_LOCK_RELEASE_ENABLE; break;
    case ACTION_LOCK_ATTACK: e->mode=EDIT_LOCK_ATTACK; break;
    case ACTION_LOCK_RELEASE: e->mode=EDIT_LOCK_RELEASE; break;
    case ACTION_CLOSE: e->mode=EDIT_PLANE; break;
    }
}
