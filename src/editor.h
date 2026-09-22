#ifndef EDITOR_H
#define EDITOR_H
#include "score.h"
#include "input.h"
typedef enum { EDIT_PLANE, EDIT_MENU, EDIT_LENGTH, EDIT_DELETE, EDIT_PICKER } EditorMode;
typedef enum { ACTION_CREATE, ACTION_PLACE, ACTION_REMOVE, ACTION_LENGTH, ACTION_DELETE, ACTION_CLOSE } EditorAction;
typedef struct {
    Score score;
    int x, y, selected, lane, candidate, confirm;
    TileKind tile_candidate, last_tile;
    EditorMode mode;
    const char *message;
} Editor;
void editor_init(Editor *editor);
int editor_menu(const Editor *editor, EditorAction items[3]);
const char *editor_action_label(EditorAction action);
void editor_update(Editor *editor, InputFrame input);
#endif
