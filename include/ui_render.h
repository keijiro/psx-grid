/*
 * ui_render.h - Rust grid and menu frame layout
 *
 * The renderer reads caller-owned editor state and submits a complete frame
 * through the platform GPU backend. Submit frames on the main thread.
 */

#ifndef UI_RENDER_H
#define UI_RENDER_H

#include "audio_platform.h"
#include "editor.h"

/*
 * Submits one frame from the caller-owned `editor` view. `connected` reports
 * whether a pad is present. `playheads` may be NULL to omit the overlay.
 * Rendering does not modify either input. `editor` must not be NULL.
 */
void render_frame(const Editor* editor, int connected,
                  const AudioPlayheads* playheads);

#endif // UI_RENDER_H
