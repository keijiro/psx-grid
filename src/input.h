/*
 * input.h - Controller history, event edges and repeat timing
 *
 * Rust normalizes controller reports for the editor on the main thread.
 * Caller-owned Input state retains button and repeat history.
 */

#ifndef INPUT_H
#define INPUT_H

#include <stdint.h>

// Repeat delays are measured in calls to input_update at the display cadence.
#define INPUT_DELAY 18
#define INPUT_INTERVAL 3
#define INPUT_VALUE_DELAY 16

// Button bits shared by the platform pad driver and host input tests.
enum
{
    INPUT_LEFT = 1,                 // Move left while held.
    INPUT_RIGHT = 2,                // Move right while held.
    INPUT_UP = 4,                   // Move upward while held.
    INPUT_DOWN = 8,                 // Move downward while held.
    INPUT_CROSS = 16,               // Confirm or act on a selection.
    INPUT_CIRCLE = 32,              // Cancel or return.
    INPUT_START = 64,               // Toggle transport at its edge.
    INPUT_SELECT = 128,             // Open global settings at its edge.
    INPUT_L1 = 256,                 // Use a coarser negative adjustment.
    INPUT_R1 = 512                  // Use a coarser positive adjustment.
};

// Held-button history and independent cursor, row and value repeat clocks.
typedef struct
{
    uint16_t previous;
    int connected;
    int direction;
    int countdown;
    int row_direction;
    int row_countdown;
    int value_direction;
    int value_coarse;
    int value_countdown;
    int value_held;
} Input;

// One frame of movement and edge events after repeat processing.
typedef struct
{
    int connected;
    int dx;
    int dy;
    int row_dy;
    int value_dir;
    int value_coarse;
    int cross;
    int circle;
    int cross_held;
    int cross_released;
    int start;
    int select;
} InputFrame;

/*
 * Clears caller-owned `input` connection, button history and repeat state.
 * `input` must not be NULL.
 */
void input_init(Input* input);
/*
 * Defers held cursor repetition in `input` after a mode change.
 * `input` must not be NULL.
 */
void input_reset_repeat(Input* input);
/*
 * Clears value adjustment repetition in `input` without resetting cursor state.
 * `input` must not be NULL.
 */
void input_reset_value_repeat(Input* input);
/*
 * Writes an editor frame from `held` using `input` history. `connected` is
 * zero on disconnection, which resets state and writes an empty frame.
 * `input` and `frame` must not be NULL or overlap.
 */
void input_update(Input* input, int connected, uint16_t held,
                  InputFrame* frame);

#endif // INPUT_H
