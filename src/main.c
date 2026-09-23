#include "editor.h"
#include "audio.h"
#include "render.h"
#include "pad.h"

static Editor editor;
static Input input;
static Storage storage;
static void storage_action(int connected) {
    int action=editor.storage_request, slot=editor.storage_slot;
    editor.storage_request=STORAGE_ACTION_NONE;
    if(editor.load_busy) return;
    editor.message=action==STORAGE_ACTION_SAVE?"SAVING":action==STORAGE_ACTION_LOAD?"READING":"CHECKING CARD";
    editor.slot_status=STORAGE_BUSY;
    // Submit a visible busy frame before handing over SIO. Every operation
    // ends ownership before returning, including backend and parser errors.
    render_frame(&editor,connected);
    // The renderer displays the previously submitted buffer. A second frame
    // makes the busy message visible before the synchronous operation begins.
    render_frame(&editor,connected);
    StorageResult result=action==STORAGE_ACTION_SAVE?storage_save(&storage,slot,&editor.score):
        action==STORAGE_ACTION_LOAD?storage_load(&storage,slot):storage_refresh(&storage,slot);
    input_init(&input);
    editor.gesture=0; editor.message=storage_message(result);
    editor.slot_status=storage.slots[slot-1]; editor.card_free=storage.free_blocks;
    if(action==STORAGE_ACTION_LOAD && result==STORAGE_SAVED) {
        storage.incoming.revision=editor.score.revision+1;
        if(audio_platform_replace(&storage.incoming)) {
            editor.load_busy=1; editor.load_slot=slot; editor.message="WAITING FOR LAP";
        }
    }
}
int main(void) {
    editor_init(&editor); input_init(&input); render_init();
    audio_platform_init(); pad_init(); storage_init(&storage,card_platform_backend());
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
            if(editor.storage_request) { storage_action(connected); break; }
            audio_platform_update(&editor.score,connected,frame.start);
        }
        audio_platform_update(&editor.score,connected,0);
        if(editor.load_busy && audio_platform_take_replacement(&editor.score)) {
            editor.load_busy=0; editor.message="LOADED";
            editor.mode=EDIT_MAIN; editor.selected=4; editor.target=0; editor.gesture=0;
            editor_refresh_capacity(&editor);
        }
        editor.slot_status=editor.load_busy && editor.storage_slot==editor.load_slot?STORAGE_BUSY:storage.slots[editor.storage_slot-1];
        editor.playing=audio_platform_playing();
        editor.snapshot_dirty=editor.playing && editor.score.revision!=audio_platform_revision();
        render_frame(&editor,connected);
    }
}
