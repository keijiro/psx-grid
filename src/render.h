/*
 * render.h - PlayStation grid and menu renderer
 *
 * The renderer reads editor state and submits a complete double-buffered
 * frame to the GPU. Call initialization after the video system is ready.
 */

#ifndef RENDER_H
#define RENDER_H

#include "editor.h"

/* Initializes video state, texture data and both command buffers. */
void render_init(void);
/*
 * Submits one frame from the caller-owned `editor` view. `connected` reports
 * whether a pad is present; rendering does not modify the editor.
 */
void render_frame(const Editor* editor, int connected);

#endif // RENDER_H
