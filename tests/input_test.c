#include "input.h"
#include "editor.h"
#include <assert.h>
#include <stdio.h>
static Editor editor;
static Input input;
static InputQueue queue;
static int moves,presses,releases;
static void drain(void) {
    InputSample s;
    while(input_queue_pop(&queue,&s)) {
        EditorMode before=editor.mode;
        InputFrame f=input_update(&input,s.connected,s.held);
        moves+=f.dx!=0; presses+=f.cross; releases+=f.cross_released;
        editor_update(&editor,f);
        if(editor.mode!=before) input_reset_repeat(&input);
    }
}
int main(void) {
    editor_init(&editor); input_init(&input); input_queue_init(&queue);
    input_queue_push(&queue,(InputSample){1,0}); drain();
    // Entire taps happen while the consumer is busy, including an X tap
    // opening a menu immediately followed by a direction in that new mode.
    for(int n=0;n<100;n++) {
        assert(!input_queue_push(&queue,(InputSample){1,INPUT_RIGHT}));
        assert(!input_queue_push(&queue,(InputSample){1,0}));
        if(n%10==9) drain();
    }
    assert(moves==100);
    input_queue_push(&queue,(InputSample){1,INPUT_CROSS});
    input_queue_push(&queue,(InputSample){1,0});
    input_queue_push(&queue,(InputSample){1,INPUT_DOWN});
    drain();
    assert(presses==1 && releases==1 && editor.mode==EDIT_MENU && editor.selected==1);
    // Overflow cancels the active gesture and suppresses a held reconnect.
    editor_init(&editor); input_init(&input);
    input_update(&input,1,0);
    editor_update(&editor,input_update(&input,1,INPUT_CROSS));
    assert(editor.gesture);
    input_queue_init(&queue);
    for(int n=0;n<INPUT_QUEUE_CAPACITY-1;n++)
        assert(!input_queue_push(&queue,(InputSample){1,INPUT_CROSS|INPUT_RIGHT}));
    assert(input_queue_push(&queue,(InputSample){1,INPUT_CROSS|INPUT_RIGHT}));
    drain();
    assert(!editor.gesture && editor.mode==EDIT_PLANE && editor.x==1);
    input_queue_push(&queue,(InputSample){1,0});
    input_queue_push(&queue,(InputSample){1,INPUT_RIGHT}); drain();
    assert(editor.x==2);
    puts("PASS: ordered input history, mode changes, overflow and reconnect");
}
