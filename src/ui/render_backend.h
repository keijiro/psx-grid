/*
 * render_backend.h - Low-level PlayStation GPU packet interface
 *
 * Rust supplies clipped primitive geometry between begin and present calls.
 * Calls are serialized on the main thread, and the fixed packet buffer may
 * discard primitives after its capacity is exhausted.
 */

#ifndef RENDER_BACKEND_H
#define RENDER_BACKEND_H

#include "ui/render.h"

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
