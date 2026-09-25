/*
 * psxetc.h - Minimal video SDK declarations for host rendering tests
 */

#ifndef GPU_STUB_ETC_H
#define GPU_STUB_ETC_H

// Video mode used by the renderer's fixed 320-by-240 frame.
#define MODE_NTSC 0
/* Records the selected host video mode. */
void SetVideoMode(int mode);
/* Advances the host display synchronization stub. */
void VSync(int mode);

#endif // GPU_STUB_ETC_H
