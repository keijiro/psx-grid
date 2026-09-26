/*
 * audio_synth.h - Rust-owned logical voice synthesis
 *
 * A Rust-owned Audio synthesizer turns sequencer note events and sound
 * settings into controls for paired SPU voices. The same state machine drives
 * hardware or a caller-supplied test driver. It uses fixed storage and makes
 * no allocations. Calls must be serialized with the sequencer service.
 */

#ifndef AUDIO_SYNTH_H
#define AUDIO_SYNTH_H

#include "audio/sequencer.h"

#include <stddef.h>
#include <stdint.h>

// The SPU has 24 channels; each logical slot reserves two of them.
#define AUDIO_HARDWARE_VOICES 24
// One bit per logical slot, set while that slot is available for allocation.
#define AUDIO_IDLE_MASK ((1u << SEQUENCER_VOICES) - 1)
// Direct SPU voice volume tops out at 0x3fff. Complementary A/B gains keep
// equal-wave pairs within one voice budget; overlapping pairs can clip.
#define AUDIO_LEVEL 0x3fff

// The single synthesizer lives in Rust fixed storage.
typedef struct Audio Audio;

/*
 * Allocates a note from settings that remain live for this call. `context`
 * must point to the active Audio, and calls must be serialized.
 */
uint32_t rust_audio_on(void* context, AudioTime now, int pitch,
                       const SoundSettings* sound);

/*
 * Releases a note by its generation token. `context` must point to the active
 * Audio, and calls must be serialized.
 */
void rust_audio_off(void* context, AudioTime now, uint32_t token);

/*
 * Releases all active notes. `context` must point to the active Audio, and
 * calls must be serialized.
 */
void rust_audio_stop(void* context, AudioTime now);

/*
 * Advances voice controls. `context` must point to the active Audio, and
 * calls must be serialized.
 */
void rust_audio_advance(void* context, AudioTime now);

/*
 * Returns callbacks referencing `audio`, which must not be NULL. The caller
 * keeps it alive and excludes concurrent access while the sink may be called.
 */
static inline NoteSink audio_sink(Audio* audio)
{
    return (NoteSink){audio, rust_audio_on, rust_audio_off, rust_audio_stop,
                      rust_audio_advance};
}

// A copied voice view exposes timing and envelope state to diagnostics.
typedef struct
{
    AudioTime start;
    AudioTime end;
    AudioTime modulation_start;
    SoundSettings sound;
    int active;
    int releasing;
    int waiting;
    int level;
    int pitch;
    int base_pitch;
} AudioVoiceView;

/*
 * Converts a nonnegative millisecond duration to sequencer clock ticks.
 * `ms` must fit the supported sound-setting range (0..SOUND_MAX_MS).
 */
AudioTime audio_ms(int ms);

/*
 * Initializes the single fixed synthesizer and returns its stable pointer.
 * Slots and flush masks refer to logical pairs. `clock` is optional and
 * enables the one-millisecond dispatch deadline on hardware. `ready` may be
 * NULL when start begins playback synchronously. All other callbacks must
 * be non-NULL. Call only while the timer service is excluded.
 */
Audio* audio_init(void* context,
                  void (*start)(void*, int, int, const SoundSettings*),
                  void (*volume)(void*, int, int, int),
                  void (*pitch)(void*, int, int),
                  void (*flush)(void*, uint32_t, uint32_t, uint32_t, uint32_t),
                  int (*ready)(void*, int), AudioTime (*clock)(void*));

/*
 * Returns the fixed Rust synthesizer's storage size in bytes.
 */
size_t audio_storage_size(void);

/*
 * Discards release tails before a transport restart and schedules key-off
 * for every slot. `audio` must not be NULL and service must be excluded.
 */
void audio_restart(Audio* audio);

/*
 * Copies one voice's diagnostic state. `audio` and `view` must not be NULL;
 * `slot` must be in 0..SEQUENCER_VOICES-1.
 */
void audio_voice_view(const Audio* audio, int slot, AudioVoiceView* view);

/*
 * Returns the first slot's effective modulation start for timer diagnostics.
 * A pending key-on reports `now`. `audio` must not be NULL.
 */
AudioTime audio_voice_modulation_start(const Audio* audio, AudioTime now);

/*
 * Returns the number of stolen logical slots since initialization.
 * `audio` must not be NULL.
 */
uint32_t audio_steals(const Audio* audio);

/*
 * Returns the number of starts rejected after the dispatch deadline.
 * `audio` must not be NULL.
 */
uint32_t audio_late_starts(const Audio* audio);

/*
 * Returns the current free-slot mask. `audio` must not be NULL.
 */
uint32_t audio_idle_mask(const Audio* audio);

/*
 * Returns both hardware-channel bits for each active reverb slot in `audio`.
 * `audio` must not be NULL.
 */
uint32_t audio_reverb_mask(const Audio* audio);

#endif // AUDIO_SYNTH_H
