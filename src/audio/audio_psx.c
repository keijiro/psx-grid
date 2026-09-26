/*
 * audio_psx.c - PlayStation SPU and timer transport
 *
 * Implementation notes:
 *
 * The timer callback owns the active sequencer while the main thread owns
 * score preparation, SPU resource setup and card-session handoffs.
 */

#include "audio/audio.h"

#include <psxapi.h>
#include <psxetc.h>
#include <psxspu.h>

#include "input/pad.h"
#include "wave_samples.h"

/*
 * The main thread owns the spare until it publishes it. The interrupt owns
 * both buffers while one is pending, and releases the old one only after a
 * whole slice has finished. No score copy needs to mask timer interrupts.
 */
static Score snapshots[2];
static volatile int active_snapshot;
static volatile int pending_snapshot = -1;
static Sequencer states[2];
static Sequencer* volatile seq = &states[0];

enum
{
    REPLACE_IDLE,                   // No replacement is in progress.
    REPLACE_PREPARING,              // A spare snapshot is being prepared.
    REPLACE_WAITING,                // Playback has not reached the handoff.
    REPLACE_ADOPTED                 // The caller has not taken the new score.
};

/*
 * Replacement moves from main-thread preparation to interrupt adoption and
 * remains acknowledged until the editor takes the new snapshot.
 */
static volatile int replacement_state;
static int replacement_snapshot;
static Audio* audio;
static AudioTime clock_ticks;
static AudioTime service_previous;
static uint16_t last_counter;
static volatile int enabled;
static volatile uint32_t published_revision;
volatile uint32_t audio_service_peak;
volatile uint32_t audio_interval_peak;
volatile uint32_t audio_services;
volatile uint32_t audio_voice_steals;
volatile uint32_t audio_skipped_notes;
volatile uint32_t audio_overloads;
volatile uint32_t audio_dispatch_peak;
volatile uint32_t audio_note_count;
volatile uint32_t audio_first_note;
volatile uint32_t audio_last_note;
volatile uint32_t audio_started;
volatile uint32_t audio_control_time;
volatile uint32_t audio_modulation_start;
static AudioTime read_clock(void);

/*
 * Small/medium/large use the documented Room, Studio Medium and Hall networks:
 * https://psx-spx.consoledev.net/soundprocessingunitspu/#spu-reverb-examples
 * Keeping the feedback coefficients together avoids unstable combinations of
 * raw SPU controls. Amount changes only the wet return; dry gain stays fixed.
 */
static const uint16_t reverb_presets[3][32] =
{
    {
        0x007d, 0x005b, 0x6d80, 0x54b8, 0xbed0, 0,      0,      0xba80,
        0x5800, 0x5300, 0x04d6, 0x0333, 0x03f0, 0x0227, 0x0374, 0x01ef,
        0x0334, 0x01b5, 0,      0,      0,      0,      0,      0,
        0,      0,      0x01b4, 0x0136, 0x00b8, 0x005c, 0x8000, 0x8000
    },
    {
        0x00b1, 0x007f, 0x70f0, 0x4fa8, 0xbce0, 0x4510, 0xbef0, 0xb4c0,
        0x5280, 0x4ec0, 0x0904, 0x076b, 0x0824, 0x065f, 0x07a2, 0x0616,
        0x076c, 0x05ed, 0x05ec, 0x042e, 0x050f, 0x0305, 0x0462, 0x02b7,
        0x042f, 0x0265, 0x0264, 0x01b2, 0x0100, 0x0080, 0x8000, 0x8000
    },
    {
        0x01a5, 0x0139, 0x6000, 0x5000, 0x4c00, 0xb800, 0xbc00, 0xc000,
        0x6000, 0x5c00, 0x15ba, 0x11bb, 0x14c2, 0x10bd, 0x11bc, 0x0dc1,
        0x11c0, 0x0dc3, 0x0dc0, 0x09c1, 0x0bc4, 0x07c1, 0x0a00, 0x06cd,
        0x09c2, 0x05c1, 0x05c0, 0x041a, 0x0274, 0x013a, 0x8000, 0x8000
    }
};
#define REVERB_BASE 0x75000
_Static_assert(WAVE_SPU_ADDRESS + sizeof(wave_data) <= REVERB_BASE,
               "Waves overlap reverb work area");
static int reverb_size = -1;
static int reverb_amount = -1;

/*
 * Reconfigures the shared SPU network only after muting and clearing its delay
 * area; amount edits preserve the selected network.
 */
static void update_reverb(const Score* score)
{
    if (reverb_size != score->reverb.size)
    {
        static const uint32_t silence[256] =
        {
            0
        };
        // Retire the old tail before changing delay addresses. DMA runs only on
        // the main thread with timer interrupts live; a size edit must not
        // stall the sequencer clock or let old buffer contents become noise.
        SPU_REVERB_VOL_L = SPU_REVERB_VOL_R = 0;
        SPU_CTRL &= ~0x80;
        for (unsigned address = REVERB_BASE; address < 0x80000;
             address += sizeof(silence))
        {
            SpuSetTransferStartAddr(address);
            SpuWrite(silence, sizeof(silence));
            SpuIsTransferCompleted(SPU_TRANSFER_WAIT);
        }
        static const unsigned sizes[] =
        {
            0x26c0, 0x4840, 0xade0
        };
        SpuSetReverbAddr(0x80000 - sizes[score->reverb.size]);
        volatile uint16_t* registers = (volatile uint16_t*)0x1f801dc0;
        for (int i = 0; i < 32; i++)
        {
            registers[i] = reverb_presets[score->reverb.size][i];
        }
        SPU_CTRL |= 0x80;
        reverb_size = score->reverb.size;
        reverb_amount = -1;
    }
    if (reverb_amount != score->reverb.amount)
    {
        reverb_amount = score->reverb.amount;
        int level = reverb_amount * 0x3fff / 100;
        SPU_REVERB_VOL_L = SPU_REVERB_VOL_R = level;
    }
}

/*
 * Register caches avoid redundant SPU writes in the timer callback. -1 forces
 * initial writes; volume entries are also reset after a card-session handoff.
 */
static int cached_volume[AUDIO_HARDWARE_VOICES];
static int cached_pitch[SEQUENCER_VOICES];

/*
 * Prepares both hardware halves from the same pitch bank while software
 * envelopes retain the score's millisecond attack and release settings.
 */
static void start_voice(void* ctx, int slot, int bank,
                        const SoundSettings* sound)
{
    (void)ctx;
    for (int half = 0; half < 2; half++)
    {
        int voice = slot * 2 + half;
        int wave = half ? sound->wave_b : sound->wave_a;
        unsigned address =
            WAVE_SPU_ADDRESS + wave_offsets[bank * WAVE_COUNT + wave];
        SpuSetVoiceStartAddr(voice, address);
        SPU_CH_LOOP_ADDR(voice) = getSPUAddr(address);
        // Software amplitude retains the score's gate and 0..16000 ms times.
        // Fast attack and stationary maximum sustain hold hardware ADSR open.
        SPU_CH_ADSR1(voice) = 0x00ff;
        SPU_CH_ADSR2(voice) = 0x1fc0;
    }
}

static void volume(void* ctx, int slot, int a, int b)
{
    (void)ctx;
    for (int half = 0; half < 2; half++)
    {
        int voice = slot * 2 + half;
        int level = half ? b : a;
        if (cached_volume[voice] == level) continue;
        cached_volume[voice] = level;
        SpuSetVoiceVolume(voice, level, level);
    }
}

static void pitch(void* ctx, int slot, int value)
{
    (void)ctx;
    if (cached_pitch[slot] == value) return;
    cached_pitch[slot] = value;
    SpuSetVoicePitch(slot * 2, value);
    SpuSetVoicePitch(slot * 2 + 1, value);
}

/*
 * Redux reports 1 for a pending key-on, including a stolen voice whose old
 * envelope was nonzero. Wait for both halves of the logical pair.
 */
static int ready(void* ctx, int slot)
{
    (void)ctx;
    return (SPU_CH_ADSR_VOL(slot * 2) & 0x7fff) > 1 &&
           (SPU_CH_ADSR_VOL(slot * 2 + 1) & 0x7fff) > 1;
}

static uint32_t hardware_mask(uint32_t logical)
{
    uint32_t mask = 0;
    for (int i = 0; i < SEQUENCER_VOICES; i++)
    {
        if (logical & (1u << i)) mask |= 3u << (i * 2);
    }
    return mask;
}

/*
 * Commits logical key masks after Rust rejects overdue starts. Rust supplies
 * the wet mask so this callback does not reenter the mutable synthesizer.
 */
static void flush(void* ctx, uint32_t starts, uint32_t stops, uint32_t sends,
                  uint32_t dispatch_peak)
{
    (void)ctx;
    if (!(starts | stops)) return;
    AudioTime now = read_clock();
    // Flush is the sole runtime send-mask writer. Captured settings survive
    // holds and release ramps; rejected starts and retired pairs contribute no
    // bits. Recompute before key-on so reuse replaces both halves together. The
    // main thread changes only the shared network and its wet return.
    SPU_REVERB_ON1 = sends & 0xffff;
    SPU_REVERB_ON2 = sends >> 16;
    if (stops) SpuSetKey(0, hardware_mask(stops));
    if (starts)
    {
        SpuSetKey(1, hardware_mask(starts));
        for (int i = 0; i < SEQUENCER_VOICES; i++)
        {
            if (starts & (1u << i))
            {
                if (dispatch_peak > audio_dispatch_peak)
                {
                    audio_dispatch_peak = dispatch_peak;
                }
                if (!audio_note_count) audio_first_note = (uint32_t)now;
                audio_last_note = (uint32_t)now;
                audio_note_count++;
            }
        }
    }
}

/*
 * Extends a wrapping 16-bit hardware counter into the transport clock; callers
 * must sample before an entire wrap elapses.
 */
static AudioTime read_clock(void)
{
    uint16_t counter = (uint16_t)TIMER_VALUE(2);
    clock_ticks += (uint16_t)(counter - last_counter);
    last_counter = counter;
    return clock_ticks;
}

static AudioTime audio_clock(void* context)
{
    (void)context;
    return read_clock();
}

/*
 * Publishes the adopted state and its score revision only after the sequencer
 * returns the new active pointer.
 */
static void replacement_adopted(Sequencer* next)
{
    if (next == seq) return;
    seq = next;
    active_snapshot = replacement_snapshot;
    published_revision = snapshots[active_snapshot].revision;
    replacement_state = REPLACE_ADOPTED;
}

/*
 * Shares the timer callback between pad polling, score publication and bounded
 * audio work, then records deadline diagnostics.
 */
static void service(void)
{
    AudioTime now = read_clock();
    uint32_t interval = (uint32_t)(now - service_previous);
    service_previous = now;
    if (interval > audio_interval_peak) audio_interval_peak = interval;
    pad_service();
    if (enabled)
    {
        int pending = pending_snapshot;
        if (pending >= 0 && sequencer_resync(seq, &snapshots[pending], now))
        {
            active_snapshot = pending;
            published_revision = snapshots[pending].revision;
            pending_snapshot = -1;
        }
        replacement_adopted(sequencer_service(seq, now));
    }
    else
    {
        NoteSink sink = audio_sink(audio);
        sink.advance(sink.context, now);
    }
    // Register fixtures use the completed control timestamp, since reading the
    // main-thread clock before readback can straddle another service.
    audio_control_time = (uint32_t)now;
    audio_modulation_start =
        (uint32_t)audio_voice_modulation_start(audio, now);
    audio_voice_steals = audio_steals(audio);
    audio_skipped_notes = seq->skipped + audio_late_starts(audio);
    audio_overloads = seq->overloads;
    uint32_t elapsed = (uint32_t)(read_clock() - now);
    if (elapsed > audio_service_peak) audio_service_peak = elapsed;
    audio_services++;
}

void audio_platform_init(void)
{
    SpuInit();
    reverb_size = reverb_amount = -1;
    SpuSetCommonMasterVolume(0x3fff, 0x3fff);
    SPU_FM_MODE1 = SPU_FM_MODE2 = SPU_NOISE_MODE1 = SPU_NOISE_MODE2 = 0;
    SPU_REVERB_ON1 = SPU_REVERB_ON2 = 0;
    SPU_REVERB_VOL_L = SPU_REVERB_VOL_R = 0;
    SPU_CD_VOL_L = SPU_CD_VOL_R = SPU_EXT_VOL_L = SPU_EXT_VOL_R = 0;
    SpuSetTransferStartAddr(WAVE_SPU_ADDRESS);
    SpuWrite(wave_data, sizeof(wave_data));
    SpuIsTransferCompleted(SPU_TRANSFER_WAIT);
    for (int i = 0; i < AUDIO_HARDWARE_VOICES; i++) cached_volume[i] = -1;
    for (int i = 0; i < SEQUENCER_VOICES; i++)
    {
        cached_pitch[i] = -1;
        volume(NULL, i, 0, 0);
    }
    audio = audio_init(NULL, start_voice, volume, pitch, flush, ready,
                       audio_clock);
    // Timer 2 free-runs at CLK/8 (4,233,600 Hz). Timer 0 requests service every
    // 8,467 CPU clocks, approximately 0.25 ms, independently of VSync. Retain
    // the cadence used by the original sine backend; paired control cost and
    // the 1 ms dispatch deadline must be measured with the fixture. Read
    // elapsed hardware ticks, never count interrupts as elapsed time. The
    // 16-bit clock must be sampled within 15.48 ms; no callback, DMA wait or
    // score copy may mask interrupts for that long. Peaks expose violations
    // below that limit, but multiple missed wraps require external validation.
    EnterCriticalSection();
    TIMER_CTRL(2) = 0x0200;
    TIMER_VALUE(2) = 0;
    TIMER_CTRL(0) = 0;
    TIMER_RELOAD(0) = 8467;
    TIMER_VALUE(0) = 0;
    InterruptCallback(IRQ_TIMER0, service);
    TIMER_CTRL(0) =
        0x0058; // System clock, reset at target, repeating target IRQ.
    last_counter = (uint16_t)TIMER_VALUE(2);
    service_previous = clock_ticks;
    ExitCriticalSection();
}

void audio_platform_card_stop(void)
{
    EnterCriticalSection();
    if (enabled)
    {
        replacement_adopted(sequencer_stop(seq, read_clock()));
        enabled = 0;
    }
    pending_snapshot = -1;
    // The ordinary Stop deliberately runs a release ramp in later timer
    // services. A BIOS session has no such services, so silence both halves of
    // every pair and the wet return before detaching the dispatcher.
    for (int i = 0; i < SEQUENCER_VOICES; i++) volume(NULL, i, 0, 0);
    SPU_KEY_OFF1 = 0xffff;
    SPU_KEY_OFF2 = 0xff;
    SPU_REVERB_ON1 = SPU_REVERB_ON2 = 0;
    SPU_REVERB_VOL_L = SPU_REVERB_VOL_R = 0;
    SPU_CTRL &= ~0x80;
    audio = audio_init(NULL, start_voice, volume, pitch, flush, ready,
                       audio_clock);
    for (int i = 0; i < AUDIO_HARDWARE_VOICES; i++) cached_volume[i] = -1;
    TIMER_CTRL(0) = 0;
    IRQ_MASK &= ~(1u << IRQ_TIMER0);
    IRQ_STAT = (uint16_t)~(1u << IRQ_TIMER0);
    ExitCriticalSection();
}

void audio_platform_card_resume(const Score* score)
{
    // BIOS work may span arbitrarily many Timer 2 wraps. Discard that interval
    // instead of turning it into an overdue musical service.
    reverb_size = reverb_amount = -1;
    update_reverb(score);
    EnterCriticalSection();
    last_counter = (uint16_t)TIMER_VALUE(2);
    service_previous = clock_ticks;
    TIMER_RELOAD(0) = 8467;
    TIMER_VALUE(0) = 0;
    IRQ_STAT = (uint16_t)~(1u << IRQ_TIMER0);
    IRQ_MASK |= 1u << IRQ_TIMER0;
    TIMER_CTRL(0) = 0x0058;
    ExitCriticalSection();
}

void audio_platform_update(const Score* score, int connected, int start)
{
    if (replacement_state == REPLACE_IDLE) update_reverb(score);
    if (!connected || (start && enabled))
    {
        EnterCriticalSection();
        if (enabled)
        {
            replacement_adopted(sequencer_stop(seq, read_clock()));
            enabled = 0;
        }
        pending_snapshot = -1;
        ExitCriticalSection();
    }
    else if (start)
    {
        if (replacement_state != REPLACE_IDLE) return;
        // The timer remains live while the immutable snapshot is prepared.
        // Nothing in the interrupt can observe this copy until publication.
        snapshots[0] = *score;
        sequencer_start(seq, &snapshots[0], audio_sink(audio), 0);
        EnterCriticalSection();
        // Restart discards release tails. Zero their registers before reuse;
        // the regular stop path, in contrast, always completes its 5 ms ramp.
        for (int i = 0; i < SEQUENCER_VOICES; i++)
        {
            volume(NULL, i, 0, 0);
        }
        SPU_KEY_OFF1 = 0xffff;
        SPU_KEY_OFF2 = 0xff;
        // Let interrupt flush retire every old send, including unused pairs.
        // New starts remove their own stop bits through the normal allocator.
        audio_restart(audio);
        // Begin the timeline after restart preparation. Charging register
        // cleanup to the first dispatch can drop an otherwise timely chord when
        // all 16 runners begin together.
        AudioTime now = read_clock();
        for (int i = 0; i < seq->count; i++) seq->runners[i].next = now;
        audio_started = (uint32_t)now;
        audio_dispatch_peak = audio_note_count = audio_first_note =
            audio_last_note = 0;
        active_snapshot = 0;
        pending_snapshot = -1;
        published_revision = score->revision;
        enabled = 1;
        ExitCriticalSection();
    }
    else if (replacement_state == REPLACE_IDLE && enabled &&
             pending_snapshot < 0 && score->revision != published_revision)
    {
        // With no pending publication the active buffer cannot change during
        // this copy. Edits arriving while a buffer is pending are coalesced in
        // the editor score and copied on a later main-thread update.
        int spare = 1 - active_snapshot;
        snapshots[spare] = *score;
        EnterCriticalSection();
        pending_snapshot = spare;
        ExitCriticalSection();
    }
}

int audio_platform_playing(void)
{
    return enabled;
}

uint32_t audio_platform_revision(void)
{
    return published_revision;
}

AudioTime audio_platform_time(void)
{
    EnterCriticalSection();
    AudioTime now = read_clock();
    ExitCriticalSection();
    return now;
}

int audio_platform_replace(const Score* incoming)
{
    EnterCriticalSection();
    if (replacement_state != REPLACE_IDLE)
    {
        ExitCriticalSection();
        return 0;
    }
    replacement_state = REPLACE_PREPARING;
    pending_snapshot = -1;
    replacement_snapshot = 1 - active_snapshot;
    ExitCriticalSection();
    snapshots[replacement_snapshot] = *incoming;
    Sequencer* prepared = seq == &states[0] ? &states[1] : &states[0];
    sequencer_start(prepared, &snapshots[replacement_snapshot],
                    audio_sink(audio), 0);
    EnterCriticalSection();
    if (enabled)
    {
        sequencer_replace(seq, prepared, read_clock());
        replacement_state = REPLACE_WAITING;
    }
    else
    {
        prepared->playing = 0;
        replacement_adopted(prepared);
    }
    ExitCriticalSection();
    return 1;
}

int audio_platform_take_replacement(Score* score)
{
    if (replacement_state != REPLACE_ADOPTED) return 0;
    // Publication remains locked through the editor copy and effect update.
    // Same-size reverb keeps its delay memory; a size change wet-mutes and
    // clears only after adoption, with incoming dry notes already running.
    *score = snapshots[active_snapshot];
    update_reverb(score);
    EnterCriticalSection();
    replacement_state = REPLACE_IDLE;
    ExitCriticalSection();
    return 1;
}

int audio_platform_replacing(void)
{
    return replacement_state != REPLACE_IDLE;
}
