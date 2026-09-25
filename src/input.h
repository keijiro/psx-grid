/*
 * input.h - Controller samples, event edges and repeat timing
 *
 * The pad service queues raw samples; the editor consumes normalized frames
 * on the main thread. Repeat state belongs to one Input instance.
 */

#ifndef INPUT_H
#define INPUT_H

#include <stdint.h>

// Button bits shared by the platform pad driver and host input tests.
enum {
    INPUT_LEFT = 1,
    INPUT_RIGHT = 2,
    INPUT_UP = 4,
    INPUT_DOWN = 8,
    INPUT_CROSS = 16,
    INPUT_CIRCLE = 32,
    INPUT_START = 64,
    INPUT_SELECT = 128,
    INPUT_L1 = 256,
    INPUT_R1 = 512
};

// Repeat delays are measured in calls to input_update at the display cadence.
#define INPUT_DELAY 18
#define INPUT_INTERVAL 3
#define INPUT_VALUE_DELAY 16

// Held-button history and independent cursor, row and value repeat clocks.
typedef struct {
    uint16_t previous;
    int connected, direction, countdown, row_direction, row_countdown, value_direction,
        value_coarse, value_countdown, value_held;
} Input;

// One frame of movement and edge events after repeat processing.
typedef struct {
    int connected, dx, dy, row_dy, value_dir, value_coarse, cross, circle, cross_held,
        cross_released, start, select;
} InputFrame;

// Fixed queue absorbs controller reports between main-thread frames.
#define INPUT_QUEUE_CAPACITY 64

// Raw connection state and active-high button mask from one poll.
typedef struct {
    int connected;
    uint16_t held;
} InputSample;

// Single-producer, single-consumer ring of raw samples.
typedef struct {
    InputSample samples[INPUT_QUEUE_CAPACITY];
    unsigned read, write;
} InputQueue;

/* Resets the sample queue to empty. */
void input_queue_init(InputQueue* queue);
/*
 * Enqueues a sample and returns whether the queue overflowed. Overflow resets
 * history and inserts a disconnect sample before the new report.
 */
int input_queue_push(InputQueue* queue, InputSample sample);
/* Removes the oldest sample into `sample`; returns zero when empty. */
int input_queue_pop(InputQueue* queue, InputSample* sample);
/* Clears connection, button history and repeat state. */
void input_init(Input* input);
/* Defers held cursor repetition after a mode change. */
void input_reset_repeat(Input* input);
/* Clears value adjustment repetition without resetting cursor state. */
void input_reset_value_repeat(Input* input);
/* Converts one raw button mask into an editor frame; disconnection resets state. */
InputFrame input_update(Input* input, int connected, uint16_t held);

#endif // INPUT_H
