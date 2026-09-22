#include "editor.h"
#include "audio.h"
#include "render.h"
#include "pad.h"

static Editor editor;
static Input input;
int main(void) {
    editor_init(&editor); input_init(&input); render_init();
    audio_platform_init(); pad_init();
    int connected=0;
    for (;;) {
        InputSample sample;
        // Replay completed polls in order, including press/release pairs
        // received during a slow render or model transaction.
        for(int n=0;n<INPUT_QUEUE_CAPACITY && pad_read(&sample);n++) {
            connected=sample.connected;
            EditorMode before=editor.mode;
            InputFrame frame=input_update(&input,connected,sample.held);
            editor_update(&editor,frame);
            if(editor.mode!=before) input_reset_repeat(&input);
            audio_platform_update(&editor.score,connected,frame.start);
        }
        editor.playing=audio_platform_playing();
        editor.snapshot_dirty=editor.playing && editor.score.revision!=audio_platform_revision();
        render_frame(&editor,connected);
    }
}
