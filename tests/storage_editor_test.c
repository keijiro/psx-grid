/*
 * storage_editor_test.c - Editor storage-menu regression tests
 *
 * Implementation notes:
 *
 * Synthetic input frames exercise storage actions and visible state without
 * invoking a physical card session.
 */

#include "editor.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static Editor editor;
static Input input;

static void frame(uint16_t held)
{
    editor_update(&editor, input_update(&input, 1, held));
}

static void tap(uint16_t button)
{
    frame(button);
    frame(0);
}

static void init(void)
{
    editor_init(&editor);
    input_init(&input);
    frame(0);
}

/*
 * Checks that slot selection and card actions become main-loop requests
 * without immediate I/O.
 */
static void chooser_and_requests(void)
{
    init();
    assert(editor.storage_slot == 1);
    assert(editor.slot_status == STORAGE_UNKNOWN);
    assert(editor.card_free == -1);
    assert(editor.free_bytes == 7464);

    tap(INPUT_SELECT);
    assert(editor.mode == EDIT_MAIN && editor.selected == 0);
    for (int row = 0; row < 5; row++) {
        tap(INPUT_DOWN);
        assert(editor.selected == row + 1);
    }
    tap(INPUT_DOWN);
    assert(editor.selected == 5);
    tap(INPUT_UP);
    tap(INPUT_UP);
    tap(INPUT_UP);
    assert(editor.selected == 2);

    editor.message = "SAVED";
    tap(INPUT_RIGHT);
    assert(editor.storage_slot == 2);
    assert(!strcmp(editor.message, ""));
    for (int slot = 2; slot < STORAGE_SLOTS; slot++)
        tap(INPUT_RIGHT);
    assert(editor.storage_slot == STORAGE_SLOTS);
    tap(INPUT_RIGHT);
    assert(editor.storage_slot == STORAGE_SLOTS);
    for (int slot = STORAGE_SLOTS; slot > 1; slot--)
        tap(INPUT_LEFT);
    tap(INPUT_LEFT);
    assert(editor.storage_slot == 1);

    // Coarse shoulder changes are inline value edits. Opposing fine/coarse
    // controls cancel, and a simultaneous row move suppresses adjustment.
    frame(INPUT_R1);
    assert(editor.storage_slot == 2);
    frame(INPUT_RIGHT | INPUT_L1);
    assert(editor.storage_slot == 2);
    frame(0);
    frame(INPUT_DOWN | INPUT_R1);
    assert(editor.selected == 3 && editor.storage_slot == 2);
    frame(0);

    tap(INPUT_CROSS);
    assert(editor.storage_request == STORAGE_ACTION_CHECK);
    editor.storage_request = STORAGE_ACTION_NONE;
    tap(INPUT_DOWN);
    tap(INPUT_CROSS);
    assert(editor.storage_request == STORAGE_ACTION_SAVE);
    editor.storage_request = STORAGE_ACTION_NONE;
    tap(INPUT_DOWN);
    tap(INPUT_CROSS);
    assert(editor.storage_request == STORAGE_ACTION_LOAD);
    editor.storage_request = STORAGE_ACTION_NONE;
    tap(INPUT_DOWN);
    assert(editor.selected == 5);
    tap(INPUT_CIRCLE);
    assert(editor.mode == EDIT_PLANE);
}

/*
 * Checks that score edits remain disabled while a staged load awaits audio
 * adoption.
 */
static void waiting_load_lock(void)
{
    init();
    assert(!score_create(&editor.score, 1, 1, 4));
    editor_refresh_capacity(&editor);
    Score before = editor.score;
    int free_before = editor.free_bytes;

    editor.storage_slot = 4;
    editor.load_slot = editor.storage_slot;
    editor.load_busy = 1;
    editor.mode = EDIT_MAIN;
    editor.selected = 4;
    editor.target = 12;
    editor.slot_status = STORAGE_BUSY;
    editor.message = "WAITING FOR LAP";

    tap(INPUT_CROSS);
    assert(editor.storage_request == STORAGE_ACTION_NONE);
    tap(INPUT_UP);
    assert(editor.selected == 3);
    tap(INPUT_CROSS);
    assert(editor.storage_request == STORAGE_ACTION_NONE);
    tap(INPUT_UP);
    assert(editor.selected == 2);
    tap(INPUT_RIGHT);
    assert(editor.storage_slot == 5 && editor.load_slot == 4);
    assert(!strcmp(editor.message, "WAITING FOR LAP"));

    tap(INPUT_SELECT);
    assert(editor.mode == EDIT_PLANE);
    int target = editor.target;
    tap(INPUT_RIGHT);
    tap(INPUT_DOWN);
    assert(editor.x == 2 && editor.y == 2);
    assert(editor.target == target);
    tap(INPUT_CROSS);
    tap(INPUT_START);
    assert(editor.mode == EDIT_PLANE && !editor.gesture);
    assert(!memcmp(&editor.score, &before, sizeof(before)));
    assert(editor.free_bytes == free_before);

    tap(INPUT_SELECT);
    assert(editor.mode == EDIT_MAIN && editor.selected == 2);
    tap(INPUT_DOWN);
    tap(INPUT_DOWN);
    assert(editor.selected == 4);
    tap(INPUT_CROSS);
    assert(editor.storage_request == STORAGE_ACTION_NONE);
    // Card requests and score value changes stay locked until acknowledgement.
    editor.selected = 0;
    int bpm = editor.score.bpm;
    frame(INPUT_RIGHT);
    assert(editor.score.bpm == bpm);
    tap(INPUT_CROSS);
    assert(editor.storage_request == STORAGE_ACTION_NONE);
}

/*
 * Checks fine and coarse adjustments on storage-facing editor rows.
 */
static void inline_value_controls(void)
{
    init();
    tap(INPUT_SELECT);
    assert(editor.mode == EDIT_MAIN && editor.selected == 0);
    tap(INPUT_R1);
    assert(editor.score.bpm == 130);
    tap(INPUT_LEFT);
    assert(editor.score.bpm == 129);

    // Moving to a different row wins over an adjustment in the same frame.
    uint32_t revision = editor.score.revision;
    frame(INPUT_DOWN | INPUT_R1);
    assert(editor.selected == 1 && editor.score.bpm == 129);
    assert(editor.score.revision == revision);
}

/*
 * Checks capacity display after an adopted score replaces the editable one.
 */
static void adoption_and_capacity(void)
{
    init();
    Score incoming;
    score_init(&incoming);
    assert(!score_create(&incoming, 10, 10, 16));
    assert(!score_place(&incoming, 11, 10, TILE_NOTE));
    incoming.revision = editor.score.revision + 1;

    editor.load_busy = 1;
    editor.load_slot = 7;
    editor.storage_slot = 3;
    editor.x = 50;
    editor.y = 40;
    editor.target = 12;
    editor.score = incoming;
    editor.load_busy = 0;
    editor.message = "LOADED";
    editor.mode = EDIT_MAIN;
    editor.selected = 4;
    editor.target = 0;
    editor.gesture = 0;
    editor_refresh_capacity(&editor);

    assert(editor.storage_slot == 3 && editor.load_slot == 7);
    assert(editor.x == 50 && editor.y == 40);
    assert(editor.mode == EDIT_MAIN && editor.selected == 4 && !editor.target);
    assert(!strcmp(editor.message, "LOADED"));
    assert(editor.free_bytes == SCORE_FILE_BYTES - (int)score_format_measure(&incoming));
}

/*
 * Checks that held buttons from a card session cannot trigger editor actions
 * on resume.
 */
static void resume_requires_all_buttons_up(void)
{
    init();
    editor.mode = EDIT_MAIN;
    editor.selected = 4;

    /* storage_action resets Input after the intentional pad polling pause. */
    input_init(&input);
    frame(INPUT_CROSS | INPUT_START | INPUT_DOWN);
    assert(editor.selected == 4);
    assert(editor.storage_request == STORAGE_ACTION_NONE);
    frame(INPUT_CROSS);
    assert(editor.storage_request == STORAGE_ACTION_NONE);
    frame(0);
    tap(INPUT_UP);
    assert(editor.selected == 3);
    tap(INPUT_DOWN);
    assert(editor.selected == 4);
    tap(INPUT_CROSS);
    assert(editor.storage_request == STORAGE_ACTION_SAVE);
}

int main(void)
{
    chooser_and_requests();
    waiting_load_lock();
    inline_value_controls();
    adoption_and_capacity();
    resume_requires_all_buttons_up();
    puts("PASS: storage editor menu, requests, load lock, adoption and input resume");
}
