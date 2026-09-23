#ifndef PAD_H
#define PAD_H
#include "input.h"
// Initialize after render_init and audio_platform_init. Timer service and
// callbacks share the SDK interrupt context; only reads run on the main thread.
void pad_init(void);
void pad_service(void);
// Main-thread exclusive handoff; the caller resets Input after resuming.
void pad_suspend(void);
void pad_resume(void);
int pad_read(InputSample *sample);
extern volatile unsigned pad_polls, pad_reports, pad_timeouts, pad_overflows, pad_id;
#endif
