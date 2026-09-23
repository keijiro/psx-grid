#ifndef INPUT_H
#define INPUT_H
#include <stdint.h>
enum { INPUT_LEFT=1, INPUT_RIGHT=2, INPUT_UP=4, INPUT_DOWN=8, INPUT_CROSS=16, INPUT_CIRCLE=32, INPUT_START=64, INPUT_SELECT=128 };
#define INPUT_DELAY 18
#define INPUT_INTERVAL 3
typedef struct { uint16_t previous; int connected, direction, countdown; } Input;
typedef struct { int connected, dx, dy, cross, circle, cross_held, cross_released, start, select; } InputFrame;
#define INPUT_QUEUE_CAPACITY 64
typedef struct { int connected; uint16_t held; } InputSample;
typedef struct { InputSample samples[INPUT_QUEUE_CAPACITY]; unsigned read, write; } InputQueue;
void input_queue_init(InputQueue *queue);
int input_queue_push(InputQueue *queue, InputSample sample);
int input_queue_pop(InputQueue *queue, InputSample *sample);
void input_init(Input *input);
void input_reset_repeat(Input *input);
InputFrame input_update(Input *input, int connected, uint16_t held);
#endif
