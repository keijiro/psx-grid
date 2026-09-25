/*
 * storage_fixture.c - Console storage coordinator fixture
 *
 * Implementation notes:
 *
 * Use only isolated card images: this fixture replaces logical slot 01 and
 * follows the application handoff path.
 */

#include "audio.h"
#include "editor.h"
#include "render.h"
#include "pad.h"

#include <psxgpu.h>
#include <psxapi.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

// Compile the actual main-thread coordinator into the fixture so menu requests,
// busy rendering, input reset and load ownership follow the application path.
#define main application_main
#include "../src/main.c"
#undef main

volatile unsigned storage_fixture_phase, storage_fixture_error;
volatile unsigned storage_fixture_removed;
volatile uintptr_t storage_fixture_isr_stack;
volatile unsigned storage_fixture_stack_ready;
static unsigned resumed_cross, resumed_start;
static uint8_t expected[SCORE_FILE_BYTES], actual[SCORE_FILE_BYTES];
extern volatile uint32_t audio_service_peak, audio_interval_peak, audio_services;
extern volatile uint32_t audio_dispatch_peak;
static CardBackend backend;
static unsigned io_services, services_before, io_polls, polls_before;
enum { MAIN_STACK_WINDOW = 64 * 1024, MAIN_STACK_MARGIN = 2 * 1024, ISR_STACK_BYTES = 4096 };
static const uint32_t stack_mark = 0xa55ac33c;
static uint32_t *main_stack_low, *main_stack_high, *isr_stack_low;
static uintptr_t main_stack_top;
static int stack_ready;
extern char _end[];

/*
 * Paints bounded main and interrupt stack windows with sentinels so later
 * reports can estimate this fixture path's peak stack use.
 */
static int stack_fill(void)
{
    uintptr_t sp;
    __asm__ volatile("move %0,$sp" : "=r"(sp));
    main_stack_top = sp;

    uintptr_t low = sp - MAIN_STACK_WINDOW;
    uintptr_t floor = ((uintptr_t)_end + 3) & ~(uintptr_t)3;
    if (low < floor)
        low = floor;
    // Exclude the active frame and paint only a bounded window. The reported
    // peak covers this fixture path rather than every possible hardware path.
    main_stack_low = (uint32_t*)low;
    main_stack_high = (uint32_t*)((sp - MAIN_STACK_MARGIN) & ~(uintptr_t)3);
    if (main_stack_low >= main_stack_high)
        return 0;

    isr_stack_low = (uint32_t*)storage_fixture_isr_stack;
    EnterCriticalSection();
    for (uint32_t* p = main_stack_low; p < main_stack_high; p++)
        *p = stack_mark;
    for (uint32_t* p = isr_stack_low; p < isr_stack_low + ISR_STACK_BYTES / 4; p++)
        *p = stack_mark;
    ExitCriticalSection();
    stack_ready = 1;
    return 1;
}

/*
 * Scans the sentinel windows to report observed stack high-water marks;
 * interrupt sampling is excluded while its window is inspected.
 */
static void stack_report(void)
{
    uint32_t* main_used = main_stack_low;
    while (main_used < main_stack_high && *main_used == stack_mark)
        main_used++;

    EnterCriticalSection();
    uint32_t* isr_used = isr_stack_low;
    while (isr_used < isr_stack_low + ISR_STACK_BYTES / 4 && *isr_used == stack_mark)
        isr_used++;

    unsigned main_peak = (unsigned)(main_stack_top - (uintptr_t)main_used);
    unsigned main_limit = (unsigned)(main_stack_top - (uintptr_t)_end);
    unsigned isr_peak =
        (unsigned)(ISR_STACK_BYTES - ((uintptr_t)isr_used - (uintptr_t)isr_stack_low));
    ExitCriticalSection();

    char line[160];
    snprintf(line,
             sizeof(line),
             "STACK main_peak=%u main_limit=%u isr_peak=%u isr_limit=%u\n",
             main_peak,
             main_limit,
             isr_peak,
             ISR_STACK_BYTES);
    *(const char* volatile*)0x1f802084 = line;
}

/*
 * Counts timer and pad callbacks during a complete backend session.
 */
static void measured_end(void* context)
{
    backend.end(context);
    io_services = audio_services - services_before;
    io_polls = pad_polls - polls_before;
}

/*
 * Captures callback counters before BIOS session entry, including failures.
 */
static CardResult measured_begin(void* context)
{
    services_before = audio_services;
    polls_before = pad_polls;
    CardResult result = backend.begin(context);
    if (result) {
        io_services = audio_services - services_before;
        io_polls = pad_polls - polls_before;
    }
    return result;
}

static void report(const char* stage, StorageResult result, AudioTime elapsed)
{
    char line[320];
    snprintf(line,
             sizeof(line),
             "STORAGE stage=%s result=%d ticks=%u services=%u cost=%u interval=%u dispatch=%u "
             "playing=%d free=%d io_services=%u io_polls=%u removed=%u\n",
             stage,
             result,
             (unsigned)elapsed,
             audio_services,
             audio_service_peak,
             audio_interval_peak,
             audio_dispatch_peak,
             audio_platform_playing(),
             storage.free_blocks,
             io_services,
             io_polls,
             storage_fixture_removed);
    *(const char* volatile*)0x1f802084 = line;
}

static void finish(int code)
{
    if (stack_ready)
        stack_report();
    *(const char* volatile*)0x1f802084 =
        code ? "STORAGE FIXTURE FAILED\n" : "STORAGE FIXTURE COMPLETE\n";
    *(volatile short*)0x1f802082 = code;
    for (;;)
        VSync(0);
}

static void frames(int count)
{
    for (int i = 0; i < count; i++) {
        render_frame(&editor, 1);
        InputSample sample;
        while (pad_read(&sample)) {
            InputFrame frame = input_update(&input, sample.connected, sample.held);
            resumed_cross += frame.cross;
            resumed_start += frame.start;
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
    backend = card_platform_backend();

    CardBackend measured = backend;
    measured.begin = measured_begin;
    measured.end = measured_end;
    storage_init(&storage, measured);
    score_create(&editor.score, 0, 0, 16);
    for (int i = 0; i < SEQUENCER_VOICES; i++) {
        TileValue value = score_default(TILE_NOTE);
        value.pitch = 36 + i;
        score_place_value(&editor.score, 1, i, value);
    }

    storage_fixture_stack_ready = 1;
    for (int timeout = 120; !storage_fixture_isr_stack; timeout--) {
        if (!timeout)
            finish(12);
        VSync(0);
    }

    if (storage_fixture_isr_stack < 0x80010000 ||
        storage_fixture_isr_stack > (uintptr_t)_end - ISR_STACK_BYTES ||
        (storage_fixture_isr_stack & 3) || !stack_fill())
        finish(13);

    audio_platform_update(&editor.score, 1, 1);
    frames(20);
    storage_fixture_phase = 1;
    editor.mode = EDIT_MAIN;
    editor.selected = 2;
    editor_update(&editor, (InputFrame){.connected = 1, .cross = 1});
    AudioTime at = audio_platform_time();
    storage_action(1);
    StorageResult result = storage.slots[0];
    report("refresh", result, audio_platform_time() - at);
    if (result != STORAGE_EMPTY && result != STORAGE_SAVED) {
        unsigned reports = pad_reports;
        frames(20);
        report("error-resume", result, pad_reports - reports);
        if (storage_fixture_error == 1 && pad_reports > reports && !audio_platform_playing())
            finish(0);
        finish(1);
    }
    if (result == STORAGE_SAVED) {
        editor.selected = 4;
        editor_update(&editor, (InputFrame){.connected = 1, .cross = 1});
        storage_action(1);
        result = storage.slots[0];
        score_format_encode(&editor.score, expected, 1, 1);
        score_format_encode(&storage.incoming, actual, 1, 1);
        report("restart-load", result, 0);
        if (result != STORAGE_SAVED || memcmp(expected, actual, sizeof(expected)))
            finish(2);
    }
    editor.mode = EDIT_MAIN;
    editor.selected = 3;
    editor_update(&editor, (InputFrame){.connected = 1, .cross = 1});
    if (editor.storage_request != STORAGE_ACTION_SAVE)
        finish(8);
    storage_fixture_phase = 2;
    at = audio_platform_time();
    storage_action(1);
    result = storage.slots[0];
    report("save", result, audio_platform_time() - at);
    if (strcmp(editor.message, "SAVED")) {
        unsigned reports = pad_reports;
        frames(20);
        report("error-resume", result, pad_reports - reports);
        if (storage_fixture_error == 1 && pad_reports > reports && !audio_platform_playing())
            finish(0);
        finish(3);
    }
    editor.selected = 4;
    editor_update(&editor, (InputFrame){.connected = 1, .cross = 1});
    if (editor.storage_request != STORAGE_ACTION_LOAD)
        finish(9);
    storage_fixture_phase = 3;
    at = audio_platform_time();
    storage_action(1);
    result = storage.slots[0];
    report("load", result, audio_platform_time() - at);
    score_format_encode(&editor.score, expected, 1, 1);
    score_format_encode(&storage.incoming, actual, 1, 1);
    if (result != STORAGE_SAVED || memcmp(expected, actual, sizeof(expected)))
        finish(4);
    if (editor.load_busy || audio_platform_replacing() || strcmp(editor.message, "LOADED"))
        finish(5);
    report("adopt", STORAGE_SAVED, 0);
    if (resumed_cross || resumed_start)
        finish(10);
    storage_fixture_phase = 4;
    unsigned reports = pad_reports;
    frames(20);
    report("resume", STORAGE_SAVED, pad_reports - reports);
    if (pad_reports == reports || audio_platform_playing())
        finish(7);
    if (storage_fixture_error == 2 && (resumed_cross != 1 || resumed_start != 1))
        finish(11);
    finish(0);
}
