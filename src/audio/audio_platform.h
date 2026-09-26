/*
 * audio_platform.h - PlayStation audio transport and publication
 *
 * The platform owns timer service, SPU setup, and score snapshots. Lifecycle
 * and live publication run on the main thread.
 */

#ifndef AUDIO_PLATFORM_H
#define AUDIO_PLATFORM_H

#include "audio/sequencer.h"

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

#endif // AUDIO_PLATFORM_H
