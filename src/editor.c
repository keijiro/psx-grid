#include "editor.h"
#include <string.h>
void editor_init(Editor *e) { memset(e, 0, sizeof(*e)); e->x = e->y = 1; e->message = ""; }
int editor_menu(const Editor *e, EditorAction items[3]) {
    Cell c = score_at(&e->score, e->x, e->y);
    int n = 0;
    if (c.kind == CELL_EMPTY) items[n++] = ACTION_CREATE;
    if (c.kind == CELL_STEP) items[n++] = ACTION_PLACE;
    if (c.kind == CELL_TILE) items[n++] = ACTION_REMOVE;
    if (c.kind != CELL_EMPTY) items[n++] = ACTION_LENGTH;
    if (c.kind == CELL_HEAD) items[n++] = ACTION_DELETE;
    items[n++] = ACTION_CLOSE;
    return n;
}
const char *editor_action_label(EditorAction a) {
    static const char *labels[] = {"NEW LANE", "PLACE TILE", "DELETE TILE", "CHANGE LENGTH", "DELETE LANE", "CLOSE"};
    return labels[a];
}
static int clamp(int x, int max) { return x < 0 ? 0 : x > max ? max : x; }
void editor_update(Editor *e, InputFrame f) {
    if (!f.connected) return;
    if (f.circle && e->mode != EDIT_PLANE) { e->mode = EDIT_PLANE; e->message = "CANCELLED"; return; }
    if (e->mode == EDIT_PLANE) {
        e->x = clamp(e->x + f.dx, SCORE_WIDTH - 1);
        e->y = clamp(e->y + f.dy, SCORE_HEIGHT - 1);
        if (f.dx || f.dy) e->message = "";
        if (f.cross) { e->mode = EDIT_MENU; e->selected = 0; e->message = ""; }
        return;
    }
    if (e->mode == EDIT_LENGTH) {
        if (f.dx) {
            int candidate = e->candidate + f.dx;
            ScoreResult r = score_can_resize(&e->score, e->lane, candidate);
            e->message = score_message(r);
            if (r == SCORE_OK) e->candidate = candidate;
        }
        if (f.cross) {
            ScoreResult r = score_resize(&e->score, e->lane, e->candidate);
            e->message = r == SCORE_OK ? "LENGTH UPDATED" : score_message(r);
            if (r == SCORE_OK) e->mode = EDIT_PLANE;
        }
        return;
    }
    if (e->mode == EDIT_DELETE) {
        if (f.dx) e->confirm = f.dx > 0;
        if (f.cross) {
            e->message = e->confirm ? score_message(score_delete(&e->score, e->lane)) : "CANCELLED";
            e->mode = EDIT_PLANE;
        }
        return;
    }
    EditorAction items[3];
    int count = editor_menu(e, items);
    e->selected = clamp(e->selected + f.dy, count - 1);
    if (!f.cross) return;
    Cell c = score_at(&e->score, e->x, e->y);
    ScoreResult result = SCORE_OK;
    switch (items[e->selected]) {
    case ACTION_CREATE: result = score_create(&e->score, e->x, e->y, SCORE_INITIAL_LENGTH); break;
    case ACTION_PLACE: result = score_tile(&e->score, e->x, e->y, 1); break;
    case ACTION_REMOVE: result = score_tile(&e->score, e->x, e->y, 0); break;
    case ACTION_LENGTH:
        e->lane = c.lane; e->candidate = e->score.lanes[c.lane].length; e->mode = EDIT_LENGTH; return;
    case ACTION_DELETE: e->lane = c.lane; e->confirm = 0; e->mode = EDIT_DELETE; return;
    case ACTION_CLOSE: break;
    }
    e->message = score_message(result);
    e->mode = EDIT_PLANE;
}
