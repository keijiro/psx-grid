/*
 * audio.c - Logical voice envelopes and allocation
 *
 * Implementation notes:
 *
 * The same callback-driven state machine feeds hardware and host drivers.
 * It keeps envelope math in bounded integer ranges for timer service.
 */

#include "audio.h"

#include <string.h>

#include "audio_tables.h"

AudioTime audio_ms(int ms)
{
    return (uint32_t)ms * (SEQUENCER_HZ / 1000) + (uint32_t)ms * (SEQUENCER_HZ % 1000) / 1000;
}

/*
 * Returns the shift shared by a ramp's numerator and denominator so their
 * integer ratio stays bounded without changing the envelope endpoints.
 */
static unsigned gain_shift(uint32_t span)
{
    // Keep clock-tick precision for short envelopes. Longer full-scale ramps
    // need coarser steps to keep the product and release rounding in 32 bits.
    uint32_t limit = UINT32_MAX / (AUDIO_LEVEL + 1);
    if (span <= limit)
        return 0;
    if ((span >> 4) <= limit)
        return 4;
    if ((span >> 6) <= limit)
        return 6;
    return (span >> 8) <= limit ? 8 : 9;
}

/*
 * Preserves a nonzero release level until its deadline even after integer
 * rounding would otherwise silence the tail early.
 */
static int level_at(const AudioVoice* v, AudioTime now)
{
    if (!v->active)
        return 0;
    if (v->releasing) {
        if (now >= v->end)
            return 0;
        uint32_t span = (uint32_t)(v->end - v->release_at);
        unsigned shift = gain_shift(span);
        uint32_t left = (uint32_t)(v->end - now) >> shift;
        span >>= shift;
        int level =
            span ? (int)((left * (unsigned)v->release_level + span - 1) / span) : v->release_level;
        return level ? level : v->release_level ? 1 : 0;
    }
    uint32_t attack = (uint32_t)audio_ms(v->sound.attack);
    AudioTime elapsed = now - v->start;
    if (!attack || elapsed >= attack)
        return AUDIO_LEVEL;
    unsigned shift = gain_shift(attack);
    int level = (int)(((uint32_t)elapsed >> shift) * AUDIO_LEVEL / (attack >> shift));
    return level < AUDIO_LEVEL ? level : AUDIO_LEVEL - 1;
}

/*
 * Evaluates the signed pitch sweep in fixed point before the bank-specific
 * register lookup; modulation waits for observed key-on.
 */
static int pitch_at(const AudioVoice* v, AudioTime now)
{
    AudioTime elapsed = v->waiting ? 0 : now - v->modulation_start;
    uint32_t duration = (uint32_t)audio_ms(v->sound.decay);
    if (!v->sound.sweep || !duration || elapsed >= duration)
        return v->base_pitch;
    // Two radix-256 divisions normalize absolute time to Q16 without a
    // 64-bit divide. At 2000 ms, duration*256 still fits in uint32_t.
    uint32_t scaled = (uint32_t)elapsed * 256;
    uint32_t x = (scaled / duration) * 256 + (scaled % duration) * 256 / duration;
    unsigned index = x >> 6, fraction = x & 63;
    int snap =
        (int)audio_snap[index] - (int)((audio_snap[index] - audio_snap[index + 1]) * fraction / 64);
    int note = v->pitch * 65536 + v->sound.sweep * snap;
    // Clamp frequency after evaluating the signed interval. Extreme notes
    // can plateau at C0/C9, but never wrap a table or change banks mid-note.
    if (note < 0)
        note = 0;
    if (note > 108 * 65536)
        note = 108 * 65536;
    // Semitone entries store register values in Q8. Interpolate with a Q12
    // fraction; in the selected bank the product stays within 32 bits.
    const uint32_t* table = &audio_pitch[v->bank * 109];
    index = (unsigned)note >> 16;
    uint32_t value = table[index];
    if (index < 108)
        value += (table[index + 1] - value) * (((unsigned)note & 65535) >> 4) / 4096;
    return (int)((value + 128) >> 8);
}

/*
 * Crossfades the two wave halves against modulation time, which may begin
 * after the score event on hardware.
 */
static int mix_at(const AudioVoice* v, AudioTime now)
{
    AudioTime elapsed = v->waiting ? 0 : now - v->modulation_start;
    uint32_t attack = (uint32_t)audio_ms(v->sound.mix_attack),
             release = (uint32_t)audio_ms(v->sound.mix_release);
    if (attack && elapsed < attack) {
        unsigned shift = gain_shift(attack);
        return ((uint32_t)elapsed >> shift) * AUDIO_LEVEL / (attack >> shift);
    }
    if (release && elapsed < attack + release) {
        unsigned shift = gain_shift(release);
        return ((attack + release - (uint32_t)elapsed) >> shift) * AUDIO_LEVEL / (release >> shift);
    }
    return 0;
}

/*
 * Writes active pair controls before flushing key changes and marks completed
 * release tails idle for later allocation.
 */
static void advance(void* ctx, AudioTime now)
{
    Audio* a = ctx;
    if (a->idle_mask == AUDIO_IDLE_MASK && !(a->starts | a->stops))
        return;
    for (int i = 0; i < SEQUENCER_VOICES; i++) {
        AudioVoice* v = &a->voices[i];
        if (!v->active)
            continue;
        // Redux can defer key-on until its mixer runs. Hold the initial mix
        // and pitch through the first observed attack so a brief Wave B
        // transient cannot expire before playback. Keep score/gate timing
        // separate: waiting must never postpone a release or a stop ramp.
        if (v->waiting && !(a->starts & (1u << i)) && a->driver.ready(a->driver.context, i)) {
            v->waiting = 0;
            v->modulation_start = now;
        }
        v->level = level_at(v, now);
        if (a->starts & (1u << i))
            a->driver.start(a->driver.context, i, v->bank, v->sound);
        int b = v->level * mix_at(v, now) / AUDIO_LEVEL;
        a->driver.volume(a->driver.context, i, v->level - b, b);
        a->driver.pitch(a->driver.context, i, pitch_at(v, now));
        if (v->releasing && now >= v->end) {
            v->active = 0;
            a->idle_mask |= 1u << i;
            a->stops |= 1u << i;
            a->starts &= ~(1u << i);
        }
    }
    a->driver.flush(a->driver.context, a->starts, a->stops);
    a->starts = a->stops = 0;
}

/*
 * Prefers an idle pair, then the quietest release tail, then the oldest voice.
 * The returned generation prevents stale gate-offs after reuse.
 */
static uint32_t on(void* ctx, AudioTime now, int pitch, SoundSettings sound)
{
    Audio* a = ctx;
    int slot = 0;
    // Reclaim tails once per event time, not once per note in a chord. The
    // idle mask keeps the common allocation path independent of polyphony.
    if (a->allocation_time != now) {
        a->allocation_time = now;
        for (int i = 0; i < SEQUENCER_VOICES; i++) {
            AudioVoice* v = &a->voices[i];
            if (v->active && v->releasing && now >= v->end) {
                v->active = 0;
                a->idle_mask |= 1u << i;
                a->stops |= 1u << i;
                a->starts &= ~(1u << i);
                a->driver.volume(a->driver.context, i, 0, 0);
            }
        }
    }
    if (a->idle_mask) {
        uint32_t mask = a->idle_mask;
        if (!(mask & 0xffff)) {
            slot += 16;
            mask >>= 16;
        }
        if (!(mask & 0xff)) {
            slot += 8;
            mask >>= 8;
        }
        if (!(mask & 0xf)) {
            slot += 4;
            mask >>= 4;
        }
        if (!(mask & 3)) {
            slot += 2;
            mask >>= 2;
        }
        if (!(mask & 1))
            slot++;
        a->idle_mask &= ~(1u << slot);
    } else {
        int quiet = -1, quiet_level = AUDIO_LEVEL + 1;
        for (int i = 0; i < SEQUENCER_VOICES; i++) {
            AudioVoice* v = &a->voices[i];
            if (v->releasing) {
                int level = level_at(v, now);
                if (level < quiet_level) {
                    quiet = i;
                    quiet_level = level;
                }
            }
            if (v->start < a->voices[slot].start)
                slot = i;
        }
        if (quiet >= 0)
            slot = quiet;
        a->steals++;
    }
    AudioVoice* v = &a->voices[slot];
    uint32_t generation = (v->generation + 1) & 0x07ffffffu;
    if (!generation)
        generation = 1;
    v->start = v->modulation_start = now;
    v->generation = generation;
    v->sound = sound;
    v->waiting = a->driver.ready != NULL;
    v->active = 1;
    v->releasing = 0;
    v->level = 0;
    v->pitch = pitch;
    v->bank = audio_banks[pitch * 49 + (sound.decay ? sound.sweep : 0) + 24];
    v->base_pitch = (int)((audio_pitch[v->bank * 109 + pitch] + 128) >> 8);
    // Batch one final key-on per slot. A stolen slot must not receive key-off
    // in the same batch, whose register priority would suppress its new note.
    a->stops &= ~(1u << slot);
    a->starts |= 1u << slot;
    return generation << 5 | (uint32_t)slot;
}

/*
 * Captures the current envelope level so a release begins continuously,
 * including during an unfinished attack.
 */
static void release(Audio* a, int i, AudioTime now, int ms)
{
    AudioVoice* v = &a->voices[i];
    v->release_level = level_at(v, now);
    v->release_at = now;
    v->end = now + audio_ms(ms);
    v->releasing = 1;
}

/*
 * Accepts only the generation currently occupying the token slot; an old
 * sequencer gate cannot stop a stolen voice.
 */
static void off(void* ctx, AudioTime now, uint32_t token)
{
    Audio* a = ctx;
    unsigned i = token & 31;
    if (i >= SEQUENCER_VOICES)
        return;
    AudioVoice* v = &a->voices[i];
    if (v->active && !v->releasing && v->generation == (token >> 5)) {
        release(a, (int)i, now, v->sound.release);
        if (!v->sound.release) {
            v->active = 0;
            a->idle_mask |= 1u << i;
            a->stops |= 1u << i;
            a->starts &= ~(1u << i);
            a->driver.volume(a->driver.context, (int)i, 0, 0);
        }
    }
}

static void stop(void* ctx, AudioTime now)
{
    Audio* a = ctx;
    for (int i = 0; i < SEQUENCER_VOICES; i++)
        if (a->voices[i].active)
            release(a, i, now, 5);
}

void audio_init(Audio* a, AudioDriver driver)
{
    memset(a, 0, sizeof(*a));
    a->driver = driver;
    a->idle_mask = AUDIO_IDLE_MASK;
    a->allocation_time = UINT64_MAX;
}

NoteSink audio_sink(Audio* a)
{
    return (NoteSink){a, on, off, stop, advance};
}

uint32_t audio_reverb_mask(const Audio* a)
{
    uint32_t mask = 0;
    for (int i = 0; i < SEQUENCER_VOICES; i++)
        if (a->voices[i].active && a->voices[i].sound.reverb)
            mask |= 3u << (i * 2);
    return mask;
}
