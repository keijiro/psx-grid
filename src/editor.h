#ifndef EDITOR_H
#define EDITOR_H
#include "score.h"
#include "input.h"
#define EDITOR_MENU_ITEMS 8
typedef enum { EDIT_PLANE, EDIT_MENU, EDIT_LENGTH, EDIT_DELETE, EDIT_PICKER, EDIT_PITCH, EDIT_DURATION, EDIT_PERIOD, EDIT_PATTERN, EDIT_CHANCE, EDIT_DIVISION, EDIT_MOVE, EDIT_SOUND, EDIT_ATTACK, EDIT_RELEASE, EDIT_LOCK_ATTACK_ENABLE, EDIT_LOCK_RELEASE_ENABLE, EDIT_LOCK_ATTACK, EDIT_LOCK_RELEASE, EDIT_WAVES, EDIT_AMPLITUDE, EDIT_MIX, EDIT_SWEEP, EDIT_WAVE_A, EDIT_WAVE_B, EDIT_MIX_ATTACK, EDIT_MIX_RELEASE, EDIT_PITCH_SWEEP, EDIT_PITCH_DECAY, EDIT_MODE_COUNT } EditorMode;
typedef enum { ACTION_CREATE, ACTION_PLACE, ACTION_REMOVE, ACTION_LENGTH, ACTION_DELETE, ACTION_CLOSE, ACTION_COPY, ACTION_PASTE, ACTION_PITCH, ACTION_DURATION, ACTION_PERIOD, ACTION_PATTERN, ACTION_CHANCE, ACTION_DIVISION, ACTION_SOUND, ACTION_LOCK_ATTACK_ENABLE, ACTION_LOCK_RELEASE_ENABLE, ACTION_LOCK_ATTACK, ACTION_LOCK_RELEASE } EditorAction;
typedef struct {
    Score score;
    SoundSettings sound_candidate;
    // Dirty means committed edits are waiting for audio publication.
    int playing, snapshot_dirty;
    Clipboard clipboard;
    int x, y, selected, lane, candidate, confirm;
    int gesture, directed, source_x, source_y, pattern_cursor;
    TileId target;
    TileValue value, last_note;
    TileKind tile_candidate, last_tile;
    EditorMode mode;
    const char *message;
} Editor;
EditorMode editor_sound_parent(EditorMode mode);
void editor_init(Editor *editor);
int editor_menu(const Editor *editor, EditorAction items[EDITOR_MENU_ITEMS]);
const char *editor_action_label(EditorAction action);
void editor_update(Editor *editor, InputFrame input);
#endif
