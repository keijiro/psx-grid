#include "editor.h"
#include "render.h"
#include <psxpad.h>
#include <psxapi.h>

static Editor editor;
static Input input;
static uint8_t pad_buffers[2][34];
int main(void) {
    editor_init(&editor); input_init(&input); render_init();
    InitPAD(pad_buffers[0], sizeof(pad_buffers[0]), pad_buffers[1], sizeof(pad_buffers[1]));
    StartPAD();
    for (;;) {
        PADTYPE *pad = (PADTYPE *)pad_buffers[0];
        int connected = pad->stat == 0 && (pad->type == 4 || pad->type == 5 || pad->type == 7);
        uint16_t buttons = connected ? (uint16_t)~pad->btn : 0;
        uint16_t held = 0;
        if (buttons & PAD_LEFT) held |= INPUT_LEFT;
        if (buttons & PAD_RIGHT) held |= INPUT_RIGHT;
        if (buttons & PAD_UP) held |= INPUT_UP;
        if (buttons & PAD_DOWN) held |= INPUT_DOWN;
        if (buttons & PAD_CROSS) held |= INPUT_CROSS;
        if (buttons & PAD_CIRCLE) held |= INPUT_CIRCLE;
        EditorMode before = editor.mode;
        InputFrame frame = input_update(&input,connected,held);
        editor_update(&editor,frame);
        if (editor.mode != before) input_reset_repeat(&input);
        render_frame(&editor,connected);
    }
}
