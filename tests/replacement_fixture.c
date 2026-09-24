// Standalone development fixture for platform replacement ownership and timing.
#include "audio.h"
#include "pad.h"
#include <psxapi.h>
#include <psxetc.h>
#include <psxgpu.h>
#include <psxspu.h>
#include <stdio.h>

#define REVERB_BASE 0x75000

extern volatile uint32_t audio_service_peak, audio_interval_peak, audio_services;
extern volatile uint32_t audio_dispatch_peak, audio_note_count;
static Score score, candidate;

static void log_line(const char *line) {
    *(const char *volatile *)0x1f802084 = line;
}

static void finish(int code) {
    log_line(code ? "REPLACEMENT FIXTURE FAILED\n" : "REPLACEMENT FIXTURE COMPLETE\n");
    *(volatile int16_t *)0x1f802082 = code;
    for (;;)
        VSync(0);
}

static void report(const char *name, int a, int b, AudioTime ticks) {
    char line[256];
    snprintf(line,
             sizeof(line),
             "REPLACEMENT case=%s a=%d b=%d ticks=%u services=%u cost=%u interval=%u dispatch=%u "
             "notes=%u playing=%d revision=%u\n",
             name,
             a,
             b,
             (unsigned)ticks,
             (unsigned)audio_services,
             (unsigned)audio_service_peak,
             (unsigned)audio_interval_peak,
             (unsigned)audio_dispatch_peak,
             (unsigned)audio_note_count,
             audio_platform_playing(),
             (unsigned)audio_platform_revision());
    log_line(line);
}

static void frames(int count) {
    for (int i = 0; i < count; i++) {
        AudioTime begin = audio_platform_time();
        while (audio_platform_time() - begin < SEQUENCER_HZ / 60) {
        }
        InputSample sample;
        while (pad_read(&sample)) {
        }
    }
}

static void make_score(Score *score, int bpm, int size, int pitch) {
    score_init(score);
    score_create(score, 0, 0, 1);
    score_set_bpm(score, bpm);
    score_set_reverb(score, (ReverbSettings){size, 100});
    for (int i = 0; i < SEQUENCER_VOICES; i++) {
        if (i)
            score_create(score, 0, i, 1);
        TileValue note = score_default(TILE_NOTE);
        note.pitch = pitch + i;
        note.length = 128;
        score_place_value(score, 1, i, note);
    }
}

static AudioTime wait_adopt(Score *score) {
    AudioTime begin = audio_platform_time();
    while (!audio_platform_take_replacement(score)) {
        frames(1);
        if (audio_platform_time() - begin > 2 * SEQUENCER_HZ)
            finish(90);
    }
    return audio_platform_time() - begin;
}

static uint32_t reverb_word(void) {
    uint32_t value = 0;
    SpuSetTransferStartAddr(REVERB_BASE);
    SpuRead(&value, sizeof(value));
    SpuIsTransferCompleted(SPU_TRANSFER_WAIT);
    return value;
}

static void set_reverb_word(uint32_t value) {
    SpuSetTransferStartAddr(REVERB_BASE);
    SpuWrite(&value, sizeof(value));
    SpuIsTransferCompleted(SPU_TRANSFER_WAIT);
}

int main(void) {
    log_line("REPLACEMENT checkpoint=boot\n");
    ResetGraph(0);
    SetVideoMode(MODE_NTSC);
    log_line("REPLACEMENT checkpoint=gpu\n");
    audio_platform_init();
    pad_init();
    frames(4);
    log_line("REPLACEMENT checkpoint=init\n");

    make_score(&score, 120, 0, 36);
    log_line("REPLACEMENT checkpoint=score\n");
    audio_platform_update(&score, 1, 1);
    frames(4);
    candidate = score;
    score_set_bpm(&candidate, 121);
    uint32_t before = audio_platform_revision();
    // Hold ordinary publication pending deterministically. This ownership
    // probe's artificial service gap is excluded from timing acceptance.
    uint16_t mask = IRQ_MASK;
    IRQ_MASK = mask & ~(1u << IRQ_TIMER0);
    audio_platform_update(&candidate, 1, 0);
    int pending = audio_platform_revision() == before;
    make_score(&candidate, 173, 1, 48);
    uint32_t incoming_revision = candidate.revision;
    int first = audio_platform_replace(&candidate);
    int repeated = audio_platform_replace(&candidate);
    IRQ_MASK = mask;
    audio_service_peak = audio_interval_peak = 0;
    AudioTime elapsed = wait_adopt(&score);
    report("supersede", first, repeated, elapsed);
    if (!pending || !first || repeated || score.bpm != 173 ||
        audio_platform_revision() != incoming_revision)
        finish(1);

    make_score(&candidate, 181, 1, 52);
    if (!audio_platform_replace(&candidate))
        finish(2);
    audio_platform_update(&score, 1, 1);
    int waiting = audio_platform_replacing();
    elapsed = wait_adopt(&score);
    report("stop-adopt", waiting, score.bpm, elapsed);
    if (!waiting || score.bpm != 181 || audio_platform_playing())
        finish(3);

    audio_platform_update(&score, 1, 1);
    frames(3);
    make_score(&candidate, 191, 1, 55);
    if (!audio_platform_replace(&candidate))
        finish(4);
    audio_platform_update(&score, 0, 0);
    waiting = audio_platform_replacing();
    elapsed = wait_adopt(&score);
    report("disconnect-adopt", waiting, score.bpm, elapsed);
    if (!waiting || score.bpm != 191 || audio_platform_playing())
        finish(5);

    // A same-size adoption must retain the delay memory. Changing the size
    // retires that tail and clears the entire shared work area after adoption.
    audio_platform_update(&score, 1, 1);
    frames(3);
    uint32_t marker = 0x51a7c3e9;
    set_reverb_word(marker);
    make_score(&candidate, 193, 1, 57);
    audio_service_peak = audio_interval_peak = 0;
    AudioTime begin = audio_platform_time();
    if (!audio_platform_replace(&candidate))
        finish(6);
    elapsed = wait_adopt(&score);
    uint32_t retained = reverb_word();
    report("reverb-same", retained == marker, SPU_REVERB_VOL_L, audio_platform_time() - begin);
    if (retained != marker || score.reverb.size != 1)
        finish(7);

    marker = 0x29d46fb1;
    set_reverb_word(marker);
    make_score(&candidate, 197, 2, 60);
    audio_service_peak = audio_interval_peak = 0;
    begin = audio_platform_time();
    if (!audio_platform_replace(&candidate))
        finish(8);
    elapsed = wait_adopt(&score);
    uint32_t cleared = reverb_word();
    report("reverb-different", cleared == 0, SPU_REVERB_VOL_L, audio_platform_time() - begin);
    if (cleared || score.reverb.size != 2)
        finish(9);

    frames(8);
    report("twelve-note-load", audio_note_count, 0, elapsed);
    if (audio_note_count < 12 || !audio_platform_playing())
        finish(10);
    finish(0);
}
