/*
 * editor.h - Grid editor state and menu-facing view models
 *
 * A caller-owned Editor holds the editable score in fixed storage and turns
 * normalized input frames into validated score operations. Main-thread input
 * updates and rendering access this state serially; rendering does not mutate
 * it.
 */

#ifndef EDITOR_H
#define EDITOR_H

#include "input.h"
#include "score.h"
#include "storage.h"

#include <stdint.h>

// Maximum context actions and setting rows exposed in one editor menu.
#define EDITOR_MENU_ITEMS 8
#define EDITOR_ROWS 16

// Row roles determine navigation and whether an adjustment is available.
typedef enum
{
    ROW_HEADING,                    // A label has no action.
    ROW_VALUE,                      // A setting can be adjusted.
    ROW_SUBMENU,                    // An action opens another surface.
    ROW_ACTION                      // An action executes immediately.
} EditorRowKind;

// One menu row; value rows use min/max and fine/coarse adjustment steps.
typedef struct
{
    EditorRowKind kind;
    int id;
    int min;
    int max;
    int fine;
    int coarse;
    const char* label;
} EditorRow;

// Current editor surface or interaction in progress.
typedef enum
{
    EDIT_PLANE,                     // The grid receives navigation.
    EDIT_MENU,                      // A cell menu receives navigation.
    EDIT_DELETE,                    // A lane deletion awaits confirmation.
    EDIT_PICKER,                    // The tile picker receives navigation.
    EDIT_PATTERN,                   // The pattern editor receives input.
    EDIT_MOVE,                      // A drag move is in progress.
    EDIT_SOUND,                     // Channel sound settings are open.
    EDIT_MAIN,                      // Global score settings are open.
    EDIT_REVERB,                    // Reverb settings are open.
    EDIT_MODE_COUNT                 // The number of editor surfaces.
} EditorMode;

// Context actions available for the selected score cell.
typedef enum
{
    ACTION_CREATE,                  // Create a lane at the cursor.
    ACTION_PLACE,                   // Place a tile at the cursor.
    ACTION_REMOVE,                  // Remove the selected tile.
    ACTION_LENGTH,                  // Change lane length.
    ACTION_DELETE,                  // Delete a lane and its branches.
    ACTION_COPY,                    // Copy a tile suffix.
    ACTION_PASTE,                   // Paste copied tile values.
    ACTION_PITCH,                   // Change note pitch.
    ACTION_DURATION,                // Change note gate length.
    ACTION_PERIOD,                  // Change cycle period.
    ACTION_PATTERN,                 // Edit cycle steps.
    ACTION_CHANCE,                  // Change probability.
    ACTION_DIVISION,                // Change lane step division.
    ACTION_SOUND,                   // Open channel sound settings.
    ACTION_CHANNEL,                 // Change the lane channel.
    ACTION_LOCK_ATTACK_ENABLE,      // Toggle attack override.
    ACTION_LOCK_RELEASE_ENABLE,     // Toggle release override.
    ACTION_LOCK_ATTACK,             // Change attack offset.
    ACTION_LOCK_RELEASE             // Change release offset.
} EditorAction;

// Requests are consumed by the main-thread storage coordinator.
enum
{
    STORAGE_ACTION_NONE,            // No card request is pending.
    STORAGE_ACTION_CHECK,           // Refresh the selected slot.
    STORAGE_ACTION_SAVE,            // Save to the selected slot.
    STORAGE_ACTION_LOAD             // Load from the selected slot.
};

// Persistent cursor, gesture, menu, score and storage presentation state.
typedef struct
{
    Score score;
    // Dirty means committed edits are waiting for audio publication.
    int playing;
    int snapshot_dirty;
    Clipboard clipboard;
    int x;
    int y;
    int selected;
    int lane;
    int parent_selected;
    // Sound rows read the channel selected when Sound was opened.
    int sound_channel;
    int gesture;
    int directed;
    int source_x;
    int source_y;
    int pattern_cursor;
    TileId target;
    TileValue value;
    TileValue last_note;
    TileKind tile_candidate;
    TileKind last_tile;
    EditorMode mode;
    const char* message;
    int storage_slot;
    int storage_request;
    int load_busy;
    int load_slot;
    int card_free;
    StorageResult slot_status;
    uint32_t capacity_revision;
    int free_bytes;
} Editor;

/*
 * Initializes caller-owned `editor` with an empty score and default selection.
 * `editor` must not be NULL.
 */
void editor_init(Editor* editor);
/*
 * Recomputes `editor` save capacity after a committed score change.
 * `editor` must not be NULL.
 */
void editor_refresh_capacity(Editor* editor);
/*
 * Writes at most EDITOR_MENU_ITEMS actions to `items` and returns their count.
 * Entries beyond the returned count are unchanged.
 * `editor` and `items` must not be NULL.
 */
int editor_menu(const Editor* editor, EditorAction items[EDITOR_MENU_ITEMS]);
/*
 * Returns the fixed display label for a valid action.
 */
const char* editor_action_label(EditorAction action);
/*
 * Writes at most EDITOR_ROWS rows to `rows` for the current mode and returns
 * their count. Entries beyond the returned count are unchanged.
 * `editor` and `rows` must not be NULL.
 */
int editor_rows(const Editor* editor, EditorRow rows[EDITOR_ROWS]);
/*
 * Formats a valid row `id` in `editor` into `buffer`. A row without a value
 * leaves an empty string. `size` must be positive.
 * `editor` and `buffer` must not be NULL.
 */
void editor_row_value(const Editor* editor, int id, char* buffer, int size);
/*
 * Applies one normalized `input` frame to `editor` and records any storage
 * request.
 * `editor` must not be NULL.
 */
void editor_update(Editor* editor, InputFrame input);

#endif // EDITOR_H
