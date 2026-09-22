#include "pad.h"
#include <psxapi.h>
#include <psxetc.h>
#include <psxgpu.h>
#include <psxpad.h>

static InputQueue queue;
static int phase, received, length;
static uint8_t reply[9];
static uint16_t started, ready_at;
volatile unsigned pad_polls, pad_reports, pad_timeouts, pad_overflows, pad_id;
enum { IDLE, READY, RECEIVING };
// Timer 2 runs at CLK/8. Leave at least 30 us after selection and each ACK,
// including the longer first-command gap needed by some analog controllers.
#define SETTLE_TICKS 128
#define TIMEOUT_TICKS 33869 // 8 ms, below the 16-bit timer's wrap period.
static uint16_t clock_now(void) { return (uint16_t)TIMER_VALUE(2); }
static void publish(int connected) {
    uint16_t held=0;
    if(connected) {
        unsigned buttons=(uint16_t)~(reply[3]|reply[4]<<8);
        if(buttons&PAD_LEFT) held|=INPUT_LEFT;
        if(buttons&PAD_RIGHT) held|=INPUT_RIGHT;
        if(buttons&PAD_UP) held|=INPUT_UP;
        if(buttons&PAD_DOWN) held|=INPUT_DOWN;
        if(buttons&PAD_CROSS) held|=INPUT_CROSS;
        if(buttons&PAD_CIRCLE) held|=INPUT_CIRCLE;
        if(buttons&PAD_START) held|=INPUT_START;
        pad_id=reply[1];
        pad_reports++;
    }
    pad_overflows+=input_queue_push(&queue,(InputSample){connected,held});
    SIO_CTRL(0)=0;
    phase=IDLE;
}
static void receive(void) {
    if(phase!=RECEIVING || !(SIO_STAT(0)&2)) {
        SIO_CTRL(0)|=0x10;
        return;
    }
    reply[received++]=SIO_DATA(0);
    SIO_CTRL(0)|=0x10;
    if(received==2) {
        // Digital, analog joystick and DualShock reports. Consume the whole
        // report even though the editor uses only its digital button bits.
        if(reply[1]==0x41) length=5;
        else if(reply[1]==0x53 || reply[1]==0x73) length=9;
        else { publish(0); return; }
    }
    if(received==3 && reply[2]!=0x5a) { publish(0); return; }
    if(received==length) { publish(1); return; }
    ready_at=clock_now();
    phase=READY;
}
static void vblank(void) {
    // BIOS pad polling can be bypassed when the SDK drains a VBlank that
    // arrived during another IRQ. Starting here covers that path as well.
    if(phase!=IDLE) { pad_timeouts++; publish(0); }
    pad_polls++;
    SIO_CTRL(0)=0x40;
    SIO_MODE(0)=0x0d;
    SIO_BAUD(0)=0x88;
    SIO_CTRL(0)=0x1003;
    received=0; length=5;
    started=ready_at=clock_now();
    phase=READY;
}
void pad_service(void) {
    if(phase==IDLE) return;
    uint16_t now=clock_now();
    if((uint16_t)(now-started)>=TIMEOUT_TICKS) { pad_timeouts++; publish(0); return; }
    if(phase!=READY || (uint16_t)(now-ready_at)<SETTLE_TICKS) return;
    // Intermediate bytes advance on ACK. The final byte has no ACK, so use
    // the one-byte RX interrupt for it. Neither ISR waits for the controller.
    SIO_CTRL(0)=received==length-1?0x0803:0x1003;
    phase=RECEIVING;
    SIO_DATA(0)=received==0?0x01:received==1?0x42:0;
}
void pad_init(void) {
    EnterCriticalSection();
    input_queue_init(&queue);
    InterruptCallback(IRQ_SIO0,receive);
    ExitCriticalSection();
    VSyncCallback(vblank);
}
int pad_read(InputSample *sample) {
    EnterCriticalSection();
    int found=input_queue_pop(&queue,sample);
    ExitCriticalSection();
    return found;
}
