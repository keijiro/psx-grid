/*
 * audio.h - Logical voice synthesis and PlayStation audio transport
 *
 * A caller-owned Audio synthesizer turns sequencer note events and sound
 * settings into controls for paired SPU voices. The same state machine drives
 * hardware or a caller-supplied test driver. It uses fixed storage and makes
 * no allocations. Sink calls must be serialized; platform lifecycle and
 * publication run on the main thread.
 */

#ifndef AUDIO_H
#define AUDIO_H

#include "sequencer.h"

#include <stdint.h>

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
typedef struct
{
    void* context;
    void (*start)(void* context, int slot, int bank, SoundSettings sound);
    void (*volume)(void* context, int slot, int a, int b);
    void (*pitch)(void* context, int slot, int value);
    void (*flush)(void* context, uint32_t starts, uint32_t stops);
    // Optional: both voices have begun playback after the latest key-on.
    int (*ready)(void* context, int slot);
} AudioDriver;

// Envelope and modulation state of one allocated logical voice.
typedef struct
{
    AudioTime start;
    AudioTime release_at;
    AudioTime end;
    AudioTime modulation_start;
    uint32_t generation;
    SoundSettings sound;
    int active;
    int releasing;
    int waiting;
    int level;
    int release_level;
    int pitch;
    int bank;
    int base_pitch;
} AudioVoice;

// Caller-owned synthesizer with fixed storage for every logical voice.
typedef struct
{
    AudioVoice voices[SEQUENCER_VOICES];
    AudioDriver driver;
    uint32_t starts;
    uint32_t stops;
    uint32_t steals;
    uint32_t idle_mask;
    AudioTime allocation_time;
} Audio;

/*
 * Converts a nonnegative millisecond duration to sequencer clock ticks.
 * `ms` must fit the supported sound-setting range (0..SOUND_MAX_MS).
 */
AudioTime audio_ms(int ms);

/*
 * Initializes caller-owned `audio` with no active voices. `audio` must not be
 * NULL. All `driver` callbacks except ready must be non-NULL; ready may be
 * NULL when start begins playback synchronously.
 */
void audio_init(Audio* audio, AudioDriver driver);

/*
 * Returns callbacks referencing `audio`, which must not be NULL. The caller
 * keeps it alive and excludes concurrent access while the sink may be called.
 */
NoteSink audio_sink(Audio* audio);

/*
 * Returns both hardware-channel bits for each active reverb slot in `audio`.
 * `audio` must not be NULL.
 */
uint32_t audio_reverb_mask(const Audio* audio);

// Platform lifecycle and live publication run on the main thread.
/*
 * Initializes the SPU and timer-backed transport before pad initialization.
 */
void audio_platform_init(void);

/*
 * Returns the absolute sequencer clock sampled from the platform timer.
 */
AudioTime audio_platform_time(void);

/*
 * Publishes the current `score` and services transport controls. `connected`
 * reports pad presence; `start` is a button edge that toggles playback.
 * Call regularly even without input so edits coalesced behind a pending
 * snapshot are eventually published. `score` must not be NULL and must remain
 * valid for this call.
 */
void audio_platform_update(const Score* score, int connected, int start);

/*
 * Suspends audio callbacks before BIOS memory-card ownership begins.
 */
void audio_platform_card_stop(void);

/*
 * Restores audio callbacks after card ownership ends using the current
 * `score`, which must not be NULL and must remain valid for this call.
 */
void audio_platform_card_resume(const Score* score);

/*
 * Returns one while transport is running, or zero while it is stopped.
 */
int audio_platform_playing(void);

/*
 * Copies `incoming` into a spare snapshot and arms a handoff. Returns one on
 * acceptance or zero without changing the pending replacement if another
 * replacement is in progress. `incoming` must not be NULL and may be reused
 * after the call.
 */
int audio_platform_replace(const Score* incoming);

/*
 * Copies an adopted replacement into caller-owned `score` once. Returns one
 * after copying, or zero and leaves `score` unchanged until adoption.
 * `score` must not be NULL.
 */
int audio_platform_take_replacement(Score* score);

/*
 * Returns one from replacement preparation until the adopted score has been
 * copied to the caller, or zero otherwise.
 */
int audio_platform_replacing(void);

/*
 * Returns the score revision currently published to the audio service.
 */
uint32_t audio_platform_revision(void);

#endif // AUDIO_H
