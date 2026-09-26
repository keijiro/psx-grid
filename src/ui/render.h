/*
 * render.h - PlayStation grid and menu renderer
 *
 * The renderer reads caller-owned editor state and submits a complete frame
 * through fixed double buffers to the GPU. Initialize after the video system
 * is ready and submit frames on the main thread.
 */

#ifndef RENDER_H
#define RENDER_H

#include "ui/editor.h"

/*
 * Initializes video state, texture data and both command buffers.
 */
void render_init(void);
/*
 * Submits one frame from the caller-owned `editor` view. `connected` reports
 * whether a pad is present; rendering does not modify the editor.
 * `editor` must not be NULL.
 */
void render_frame(const Editor* editor, int connected);

#endif // RENDER_H
