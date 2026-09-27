/*
 * render_backend.h - PlayStation video and GPU packet interface
 *
 * The backend initializes video and accepts clipped primitive geometry from
 * Rust between begin and present calls. Calls are serialized on the main
 * thread, and the fixed packet buffer may discard primitives after its
 * capacity is exhausted.
 */

#ifndef RENDER_BACKEND_H
#define RENDER_BACKEND_H

#include <stdint.h>

#if defined(__mips__) && !defined(NDEBUG)
// A short history fits beside the editor without allocating at runtime.
#define RENDER_MONITOR_HISTORY 48

// Timer ticks use the audio backend's 4,233,600 Hz clock.
typedef struct
{
    uint32_t history[RENDER_MONITOR_HISTORY];
    uint32_t history_next;
    uint32_t history_count;
    uint32_t frame_ticks;
    uint32_t work_ticks;
    uint32_t work_peak;
    uint32_t gpu_wait_ticks;
    uint32_t vsync_wait_ticks;
    uint32_t missed_vsyncs;
    uint32_t packet_peak;
    uint32_t packet_overflows;
    uint32_t audio_dispatch_peak;
    uint32_t audio_skipped_notes;
    uint32_t audio_queue_underruns;
    uint32_t audio_overloads;
} RenderMonitor;

/*
 * Restarts frame timing after initialization or a BIOS card handoff. The
 * cumulative audio and packet counters remain owned by their backends.
 */
void render_backend_monitor_reset(void);

/*
 * Returns the most recently completed frame's measurements. The returned
 * storage is static and remains valid until the next frame is submitted.
 */
const RenderMonitor* render_backend_monitor(void);
#endif

/*
 * Initializes video state, texture data and both command buffers.
 */
void render_init(void);

/*
 * Clears the next command buffer before drawing a frame.
 */
void render_backend_begin(void);
/*
 * Appends one clipped gray rectangle at the given ordering-table depth.
 * Coordinates and dimensions must describe a positive region on screen.
 */
void render_backend_tile(int depth, int x, int y, int w, int h, int gray);
/*
 * Appends one clipped atlas sprite at the given ordering-table depth.
 * Both the screen and atlas regions must be within their bounds.
 */
void render_backend_sprite(int depth, int x, int y, int u, int v, int w, int h);
/*
 * Inserts texture setup, submits the completed frame, and swaps buffers.
 */
void render_backend_present(void);

#endif // RENDER_BACKEND_H
