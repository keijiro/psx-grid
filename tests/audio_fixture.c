/*
 * audio_fixture.c - Console audio timing and synthesis fixture
 *
 * Implementation notes:
 *
 * This standalone development fixture runs outside the editor executable
 * and reports hardware counters for emulator-driven checks.
 */

#include "value_api.h"

#include "audio/audio_platform.h"
#include "audio_synth.h"
#include "editor.h"
#include "input/pad.h"
#include "ui/render_backend.h"
#include "ui_render.h"

#include <psxapi.h>
#include <psxetc.h>
#include <psxgpu.h>
#include <psxspu.h>
#include <stdarg.h>
#include <stdio.h>

extern volatile uint32_t audio_service_peak;
extern volatile uint32_t audio_interval_peak;
extern volatile uint32_t audio_services;
extern volatile uint32_t audio_voice_steals;
extern volatile uint32_t audio_skipped_notes;
extern volatile uint32_t audio_overloads;
extern volatile uint32_t audio_dispatch_peak;
extern volatile uint32_t audio_note_count;
extern volatile uint32_t audio_first_note;
extern volatile uint32_t audio_last_note;
extern volatile uint32_t audio_started;
extern volatile uint32_t audio_control_time;
extern volatile uint32_t audio_modulation_start;
static Editor editor;
static uint32_t capture[256];

static void log_message(const char* format, ...)
{
    char message[256];
    va_list args;
    va_start(args, format);
    vsnprintf(message, sizeof(message), format, args);
    va_end(args);
    // The emulator's BIOS character output can duplicate characters when
    // preempted by a timer IRQ. Submit a complete diagnostic through Redux's
    // message port, leaving interrupts enabled while formatting the text.
    *(const char* volatile*)0x1f802084 = message;
}

static void report(const char* phase)
{
    EnterCriticalSection();
    uint32_t v[] =
    {
        audio_services,
        audio_service_peak,
        audio_interval_peak,
        audio_voice_steals,
        audio_skipped_notes,
        audio_overloads,
        SPU_CH_ADSR_VOL(0),
        SPU_CH_VOL_L(0),
        audio_dispatch_peak,
        audio_note_count,
        audio_last_note - audio_first_note
    };
    unsigned volumes = 0;
    unsigned sends =
        (unsigned)SPU_REVERB_ON1 | ((unsigned)SPU_REVERB_ON2 << 16);
    for (int i = 0; i < AUDIO_HARDWARE_VOICES; i++)
    {
        volumes |= SPU_CH_VOL_L(i) | SPU_CH_VOL_R(i);
    }
    audio_service_peak = audio_interval_peak = 0;
    ExitCriticalSection();
    log_message("AUDIO %s: calls=%u cost=%u interval=%u steals=%u skipped=%u "
                "overloads=%u env=%u "
                "vol=%u volumes=%u send=%u\n",
                phase, (unsigned)v[0], (unsigned)v[1], (unsigned)v[2],
                (unsigned)v[3], (unsigned)v[4], (unsigned)v[5], (unsigned)v[6],
                (unsigned)v[7], volumes, sends);
    log_message("DISPATCH %s: peak=%u count=%u span=%u\n", phase,
                (unsigned)v[8], (unsigned)v[9], (unsigned)v[10]);
}

static void frames(int count)
{
    for (int i = 0; i < count; i++)
    {
        editor.x = i % 128;
        editor.y = (i / 4) % 64;
        render_frame(&editor, 1);
        InputSample sample;
        while (pad_read(&sample))
        {
        }
    }
}

/*
 * Publishes repeated edits while playback is active to expose missed or
 * coalesced revision handoffs.
 */
static void publish_revisions(const char* phase, int count)
{
    uint32_t copy_peak = 0;
    uint32_t adopt_peak = 0;
    int adopted = 0;
    TileId tile = editor.score.lanes[0].tiles[0];
    for (int i = 0; i < count; i++)
    {
        // The dense fixture also reverses all heads to exercise the full runner
        // reconciliation and execution-order sort at every adoption.
        if (editor.score.lanes[SCORE_LANES - 1].active)
        {
            for (int lane = 0; lane < SCORE_LANES; lane++)
            {
                editor.score.lanes[lane].x =
                    (i % 2 ? lane : SCORE_LANES - 1 - lane) * 6;
            }
        }
        TileValue value = editor.score.tiles[tile].value;
        value.pitch = value.pitch == 48 ? 55 : 48;
        test_score_edit(&editor.score, tile, value);
        AudioTime begin = audio_platform_time();
        audio_platform_update(&editor.score, 1, 0);
        uint32_t copied = (uint32_t)(audio_platform_time() - begin);
        if (copied > copy_peak) copy_peak = copied;
        while (audio_platform_revision() != editor.score.revision &&
               audio_platform_time() - begin < SEQUENCER_HZ / 2)
        {
            audio_platform_update(&editor.score, 1, 0);
        }
        uint32_t elapsed = (uint32_t)(audio_platform_time() - begin);
        if (elapsed > adopt_peak) adopt_peak = elapsed;
        adopted += audio_platform_revision() == editor.score.revision;
        frames(1);
    }
    log_message("LIVE %s: edits=%d adopted=%d copy=%u adoption=%u playing=%d\n",
                phase, count, adopted, (unsigned)copy_peak,
                (unsigned)adopt_peak, audio_platform_playing());
    report(phase);
}

/*
 * Stops transport with a publication outstanding to check that the next start
 * owns a complete score.
 */
static void stop_pending(int disconnect)
{
    int pending = 0;
    uint32_t before = 0;
    for (int i = 0; i < 32 && !pending; i++)
    {
        TileId tile = editor.score.lanes[0].tiles[0];
        TileValue value = editor.score.tiles[tile].value;
        value.pitch = value.pitch == 48 ? 55 : 48;
        test_score_edit(&editor.score, tile, value);
        audio_platform_update(&editor.score, 1, 0);
        // Freeze only the adoption window after the large copy has finished.
        // This makes the pending-stop case observable without masking the timer
        // during a score copy or depending on emulator host scheduling.
        uint16_t mask = IRQ_MASK;
        IRQ_MASK = mask & ~(1u << IRQ_TIMER0);
        before = audio_platform_revision();
        pending = before != editor.score.revision;
        if (pending)
        {
            audio_platform_update(&editor.score, !disconnect, !disconnect);
        }
        IRQ_MASK = mask;
    }
    frames(2);
    log_message("LIVE %s: pending=%d playing=%d unchanged=%d\n",
                disconnect ? "pending disconnect" : "pending stop", pending,
                audio_platform_playing(), audio_platform_revision() == before);
    report(disconnect ? "pending disconnect" : "pending stop");
}

/*
 * Exercises rapid main-thread edits while the interrupt still owns the
 * previous snapshot.
 */
static void coalesce_revision(void)
{
    int pending = 0;
    int deferred = 0;
    for (int i = 0; i < 32 && !pending; i++)
    {
        int channel = (editor.score.lanes[0].channel + 1) % SCORE_CHANNELS;
        score_set_channel(&editor.score, 0, channel);
        audio_platform_update(&editor.score, 1, 0);
        uint16_t mask = IRQ_MASK;
        IRQ_MASK = mask & ~(1u << IRQ_TIMER0);
        uint32_t before = audio_platform_revision();
        pending = before != editor.score.revision;
        if (pending)
        {
            SoundSettings sound = editor.score.sounds[channel];
            sound.reverb = !sound.reverb;
            test_score_set_sound(&editor.score, channel, sound);
            audio_platform_update(&editor.score, 1, 0);
            deferred = audio_platform_revision() == before;
        }
        IRQ_MASK = mask;
    }
    // No more edits follow. The ordinary main-loop update must eventually
    // submit the revision that arrived while both buffers belonged to audio.
    AudioTime begin = audio_platform_time();
    while (audio_platform_revision() != editor.score.revision &&
           audio_platform_time() - begin < SEQUENCER_HZ / 2)
    {
        audio_platform_update(&editor.score, 1, 0);
    }
    log_message(
        "LIVE coalesced: pending=%d deferred=%d adopted=%d playing=%d\n",
        pending, deferred, audio_platform_revision() == editor.score.revision,
        audio_platform_playing());
}

/*
 * Samples audible voice behavior and timing counters on the actual SPU path.
 */
static void synthesis_checks(void)
{
    for (int wave = 0; wave < WAVE_COUNT; wave++)
    {
        score_init(&editor.score);
        score_create(&editor.score, 0, 0, 1);
        score_set_division(&editor.score, 0, 1);
        test_score_set_sound(&editor.score, 0,
                        (SoundSettings){0, 5, wave, wave, 0, 0, 0, 200, 0});
        TileValue note = test_score_default(TILE_NOTE);
        note.length = 1280;
        for (int i = 0; i < SEQUENCER_VOICES; i++)
        {
            test_score_place_value(&editor.score, 1, i, note);
        }
        audio_platform_update(&editor.score, 1, 1);
        // Sample the first chord before another score step can steal it on
        // slower Debug builds. Two VSyncs also fill the decoded SPU buffer.
        frames(2);
        SpuSetTransferStartAddr(0x800);
        SpuRead(capture, sizeof(capture));
        SpuIsTransferCompleted(SPU_TRANSFER_WAIT);
        int peak = 0;
        const int16_t* pcm = (const int16_t*)capture;
        for (int j = 0; j < 512; j++)
        {
            int n = pcm[j] < 0 ? -pcm[j] : pcm[j];
            if (n > peak) peak = n;
        }
        int pairs = 0;
        for (int i = 0; i < AUDIO_HARDWARE_VOICES; i += 2)
        {
            pairs += SPU_CH_FREQ(i) == SPU_CH_FREQ(i + 1) &&
                     SPU_CH_VOL_L(i) + SPU_CH_VOL_L(i + 1) == AUDIO_LEVEL &&
                     SPU_CH_LOOP_ADDR(i) == SPU_CH_LOOP_ADDR(i + 1);
        }
        // Capture is pre-volume voice output, not the final mixer. The bound
        // scales its observed peak by all twelve complementary gain budgets.
        log_message(
            "WAVE wave=%d peak=%d pairs=%d bound=%d\n", wave, peak, pairs,
            (int)(((uint64_t)peak * SEQUENCER_VOICES * AUDIO_LEVEL + 16383) /
                  16384));
        report("wave chord");
        audio_platform_update(&editor.score, 0, 0);
        frames(2);
    }
    for (int sign = -1; sign <= 1; sign += 2)
    {
        score_init(&editor.score);
        score_create(&editor.score, 0, 0, 1);
        score_set_division(&editor.score, 0, 1);
        test_score_set_sound(&editor.score, 0,
                        (SoundSettings){0, 500, WAVE_SAW, WAVE_SQUARE, 100, 100,
                                        sign * 24, 200, 0});
        TileValue note = test_score_default(TILE_NOTE);
        note.length = 1280;
        for (int i = 0; i < SEQUENCER_VOICES; i++)
        {
            test_score_place_value(&editor.score, 1, i, note);
        }
        audio_platform_update(&editor.score, 1, 1);
        const int times[] =
        {
            2, 25, 50, 100, 150, 199, 250
        };
        // Wait for the observed key-on, since Redux may defer playback beyond
        // the score event. Buffer readback until sampling ends so formatting
        // does not consume the short sweep window.
        unsigned samples[7][5];
        while (audio_control_time == audio_modulation_start ||
               audio_note_count < SEQUENCER_VOICES)
        {
        }
        for (int j = 0; j < 7; j++)
        {
            while (audio_platform_time() - audio_modulation_start <
                   audio_ms(times[j]))
            {
            }
            EnterCriticalSection();
            unsigned elapsed = audio_control_time - audio_modulation_start;
            unsigned pitch = SPU_CH_FREQ(0);
            unsigned a = SPU_CH_VOL_L(0);
            unsigned b = SPU_CH_VOL_L(1);
            int pairs = 0;
            for (int i = 0; i < AUDIO_HARDWARE_VOICES; i += 2)
            {
                // Mixer readback can acknowledge different pairs on different
                // services. Only the two halves must share pitch and gain
                // budget.
                pairs += SPU_CH_FREQ(i) == SPU_CH_FREQ(i + 1) &&
                         SPU_CH_VOL_L(i) + SPU_CH_VOL_L(i + 1) == AUDIO_LEVEL &&
                         SPU_CH_LOOP_ADDR(i) != SPU_CH_LOOP_ADDR(i + 1);
            }
            ExitCriticalSection();
            samples[j][0] = elapsed;
            samples[j][1] = pitch;
            samples[j][2] = a;
            samples[j][3] = b;
            samples[j][4] = pairs;
        }
        for (int j = 0; j < 7; j++)
        {
            log_message(
                "SYNTH sign=%d ms=%d ticks=%u pitch=%u a=%u b=%u pairs=%u\n",
                sign, times[j], samples[j][0], samples[j][1], samples[j][2],
                samples[j][3], samples[j][4]);
        }
        report("sweep chord");
        audio_platform_update(&editor.score, 0, 0);
        frames(2);
        report("sweep stop");
    }
}

/*
 * Observes whether deferred hardware key-on preserves the short second-wave
 * transient.
 */
static void transient_checks(void)
{
    unsigned errors = 0;
    unsigned initial = 0;
    unsigned completed = 0;
    unsigned pending = 0;
    for (int trial = 0; trial < 16; trial++)
    {
        score_init(&editor.score);
        score_create(&editor.score, 0, 0, 1);
        score_set_division(&editor.score, 0, 1);
        test_score_set_sound(
            &editor.score, 0,
            (SoundSettings){0, 5, WAVE_SINE, WAVE_NOISE, 0, 1, 24, 1, 0});
        TileValue note = test_score_default(TILE_NOTE);
        note.length = 1280;
        test_score_place_value(&editor.score, 1, 0, note);
        audio_platform_update(&editor.score, 1, 1);
        int saw_initial = 0;
        int saw_end = 0;
        while (audio_platform_time() - audio_started < audio_ms(100))
        {
            EnterCriticalSection();
            unsigned count = audio_note_count;
            unsigned elapsed = audio_control_time - audio_modulation_start;
            unsigned a = SPU_CH_VOL_L(0);
            unsigned b = SPU_CH_VOL_L(1);
            unsigned env_a = SPU_CH_ADSR_VOL(0);
            unsigned env_b = SPU_CH_ADSR_VOL(1);
            ExitCriticalSection();
            if (!count) continue;
            unsigned duration = (unsigned)audio_ms(1);
            unsigned expected = elapsed < duration ? (duration - elapsed) *
                                                         AUDIO_LEVEL / duration
                                                   : 0;
            if (a + b != AUDIO_LEVEL || b != expected) errors++;
            if (env_a <= 1 || env_b <= 1)
            {
                pending++;
                if (b != AUDIO_LEVEL) errors++;
            }
            if (!elapsed && b == AUDIO_LEVEL) saw_initial = 1;
            if (elapsed >= duration && !b)
            {
                saw_end = 1;
                break;
            }
        }
        initial += saw_initial;
        completed += saw_end;
        audio_platform_update(&editor.score, 0, 0);
        frames(2);
    }
    log_message(
        "TRANSIENT trials=16 initial=%u completed=%u pending=%u errors=%u\n",
        initial, completed, pending, errors);
}

static unsigned reverb_mask(void)
{
    return (unsigned)SPU_REVERB_ON1 | ((unsigned)SPU_REVERB_ON2 << 16);
}

static void channel_sample(int wet, const char* phase)
{
    EnterCriticalSection();
    unsigned mask = reverb_mask();
    unsigned a = SPU_CH_VOL_L(0);
    unsigned b = SPU_CH_VOL_L(2);
    unsigned wave_a = SPU_CH_LOOP_ADDR(0);
    unsigned wave_b = SPU_CH_LOOP_ADDR(2);
    unsigned count = audio_note_count;
    unsigned left = SPU_REVERB_VOL_L;
    unsigned right = SPU_REVERB_VOL_R;
    ExitCriticalSection();
    log_message("CHANNEL wet=%d phase=%s send=%u a=%u b=%u wave_a=%u wave_b=%u "
                "count=%u left=%u right=%u\n",
                wet, phase, mask, a, b, wave_a, wave_b, count, left, right);
}

static void channel_wait(int ms)
{
    while (audio_platform_time() - audio_started < audio_ms(ms))
    {
    }
}

/*
 * Checks that channel sound settings and wet sends reach the hardware pair
 * selected by the score.
 */
static void channel_checks(void)
{
    log_message("RAM score=%u sequencer=%u editor=%u\n",
                (unsigned)sizeof(Score), (unsigned)sizeof(Sequencer),
                (unsigned)sizeof(Editor));
    for (int wet = 0; wet <= 1; wet++)
    {
        score_init(&editor.score);
        for (int i = 0; i < 2; i++)
        {
            score_create(&editor.score, i * 6, 0, 4);
            score_set_division(&editor.score, i, 4);
            score_set_channel(&editor.score, i, i);
            SoundSettings sound = SOUND_DEFAULT;
            sound.attack = 0;
            sound.release = 400;
            sound.reverb = i ? !wet : wet;
            sound.mix_attack = sound.mix_release = 0;
            sound.wave_a = sound.wave_b = i ? WAVE_SQUARE : WAVE_SAW;
            test_score_set_sound(&editor.score, i, sound);
            TileValue note = test_score_default(TILE_NOTE);
            note.length = i ? 1280 : 8;
            test_score_place_value(&editor.score, i * 6 + 1, 0, note);
            if (!i) test_score_place_value(&editor.score, 3, 0, note);
        }
        test_score_set_reverb(&editor.score, (ReverbSettings){2, 100});
        audio_platform_update(&editor.score, 1, 1);
        channel_wait(100);
        channel_sample(wet, "held");
        SoundSettings sound = editor.score.sounds[0];
        sound.reverb = !wet;
        sound.release = 0;
        test_score_set_sound(&editor.score, 0, sound);
        audio_platform_update(&editor.score, 1, 0);
        channel_wait(150);
        channel_sample(wet, "edited");
        channel_wait(300);
        channel_sample(wet, "release");
        channel_wait(700);
        channel_sample(wet, "retired");
        channel_wait(1100);
        channel_sample(wet, "reuse");
        // A dry replacement must not erase the shared effect's feedback memory
        // or mute its return. This is a device-memory check, not proof of the
        // subjective decay heard at the final output.
        if (wet)
        {
            unsigned nonzero = 0;
            unsigned base = (unsigned)SPU_REVERB_ADDR * 8;
            for (unsigned address = base; address < 0x80000;
                 address += sizeof(capture))
            {
                unsigned bytes = 0x80000 - address;
                if (bytes > sizeof(capture)) bytes = sizeof(capture);
                SpuSetTransferStartAddr(address);
                SpuRead(capture, bytes);
                SpuIsTransferCompleted(SPU_TRANSFER_WAIT);
                for (unsigned i = 0; i < bytes / sizeof(capture[0]); i++)
                {
                    nonzero += capture[i] != 0;
                }
            }
            log_message("CHANNEL tail nonzero=%u send=%u left=%u\n", nonzero,
                        reverb_mask(), (unsigned)SPU_REVERB_VOL_L);
        }
        channel_wait(1300);
        channel_sample(wet, "completed");
        audio_platform_update(&editor.score, wet ? 0 : 1, wet ? 0 : 1);
        // Trial zero exercises transport stop; trial one disconnects.
        frames(2);
        channel_sample(wet, "stopped");
        audio_platform_update(&editor.score, 1, 1);
        frames(4);
        channel_sample(wet, "restart");
        audio_platform_update(&editor.score, 0, 0);
        frames(2);
    }
    score_init(&editor.score);
    for (int i = 0; i < SCORE_LANES; i++)
    {
        score_create(&editor.score, i * 6, 0, 1);
        score_set_channel(&editor.score, i, i % SCORE_CHANNELS);
        score_set_division(&editor.score, i, 1);
        SoundSettings sound = SOUND_DEFAULT;
        sound.reverb = i % 2;
        sound.attack = sound.release = sound.mix_attack = sound.mix_release = 0;
        sound.wave_a = sound.wave_b = i % WAVE_COUNT;
        test_score_set_sound(&editor.score, i % SCORE_CHANNELS, sound);
        TileValue note = test_score_default(TILE_NOTE);
        note.length = 10;
        if (i < SEQUENCER_VOICES)
        {
            test_score_place_value(&editor.score, i * 6 + 1, 0, note);
        }
    }
    audio_service_peak = audio_interval_peak = 0;
    audio_platform_update(&editor.score, 1, 1);
    channel_wait(100);
    report("channel capacity first");
    channel_wait(2100);
    unsigned active = 0;
    for (int i = 0; i < AUDIO_HARDWARE_VOICES; i += 2)
    {
        active += SPU_CH_VOL_L(i) + SPU_CH_VOL_L(i + 1) == AUDIO_LEVEL;
    }
    log_message("CHANNEL capacity lanes=%d channels=%d pairs=%u send=%u\n",
                SCORE_LANES, SCORE_CHANNELS, active, reverb_mask());
    report("channel capacity");
    audio_platform_update(&editor.score, 0, 0);
    frames(2);
}

/*
 * Drives sound-menu edits during playback to expose publication and modulation
 * timing errors.
 */
static void menu_audio_checks(void)
{
    for (int bpm = 60; bpm <= 240; bpm *= 4)
    {
        score_init(&editor.score);
        score_create(&editor.score, 0, 0, 1);
        score_place(&editor.score, 1, 0, TILE_NOTE);
        score_set_bpm(&editor.score, bpm);
        audio_platform_update(&editor.score, 1, 1);
        frames(150);
        log_message("TEMPO bpm=%d count=%u span=%u\n", bpm,
                    (unsigned)audio_note_count,
                    (unsigned)(audio_last_note - audio_first_note));
        audio_platform_update(&editor.score, 0, 0);
        frames(2);
    }
    for (int size = 0; size < 3; size++)
    {
        score_init(&editor.score);
        // The DMA scan can cross a two-second step; keep the held note from
        // retriggering before its captured send is inspected.
        score_create(&editor.score, 0, 0, 16);
        score_set_division(&editor.score, 0, 1);
        TileValue note = test_score_default(TILE_NOTE);
        note.length = 1280;
        test_score_place_value(&editor.score, 1, 0, note);
        SoundSettings sound = editor.score.sounds[0];
        sound.reverb = 1;
        test_score_set_sound(&editor.score, 0, sound);
        test_score_set_reverb(&editor.score, (ReverbSettings){size, 100});
        audio_platform_update(&editor.score, 1, 1);
        frames(60);
        // Read the effect work area through SPU DMA. Nonzero feedback memory
        // proves the effect receives samples, beyond register programming.
        unsigned nonzero = 0;
        unsigned base = (unsigned)SPU_REVERB_ADDR * 8;
        for (unsigned address = base; address < 0x80000;
             address += sizeof(capture))
        {
            unsigned bytes = 0x80000 - address;
            if (bytes > sizeof(capture)) bytes = sizeof(capture);
            SpuSetTransferStartAddr(address);
            SpuRead(capture, bytes);
            SpuIsTransferCompleted(SPU_TRANSFER_WAIT);
            for (unsigned i = 0; i < bytes / sizeof(capture[0]); i++)
            {
                nonzero += capture[i] != 0;
            }
        }
        log_message(
            "REVERB size=%d base=%u left=%u right=%u send=%u nonzero=%u\n",
            size, base, (unsigned)SPU_REVERB_VOL_L, (unsigned)SPU_REVERB_VOL_R,
            (unsigned)SPU_REVERB_ON1 | ((unsigned)SPU_REVERB_ON2 << 16),
            nonzero);
        sound.reverb = 0;
        test_score_set_sound(&editor.score, 0, sound);
        audio_platform_update(&editor.score, 1, 0);
        frames(4);
        log_message("REVERB held size=%d send=%u\n", size,
                    (unsigned)SPU_REVERB_ON1 |
                        ((unsigned)SPU_REVERB_ON2 << 16));
        sound.reverb = 1;
        test_score_set_sound(&editor.score, 0, sound);
        test_score_set_reverb(&editor.score, (ReverbSettings){size, 0});
        audio_platform_update(&editor.score, 1, 0);
        frames(4);
        log_message("REVERB zero size=%d left=%u right=%u send=%u\n", size,
                    (unsigned)SPU_REVERB_VOL_L, (unsigned)SPU_REVERB_VOL_R,
                    (unsigned)SPU_REVERB_ON1 |
                        ((unsigned)SPU_REVERB_ON2 << 16));
        test_score_set_reverb(&editor.score, (ReverbSettings){(size + 1) % 3, 100});
        audio_service_peak = audio_interval_peak = 0;
        AudioTime begin = audio_platform_time();
        audio_platform_update(&editor.score, 1, 0);
        unsigned changed = (unsigned)(audio_platform_time() - begin);
        frames(2);
        log_message(
            "REVERB change size=%d ticks=%u cost=%u interval=%u playing=%d\n",
            size, changed, (unsigned)audio_service_peak,
            (unsigned)audio_interval_peak, audio_platform_playing());
        audio_platform_update(&editor.score, 0, 0);
        frames(2);
    }
}

int main(void)
{
    editor_init(&editor);
    render_init();
    audio_platform_init();
    pad_init();
    frames(60);
    report("idle");
    score_create(&editor.score, 0, 0, 16);
    for (int i = 1; i <= 16; i++) score_place(&editor.score, i, 0, TILE_NOTE);
    audio_platform_update(&editor.score, 1, 1);
    frames(180);
    report("C4 loop");
    audio_platform_update(&editor.score, 0, 0);
    frames(2);
    report("stopped");
    score_init(&editor.score);
    score_create(&editor.score, 0, 0, 1);
    score_set_division(&editor.score, 0, 64);
    score_place(&editor.score, 1, 0, TILE_NOTE);
    audio_platform_update(&editor.score, 1, 1);
    frames(4);
    unsigned original_pitch = SPU_CH_FREQ(0);
    publish_revisions("live pitch", 25);
    frames(4);
    log_message("LIVE pitch registers: before=%u after=%u\n", original_pitch,
                (unsigned)SPU_CH_FREQ(0));
    coalesce_revision();
    stop_pending(0);
    audio_platform_update(&editor.score, 1, 1);
    frames(4);
    stop_pending(1);
    score_init(&editor.score);
    score_create(&editor.score, 0, 0, 16);
    for (int i = 0; i < SEQUENCER_VOICES; i++)
    {
        TileValue chord = test_score_default(TILE_NOTE);
        chord.pitch = 36 + i;
        test_score_place_value(&editor.score, 1, i, chord);
    }
    audio_platform_update(&editor.score, 1, 1);
    frames(6);
    report("12-note chord");
    audio_platform_update(&editor.score, 0, 0);
    frames(2);
    // Capture voice 1's decoded signal through the actual SPU DMA read path.
    // Its ring is unordered here; this checks signal presence, not continuity.
    for (int octave = 0; octave <= 9; octave += octave ? 5 : 4)
    {
        score_init(&editor.score);
        score_create(&editor.score, 0, 0, 1);
        TileValue note = test_score_default(TILE_NOTE);
        note.pitch = octave * 12;
        note.length = 1280;
        test_score_place_value(&editor.score, 1, 0, note);
        test_score_place_value(&editor.score, 1, 1, note);
        audio_platform_update(&editor.score, 1, 1);
        frames(12);
        SpuSetTransferStartAddr(0x800);
        SpuRead(capture, sizeof(capture));
        SpuIsTransferCompleted(SPU_TRANSFER_WAIT);
        int peak = 0;
        const int16_t* pcm = (const int16_t*)capture;
        for (int j = 0; j < 512; j++)
        {
            int n = pcm[j] < 0 ? -pcm[j] : pcm[j];
            if (n > peak) peak = n;
        }
        log_message("CAPTURE C%d peak=%d env=%u\n", octave, peak,
                    (unsigned)SPU_CH_ADSR_VOL(1));
        audio_platform_update(&editor.score, 0, 0);
        frames(2);
    }
    const int times[] =
    {
        0, 1, 5, 100
    };
    for (int i = 0; i < 4; i++)
    {
        score_init(&editor.score);
        score_create(&editor.score, 0, 0, 1);
        score_set_division(&editor.score, 0, 1);
        test_score_set_sound(&editor.score, 0,
                        (SoundSettings){times[i], times[i], WAVE_SINE,
                                        WAVE_SINE, 0, 0, 0, 200, 0});
        TileValue note = test_score_default(TILE_NOTE);
        note.length = 5;
        test_score_place_value(&editor.score, 1, 0, note);
        audio_platform_update(&editor.score, 1, 1);
        uint32_t peak = 0;
        uint32_t zero = 0;
        AudioTime gate = SEQUENCER_HZ / 2;
        while (!zero)
        {
            AudioTime elapsed = audio_platform_time() - audio_started;
            unsigned level = SPU_CH_VOL_L(0);
            if (!peak && level == AUDIO_LEVEL) peak = (uint32_t)elapsed;
            if (elapsed >= gate && !level)
            {
                zero = (uint32_t)(elapsed - gate) + 1;
            }
            if (elapsed > gate + audio_ms(times[i] + 100)) break;
        }
        log_message("ENVELOPE request=%d attack_ticks=%u release_ticks=%u\n",
                    times[i], (unsigned)peak, (unsigned)(zero ? zero - 1 : 0));
        audio_platform_update(&editor.score, 0, 0);
        frames(2);
    }
    synthesis_checks();
    transient_checks();
    menu_audio_checks();
    channel_checks();
    // Deliberate sequencer stress beyond the persistence budget: 16 disjoint
    // four-step lanes with 64-deep stacks still exercise all 4096 pool slots.
    // Playback retains this overload coverage independently of edit admission.
    score_init(&editor.score);
    TileId id = 1;
    for (int i = 0; i < 16; i++)
    {
        Lane* lane = &editor.score.lanes[i];
        *lane = (Lane){.active = 1,
                       .x = i * 6,
                       .y = 0,
                       .length = 4,
                       .division = 64,
                       .channel = i % SCORE_CHANNELS};
        editor.score.lane_generation[i] = ++editor.score.generation;
        for (int j = 0; j < 4; j++)
        {
            lane->tiles[j] = id;
            for (int k = 0; k < 64; k++, id++)
            {
                editor.score.tiles[id] =
                    (Tile){test_score_default(TILE_NOTE),
                           k == 63 ? 0 : (TileId)(id + 1), -1};
                editor.score.tile_generation[id] = ++editor.score.generation;
            }
        }
    }
    audio_platform_update(&editor.score, 1, 1);
    frames(12);
    report("4096 tiles");
    publish_revisions("live 4096 tiles", 32);
    audio_platform_update(&editor.score, 0, 0);
    frames(2);
    report("final stop");
    log_message("AUDIO FIXTURE COMPLETE\n");
    // PCSX-Redux development exit port; -testmode makes this terminate the
    // emulator. Physical hardware must use the ordinary editor executable.
    *(volatile int16_t*)0x1f802082 = 0;
    for (;;) VSync(0);
}
