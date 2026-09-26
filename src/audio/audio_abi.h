/*
 * audio_abi.h - C note-sink adapter for the Rust voice synthesizer
 *
 * The adapter passes sequencer note events into Rust through pointer-based
 * callbacks. Its sink context remains owned by the caller.
 */

#ifndef AUDIO_ABI_H
#define AUDIO_ABI_H

#include "audio_synth.h"

/*
 * Returns callbacks referencing `audio`, which must not be NULL. The caller
 * keeps it alive and excludes concurrent access while the sink may be called.
 */
NoteSink audio_sink(Audio* audio);

#endif // AUDIO_ABI_H
