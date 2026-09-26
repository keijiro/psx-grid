/*
 * main.c - Application startup and main-thread coordination
 *
 * Implementation notes:
 *
 * The main loop coordinates input, audio publication, rendering and
 * synchronous BIOS card sessions without sharing mutable editor state with
 * callbacks.
 */

#include "audio/audio_platform.h"
#include "editor.h"
#include "input/pad.h"
#include "ui/render_backend.h"
#include "ui_render.h"

#include <psxapi.h>
#include <psxetc.h>
#include <psxgpu.h>

/*
 * Main-thread state stays separate from the interrupt-owned audio snapshots;
 * storage handoffs publish a complete score back into the editor.
 */
static Editor editor;
static Input input;
static Storage storage;

/*
 * Clears the load lock only after audio adopts the incoming snapshot, then
 * refreshes the menu capacity against the newly editable score.
 */
static void replacement_acknowledged(void)
{
    editor.load_busy = 0;
    editor.message = "LOADED";
    editor.mode = EDIT_MAIN;
    editor.selected = 5;
    editor.target = 0;
    editor.gesture = 0;
    editor_refresh_capacity(&editor);
}

/*
 * Shows a busy frame, transfers callback ownership to BIOS card service, then
 * restores input and audio before publishing the result.
 */
static void storage_action(int connected)
{
    int action = editor.storage_request;
    int slot = editor.storage_slot;
    editor.storage_request = STORAGE_ACTION_NONE;
    if (editor.load_busy || audio_platform_replacing())
    {
        editor.message = storage_message(STORAGE_BUSY);
        return;
    }
    if (action < STORAGE_ACTION_CHECK || action > STORAGE_ACTION_LOAD ||
        slot < 1 || slot > STORAGE_SLOTS)
    {
        editor.message = storage_message(STORAGE_IO);
        return;
    }
    if (action == STORAGE_ACTION_SAVE &&
        score_format_encode(&editor.score, storage.save, slot, 1) != FORMAT_OK)
    {
        editor.message = storage_message(STORAGE_SCORE_FULL);
        return;
    }
    editor.message = action == STORAGE_ACTION_SAVE   ? "SAVING"
                     : action == STORAGE_ACTION_LOAD ? "READING"
                                                     : "CHECKING CARD";
    editor.slot_status = STORAGE_BUSY;
    // Submit a visible busy frame while SDK GPU callbacks still work.
    render_frame(&editor, connected);
    // The renderer displays the previously submitted buffer. A second frame
    // makes the busy slot status visible before synchronous access begins.
    render_frame(&editor, connected);
    DrawSync(0);
    pad_suspend();
    audio_platform_card_stop();
    // StopCallback saves the SDK IRQ/DMA state and leaves CPU interrupts
    // disabled. BIOS card service needs VBlank and SIO0 after that handoff; no
    // critical section may span its event waits or filesystem calls.
    StopCallback();
    IRQ_MASK = (1u << IRQ_VBLANK) | (1u << IRQ_SIO0);
    ExitCriticalSection();
    StorageResult result = action == STORAGE_ACTION_SAVE
                               ? storage_save(&storage, slot, &editor.score)
                           : action == STORAGE_ACTION_LOAD
                               ? storage_load(&storage, slot)
                               : storage_refresh(&storage, slot);
    EnterCriticalSection();
    RestartCallback();
    audio_platform_card_resume(&editor.score);
    pad_resume();
    input_init(&input);
    editor.gesture = 0;
    editor.message = storage_message(result);
    editor.slot_status = storage.slots[slot - 1];
    editor.card_free = storage.free_blocks;
    if (action == STORAGE_ACTION_LOAD && result == STORAGE_SAVED)
    {
        storage.incoming.revision = editor.score.revision + 1;
        if (audio_platform_replace(&storage.incoming))
        {
            // Physical access stopped transport before the BIOS handoff, so
            // replacement adopts immediately after SDK restoration.
            editor.load_busy = 1;
            editor.load_slot = slot;
            if (audio_platform_take_replacement(&editor.score))
            {
                replacement_acknowledged();
            }
        }
    }
}

int main(void)
{
    editor_init(&editor);
    input_init(&input);
    render_init();
    audio_platform_init();
    pad_init();
    CardBackend backend = card_platform_backend();
    storage_init(&storage, &backend);
    int connected = 0;
    for (;;)
    {
        InputSample sample;
        // Replay completed polls in order, including press/release pairs
        // received during a slow render or model transaction.
        for (int n = 0; n < INPUT_QUEUE_CAPACITY && pad_read(&sample); n++)
        {
            connected = sample.connected;
            EditorMode before = editor.mode;
            int selected = editor.selected;
            InputFrame frame;
            input_update(&input, connected, sample.held, &frame);
            editor_update(&editor, &frame);
            if (editor.mode != before) input_reset_repeat(&input);
            else if (editor.selected != selected)
            {
                input_reset_value_repeat(&input);
            }
            if (editor.storage_request)
            {
                storage_action(connected);
                break;
            }
            audio_platform_update(&editor.score, connected, frame.start);
        }
        audio_platform_update(&editor.score, connected, 0);
        if (editor.load_busy && audio_platform_take_replacement(&editor.score))
        {
            replacement_acknowledged();
        }
        editor.slot_status =
            editor.load_busy && editor.storage_slot == editor.load_slot
                ? STORAGE_BUSY
                : storage.slots[editor.storage_slot - 1];
        editor.playing = audio_platform_playing();
        editor.snapshot_dirty = editor.playing && editor.score.revision !=
                                                      audio_platform_revision();
        render_frame(&editor, connected);
    }
}
