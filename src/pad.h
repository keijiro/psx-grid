/*
 * pad.h - Interrupt-driven PlayStation controller polling
 *
 * Timer and serial callbacks publish raw reports into a fixed input queue;
 * the main thread reads them through pad_read. Card access explicitly
 * transfers serial-port ownership to the BIOS.
 */

#ifndef PAD_H
#define PAD_H

#include "input_queue.h"

/*
 * Initializes after render_init and audio_platform_init. Timer service and
 * callbacks share the SDK interrupt context; only reads run on the main thread.
 */
void pad_init(void);
/*
 * Advances the pad transaction from the shared audio timer callback.
 */
void pad_service(void);
/*
 * Waits for pad polling to yield SIO0 before a main-thread BIOS card session.
 * The caller resets Input after pad_resume to discard old held buttons.
 */
void pad_suspend(void);
/*
 * Restores pad polling after BIOS card access has ended.
 */
void pad_resume(void);
/*
 * Pops a queued report into `sample` and returns one. Returns zero without
 * writing `sample` if no report is queued.
 * `sample` must not be NULL.
 */
int pad_read(InputSample* sample);
// Debugger-visible poll, report, timeout, overflow and controller-ID counters.
extern volatile unsigned pad_polls;
extern volatile unsigned pad_reports;
extern volatile unsigned pad_timeouts;
extern volatile unsigned pad_overflows;
extern volatile unsigned pad_id;

#endif // PAD_H
