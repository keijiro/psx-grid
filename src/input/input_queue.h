/*
 * input_queue.h - Raw controller samples across the interrupt boundary
 *
 * The pad interrupt publishes complete reports into a fixed single-producer,
 * single-consumer ring. The main thread consumes them in order before Rust
 * normalizes input history. Callers own the queue storage.
 */

#ifndef INPUT_QUEUE_H
#define INPUT_QUEUE_H

#include <stdint.h>

// Eight delayed render frames can cover over 64 pad reports under load.
#define INPUT_QUEUE_CAPACITY 128

// Raw connection state and active-high button mask from one poll.
typedef struct
{
    int connected;
    uint16_t held;
} InputSample;

// Single-producer, single-consumer ring of raw samples.
typedef struct
{
    InputSample samples[INPUT_QUEUE_CAPACITY];
    unsigned read;
    unsigned write;
} InputQueue;

/*
 * Resets caller-owned `queue` to empty before its first push or pop.
 * `queue` must not be NULL.
 */
void input_queue_init(InputQueue* queue);
/*
 * Enqueues `sample` in `queue` and returns one if the queue overflowed, or
 * zero otherwise.
 * Overflow resets history and inserts a disconnect sample before the report.
 * `queue` must not be NULL.
 */
int input_queue_push(InputQueue* queue, InputSample sample);
/*
 * Removes the oldest sample into `sample` and returns one. Returns zero
 * without writing `sample` when the queue is empty.
 * `queue` and `sample` must not be NULL.
 */
int input_queue_pop(InputQueue* queue, InputSample* sample);

#endif // INPUT_QUEUE_H
