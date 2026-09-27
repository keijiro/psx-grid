/*
 * audio_platform.h - PlayStation audio transport and publication
 *
 * Rust owns score snapshots and transport state. C supplies timer and SPU
 * services. Lifecycle and live publication run on the main thread.
 */

#ifndef AUDIO_PLATFORM_H
#define AUDIO_PLATFORM_H

#include "audio_sequencer.h"

// One runner's audible location in the published score.
typedef struct
{
    int lane;
    int step;
} AudioPlayhead;

// A frame-sized copy of the timer-owned positions.
typedef struct
{
    uint32_t revision;
    int count;
    AudioPlayhead items[SCORE_LANES];
} AudioPlayheads;

/*
 * Initializes the SPU and timer-backed transport before pad initialization.
 */
void audio_platform_init(void);

/*
 * Returns the absolute sequencer clock sampled from the platform timer.
 */
AudioTime audio_platform_time(void);

/*
 * Prepares the current `score` and services transport controls. `connected`
 * reports pad presence; `start` is a button edge that toggles playback.
 * Call regularly even without input so edits coalesced behind a pending
 * snapshot are eventually planned. `score` must not be NULL and must remain
 * valid for this call.
 */
void audio_platform_update(const Score* score, int connected, int start);

/*
 * Plans enough future events to cover the next frame and ordinary main-loop
 * stalls. Call after model updates and before the frame wait.
 */
void audio_platform_fill(void);

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
 * Copies audible runner positions and their score revision while timer
 * service is excluded. Returns an empty list when playback is stopped.
 * `out` must not be NULL.
 */
void audio_platform_playheads(AudioPlayheads* out);

/*
 * Copies `incoming` into a spare snapshot and arms a handoff. Returns one on
 * acceptance or zero without changing the pending replacement if another
 * replacement is in progress. `incoming` must not be NULL and may be reused
 * after the call.
 */
int audio_platform_replace(const Score* incoming);

/*
 * Copies a replacement into caller-owned `score` once its lap seam reaches
 * timer dispatch. Returns one after copying, or zero and leaves `score`
 * unchanged until that handoff.
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

#endif // AUDIO_PLATFORM_H
