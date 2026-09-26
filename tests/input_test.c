/*
 * input_test.c - Host controller input regression tests
 *
 * Implementation notes:
 *
 * Queued button samples exercise edge detection, reconnection and
 * independent repeat clocks through the editor.
 */

#include "editor.h"
#include "input.h"

#include <assert.h>
#include <stdio.h>

static Editor editor;
static Input input;
static InputQueue queue;
static int moves;
static int presses;
static int releases;

static void drain(void)
{
    InputSample s;
    while (input_queue_pop(&queue, &s))
    {
        EditorMode before = editor.mode;
        InputFrame f = input_update(&input, s.connected, s.held);
        moves += f.dx != 0;
        presses += f.cross;
        releases += f.cross_released;
        editor_update(&editor, f);
        if (editor.mode != before) input_reset_repeat(&input);
    }
}

/*
 * Checks that opposing buttons cancel adjustment and each repeat clock
 * restarts on a new hold.
 */
static void value_repeat_and_conflicts(void)
{
    Input i;
    input_init(&i);
    input_update(&i, 1, 0);
    InputFrame f = input_update(&i, 1, INPUT_RIGHT);
    assert(f.value_dir == 1 && !f.value_coarse);
    for (int n = 0; n < INPUT_VALUE_DELAY - 1; n++)
    {
        f = input_update(&i, 1, INPUT_RIGHT);
        assert(!f.value_dir);
    }
    f = input_update(&i, 1, INPUT_RIGHT);
    assert(f.value_dir == 1 && !f.value_coarse);

    // Direction change and coarse/fine opposition cancel the event and reset
    // the value hold clock. Same-direction fine plus shoulder is coarse only.
    f = input_update(&i, 1, INPUT_LEFT);
    assert(f.value_dir == -1 && !f.value_coarse);
    f = input_update(&i, 1, INPUT_R1 | INPUT_RIGHT);
    assert(f.value_dir == 1 && f.value_coarse);
    f = input_update(&i, 1, INPUT_L1 | INPUT_RIGHT);
    assert(!f.value_dir);
    f = input_update(&i, 1, INPUT_R1 | INPUT_L1);
    assert(!f.value_dir);
    f = input_update(&i, 1, INPUT_L1);
    assert(f.value_dir == -1 && f.value_coarse);

    // Disconnect/reconnect suppresses held shoulder input until all buttons are
    // released, while a fresh press still emits immediately.
    f = input_update(&i, 0, INPUT_R1);
    assert(!f.value_dir);
    f = input_update(&i, 1, INPUT_R1);
    assert(!f.value_dir);
    f = input_update(&i, 1, 0);
    assert(!f.value_dir);
    f = input_update(&i, 1, INPUT_R1);
    assert(f.value_dir == 1 && f.value_coarse);

    Input coarse;
    input_init(&coarse);
    input_update(&coarse, 1, 0);
    f = input_update(&coarse, 1, INPUT_L1);
    assert(f.value_dir == -1 && f.value_coarse);
    int last_event = 0;
    int first_interval = 0;
    int last_interval = 0;
    for (int tick = 1; tick <= 220; tick++)
    {
        f = input_update(&coarse, 1, INPUT_L1);
        if (f.value_dir)
        {
            if (last_event)
            {
                int interval = tick - last_event;
                if (!first_interval) first_interval = interval;
                last_interval = interval;
            }
            last_event = tick;
        }
    }
    assert(first_interval == 5 && last_interval == 2);

    // Menu value timing remains independent from the plane cursor's fixed
    // repeat delay.
    Input plane;
    input_init(&plane);
    input_update(&plane, 1, 0);
    f = input_update(&plane, 1, INPUT_RIGHT);
    assert(f.dx == 1);
    for (int tick = 1; tick < INPUT_DELAY; tick++)
    {
        f = input_update(&plane, 1, INPUT_RIGHT);
        assert(!f.dx);
    }
    f = input_update(&plane, 1, INPUT_RIGHT);
    assert(f.dx == 1);
}

int main(void)
{
    value_repeat_and_conflicts();
    editor_init(&editor);
    input_init(&input);
    input_queue_init(&queue);
    assert(!score_create(&editor.score, 100, 1, 4));
    input_queue_push(&queue, (InputSample){1, 0});
    drain();
    // Entire taps happen while the consumer is busy, including an X tap opening
    // a menu immediately followed by a direction in that new mode.
    for (int n = 0; n < 100; n++)
    {
        assert(!input_queue_push(&queue, (InputSample){1, INPUT_RIGHT}));
        assert(!input_queue_push(&queue, (InputSample){1, 0}));
        if (n % 10 == 9) drain();
    }
    assert(moves == 100);
    input_queue_push(&queue, (InputSample){1, INPUT_CROSS});
    input_queue_push(&queue, (InputSample){1, 0});
    input_queue_push(&queue, (InputSample){1, INPUT_DOWN});
    drain();
    assert(presses == 1 && releases == 1 && editor.mode == EDIT_MENU &&
           editor.selected == 1);
    // Overflow cancels the active gesture and suppresses a held reconnect.
    editor_init(&editor);
    input_init(&input);
    input_update(&input, 1, 0);
    editor_update(&editor, input_update(&input, 1, INPUT_CROSS));
    assert(editor.gesture);
    input_queue_init(&queue);
    for (int n = 0; n < INPUT_QUEUE_CAPACITY - 1; n++)
    {
        assert(!input_queue_push(&queue,
                                 (InputSample){1, INPUT_CROSS | INPUT_RIGHT}));
    }
    assert(
        input_queue_push(&queue, (InputSample){1, INPUT_CROSS | INPUT_RIGHT}));
    drain();
    assert(!editor.gesture && editor.mode == EDIT_PLANE && editor.x == 1);
    input_queue_push(&queue, (InputSample){1, 0});
    input_queue_push(&queue, (InputSample){1, INPUT_RIGHT});
    drain();
    assert(editor.x == 2);
    puts("PASS: value repeat, coarse conflicts, ordered history, overflow and "
         "reconnect");
}
