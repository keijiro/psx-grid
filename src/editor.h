/*
 * editor.h - Grid editor state and menu-facing view models
 *
 * The editor owns the editable score and turns normalized input frames into
 * validated score operations. Rendering reads this state without mutating it.
 */

#ifndef EDITOR_H
#define EDITOR_H

#include "score.h"
#include "input.h"
#include "storage.h"

// Maximum context actions and setting rows exposed in one editor menu.
#define EDITOR_MENU_ITEMS 8
#define EDITOR_ROWS 16

// Row roles determine navigation and whether an adjustment is available.
typedef enum
{
    ROW_HEADING,
    ROW_VALUE,
    ROW_SUBMENU,
    ROW_ACTION
} EditorRowKind;

// One menu row; value rows use min/max and fine/coarse adjustment steps.
typedef struct
{
    EditorRowKind kind;
    int id, min, max, fine, coarse;
    const char* label;
} EditorRow;

// Current editor surface or interaction in progress.
typedef enum
{
    EDIT_PLANE,
    EDIT_MENU,
    EDIT_DELETE,
    EDIT_PICKER,
    EDIT_PATTERN,
    EDIT_MOVE,
    EDIT_SOUND,
    EDIT_MAIN,
    EDIT_REVERB,
    EDIT_MODE_COUNT
} EditorMode;

// Context actions available for the selected score cell.
typedef enum
{
    ACTION_CREATE,
    ACTION_PLACE,
    ACTION_REMOVE,
    ACTION_LENGTH,
    ACTION_DELETE,
    ACTION_COPY,
    ACTION_PASTE,
    ACTION_PITCH,
    ACTION_DURATION,
    ACTION_PERIOD,
    ACTION_PATTERN,
    ACTION_CHANCE,
    ACTION_DIVISION,
    ACTION_SOUND,
    ACTION_CHANNEL,
    ACTION_LOCK_ATTACK_ENABLE,
    ACTION_LOCK_RELEASE_ENABLE,
    ACTION_LOCK_ATTACK,
    ACTION_LOCK_RELEASE
} EditorAction;

// Requests are consumed by the main-thread storage coordinator.
enum
{
    STORAGE_ACTION_NONE,
    STORAGE_ACTION_CHECK,
    STORAGE_ACTION_SAVE,
    STORAGE_ACTION_LOAD
};

// Persistent cursor, gesture, menu, score and storage presentation state.
typedef struct
{
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
    const char* message;
    int storage_slot, storage_request, load_busy, load_slot, card_free;
    StorageResult slot_status;
    uint32_t capacity_revision;
    int free_bytes;
} Editor;

/* Initializes caller-owned `editor` with an empty score and default selection. */
void editor_init(Editor* editor);
/* Recomputes `editor` save capacity after a committed score change. */
void editor_refresh_capacity(Editor* editor);
/* Writes at most EDITOR_MENU_ITEMS actions to `items` and returns their count. */
int editor_menu(const Editor* editor, EditorAction items[EDITOR_MENU_ITEMS]);
/* Returns the fixed display label for a valid action. */
const char* editor_action_label(EditorAction action);
/* Writes at most EDITOR_ROWS rows to `rows` for the current mode and returns their count. */
int editor_rows(const Editor* editor, EditorRow rows[EDITOR_ROWS]);
/* Formats `id` in `editor` into `buffer`; `size` must be positive. */
void editor_row_value(const Editor* editor, int id, char* buffer, int size);
/* Applies one normalized `input` frame to `editor` and records any storage request. */
void editor_update(Editor* editor, InputFrame input);

#endif // EDITOR_H
