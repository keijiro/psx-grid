/*
 * input.c - Interrupt-facing sample queue and Rust value ABI adapter
 *
 * Implementation notes:
 *
 * The queue crosses the pad interrupt and main thread, so it remains in C.
 * The Rust frame interface writes through a pointer; the adapter retains the
 * existing C value ABI, including hidden structure-return parameters.
 */

#include "input.h"

_Static_assert(sizeof(Input) == 40, "Rust Input ABI changed");
_Static_assert(sizeof(InputFrame) == 48, "Rust InputFrame ABI changed");

extern void input_update_rust(Input* input, int connected, uint16_t held,
                              InputFrame* frame);

InputFrame input_update(Input* input, int connected, uint16_t held)
{
    InputFrame frame;
    input_update_rust(input, connected, held, &frame);
    return frame;
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
