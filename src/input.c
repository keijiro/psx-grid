/*
 * input.c - Controller event and repeat processing
 *
 * Implementation notes:
 *
 * Cursor, menu-row and value adjustment repeats use separate clocks so a
 * mode change cannot carry an unrelated hold into an edit.
 */

#include "input.h"

#include <string.h>

void input_init(Input* i)
{
    memset(i, 0, sizeof(*i));
}

// Retain the held direction: only its repeat is delayed across a mode change.
// A newly pressed direction must still produce its first movement immediately.
void input_reset_value_repeat(Input* i)
{
    i->value_direction = 0;
    i->value_held = 0;
    i->value_countdown = 0;
}

void input_reset_repeat(Input* i)
{
    i->countdown = INPUT_DELAY;
    i->row_direction = 0;
    i->row_countdown = 0;
    input_reset_value_repeat(i);
}

InputFrame input_update(Input* i, int connected, uint16_t held)
{
    InputFrame f = {0};
    f.connected = connected;
    if (!connected)
    {
        input_init(i);
        return f;
    }
    int dx = !!(held & INPUT_RIGHT) - !!(held & INPUT_LEFT);
    int dy = dx ? 0 : !!(held & INPUT_DOWN) - !!(held & INPUT_UP);
    int row_dy = !!(held & INPUT_DOWN) - !!(held & INPUT_UP);
    int direction = dx ? (dx > 0 ? 1 : 2) : dy ? (dy > 0 ? 3 : 4) : 0;
    if (!i->connected)
    {
        i->connected = 1;
        i->previous = held;
        // Ignore all buttons held at reconnection until they are released.
        i->direction = held ? -2 : 0;
        return f;
    }
    if (i->direction == -2)
    {
        i->previous = held;
        if (!held) i->direction = 0;
        return f;
    }
    f.cross_held = !!(held & INPUT_CROSS);
    f.cross_released = !!(i->previous & ~held & INPUT_CROSS);
    f.cross = !!(held & ~i->previous & INPUT_CROSS);
    f.circle = !!(held & ~i->previous & INPUT_CIRCLE);
    f.start = !!(held & ~i->previous & INPUT_START);
    f.select = !!(held & ~i->previous & INPUT_SELECT);
    i->previous = held;
    if (!row_dy)
    {
        i->row_direction = 0;
        i->row_countdown = 0;
    }
    else if (row_dy != i->row_direction)
    {
        i->row_direction = row_dy;
        i->row_countdown = INPUT_DELAY;
        f.row_dy = row_dy;
    }
    else if (--i->row_countdown <= 0)
    {
        i->row_countdown = INPUT_INTERVAL;
        f.row_dy = row_dy;
    }
    int fine = !!(held & INPUT_RIGHT) - !!(held & INPUT_LEFT);
    int coarse = !!(held & INPUT_R1) - !!(held & INPUT_L1);
    int conflict = ((held & (INPUT_LEFT | INPUT_RIGHT)) == (INPUT_LEFT | INPUT_RIGHT)) ||
                   ((held & (INPUT_L1 | INPUT_R1)) == (INPUT_L1 | INPUT_R1));
    int value = (conflict || (fine && coarse && fine != coarse)) ? 0 : coarse ? coarse : fine;
    // The value clock is separate from the plane cursor's fixed repeat.
    // Opposing inputs release it; a change of direction or step size starts
    // a new hold rather than inheriting the previous acceleration.
    if (!value)
    {
        i->value_direction = 0;
        i->value_held = 0;
        i->value_countdown = 0;
    }
    else if (value != i->value_direction || !!coarse != i->value_coarse)
    {
        i->value_direction = value;
        i->value_coarse = !!coarse;
        i->value_held = 0;
        i->value_countdown = INPUT_VALUE_DELAY;
        f.value_dir = value;
        f.value_coarse = !!coarse;
    }
    else
    {
        i->value_held++;
        if (--i->value_countdown <= 0)
        {
            f.value_dir = value;
            f.value_coarse = !!coarse;
            i->value_countdown = i->value_held < 45 ? 5 : i->value_held < 90 ? 4 : i->value_held < 150 ? 3 : 2;
        }
    }
    if (!direction)
    {
        i->direction = 0;
        i->countdown = 0;
    }
    else if (direction != i->direction)
    {
        i->direction = direction;
        i->countdown = INPUT_DELAY;
        f.dx = dx;
        f.dy = dy;
    }
    else if (--i->countdown <= 0)
    {
        i->countdown = INPUT_INTERVAL;
        f.dx = dx;
        f.dy = dy;
    }
    return f;
}

void input_queue_init(InputQueue* q)
{
    q->read = q->write = 0;
}

int input_queue_push(InputQueue* q, InputSample sample)
{
    unsigned next = (q->write + 1) % INPUT_QUEUE_CAPACITY;
    int overflow = next == q->read;
    if (overflow)
    {
        // A stall longer than the fixed history loses ordering. Cancel any
        // gesture and re-arm through the normal all-buttons-up guard instead
        // of delivering a release whose corresponding press was discarded.
        input_queue_init(q);
        q->samples[q->write++] = (InputSample){0, 0};
        next = q->write + 1;
    }
    q->samples[q->write] = sample;
    q->write = next;
    return overflow;
}

int input_queue_pop(InputQueue* q, InputSample* sample)
{
    if (q->read == q->write) return 0;
    *sample = q->samples[q->read];
    q->read = (q->read + 1) % INPUT_QUEUE_CAPACITY;
    return 1;
}
