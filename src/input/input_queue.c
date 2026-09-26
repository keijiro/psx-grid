/*
 * input_queue.c - Interrupt-facing controller sample queue
 *
 * Implementation notes:
 *
 * The queue crosses the pad interrupt and main thread, so it remains in C.
 */

#include "input/input_queue.h"

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
        // A stalled consumer loses ordering. Insert a disconnect before the
        // report so the release guard cancels an incomplete gesture.
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
