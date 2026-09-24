#ifndef EDITOR_H
#define EDITOR_H
#include "score.h"
#include "input.h"
#include "storage.h"
#define EDITOR_MENU_ITEMS 8
#define EDITOR_ROWS 16
typedef enum { ROW_HEADING, ROW_VALUE, ROW_SUBMENU, ROW_ACTION } EditorRowKind;
typedef struct { EditorRowKind kind; int id, min, max, fine, coarse; const char *label; } EditorRow;
typedef enum { EDIT_PLANE, EDIT_MENU, EDIT_DELETE, EDIT_PICKER, EDIT_PATTERN, EDIT_MOVE, EDIT_SOUND, EDIT_MAIN, EDIT_REVERB, EDIT_MODE_COUNT } EditorMode;
typedef enum { ACTION_CREATE, ACTION_PLACE, ACTION_REMOVE, ACTION_LENGTH, ACTION_DELETE, ACTION_COPY, ACTION_PASTE, ACTION_PITCH, ACTION_DURATION, ACTION_PERIOD, ACTION_PATTERN, ACTION_CHANCE, ACTION_DIVISION, ACTION_SOUND, ACTION_CHANNEL, ACTION_LOCK_ATTACK_ENABLE, ACTION_LOCK_RELEASE_ENABLE, ACTION_LOCK_ATTACK, ACTION_LOCK_RELEASE } EditorAction;
enum { STORAGE_ACTION_NONE, STORAGE_ACTION_CHECK, STORAGE_ACTION_SAVE, STORAGE_ACTION_LOAD };
typedef struct {
    Score score;
    // Dirty means committed edits are waiting for audio publication.
    int playing, snapshot_dirty;
    Clipboard clipboard;
    int x, y, selected, lane;
    int parent_selected;
    // Sound rows read the channel selected when Sound was opened.
    int sound_channel;
    int gesture, directed, source_x, source_y, pattern_cursor;
    TileId target;
    TileValue value, last_note;
    TileKind tile_candidate, last_tile;
    EditorMode mode;
    const char *message;
    int storage_slot, storage_request, load_busy, load_slot, card_free;
    StorageResult slot_status;
    uint32_t capacity_revision;
    int free_bytes;
} Editor;
void editor_init(Editor *editor);
void editor_refresh_capacity(Editor *editor);
int editor_menu(const Editor *editor, EditorAction items[EDITOR_MENU_ITEMS]);
const char *editor_action_label(EditorAction action);
int editor_rows(const Editor *editor, EditorRow rows[EDITOR_ROWS]);
void editor_row_value(const Editor *editor, int id, char *buffer, int size);
void editor_update(Editor *editor, InputFrame input);
#endif
