/*
 * audio.h - Logical voice synthesis and PlayStation audio transport
 *
 * A logical voice uses a pair of SPU voices. The same Audio state machine
 * drives either the hardware driver or a test driver supplied by the caller.
 */

#ifndef AUDIO_H
#define AUDIO_H

#include "sequencer.h"

// The SPU has 24 channels; each logical slot reserves two of them.
#define AUDIO_HARDWARE_VOICES 24
// One bit per logical slot, set while that slot is available for allocation.
#define AUDIO_IDLE_MASK ((1u << SEQUENCER_VOICES) - 1)
// Direct SPU voice volume tops out at 0x3fff. Complementary A/B gains keep
// equal-wave pairs within one voice budget; overlapping pairs can clip.
#define AUDIO_LEVEL 0x3fff

/*
 * Driver slots and flush masks refer to logical pairs, not SPU channels.
 * Register writes are injected so allocation, envelopes and stale gate-offs
 * use exactly the same code on the console and in the host fixture. flush
 * commits pending key changes; ready may be NULL if start is synchronous.
 */
typedef struct {
    void* context;
    void (*start)(void* context, int slot, int bank, SoundSettings sound);
    void (*volume)(void* context, int slot, int a, int b);
    void (*pitch)(void* context, int slot, int value);
    void (*flush)(void* context, uint32_t starts, uint32_t stops);
    // Optional: both voices have begun playback after the latest key-on.
    int (*ready)(void* context, int slot);
} AudioDriver;

// Envelope and modulation state of one allocated logical voice.
typedef struct {
    AudioTime start, release_at, end, modulation_start;
    uint32_t generation;
    SoundSettings sound;
    int active, releasing, waiting, level, release_level, pitch, bank, base_pitch;
} AudioVoice;

// Caller-owned synthesizer with fixed storage for every logical voice.
typedef struct {
    AudioVoice voices[SEQUENCER_VOICES];
    AudioDriver driver;
    uint32_t starts, stops, steals, idle_mask;
    AudioTime allocation_time;
} Audio;

/*
 * Converts a nonnegative millisecond duration to sequencer clock ticks.
 * Durations must fit the supported sound-setting range.
 */
AudioTime audio_ms(int ms);
/* Initializes `audio` with a complete driver callback table. */
void audio_init(Audio* audio, AudioDriver driver);
/*
 * Returns callbacks referencing `audio`. The caller keeps it alive and
 * excludes concurrent access for as long as the sink may be called.
 */
NoteSink audio_sink(Audio* audio);
/* Returns both hardware-channel bits for each active slot using reverb. */
uint32_t audio_reverb_mask(const Audio* audio);
// Platform lifecycle and live publication. Call update regularly on the main
// thread, even without input, to publish edits coalesced behind a pending score.
/* Initializes the SPU and timer-backed transport before pad initialization. */
void audio_platform_init(void);
/* Returns the absolute sequencer clock sampled from the platform timer. */
AudioTime audio_platform_time(void);
/* Publishes the current score and services transport controls on the main thread. */
void audio_platform_update(const Score* score, int connected, int start);
/* Suspends audio callbacks before BIOS memory-card ownership begins. */
void audio_platform_card_stop(void);
/* Restores audio callbacks after card ownership ends, using the current score. */
void audio_platform_card_resume(const Score* score);
/* Reports whether transport is running. */
int audio_platform_playing(void);
/* Copies `incoming` into a spare snapshot and arms a handoff; returns success. */
int audio_platform_replace(const Score* incoming);
/* Copies an adopted replacement into `score` once; returns whether one was ready. */
int audio_platform_take_replacement(Score* score);
/* Reports whether replacement preparation or adoption is still pending. */
int audio_platform_replacing(void);
/* Returns the score revision currently published to the audio service. */
uint32_t audio_platform_revision(void);

#endif // AUDIO_H
