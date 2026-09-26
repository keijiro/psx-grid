/*
 * sequencer.h - Clocked traversal of score lanes into note events
 *
 * A caller-owned Sequencer traverses an immutable Score snapshot into note
 * events through a supplied sink. It uses fixed storage for pending note-offs
 * and must be serviced with serialized access. A replacement is adopted only
 * at a complete slice edge.
 */

#ifndef SEQUENCER_H
#define SEQUENCER_H

#include "score/score.h"

#include <stdint.h>

// CLK/8 gives an exact integral duration for every division and twentieth-step
// gate at 120 BPM. A 64-bit absolute clock avoids both rounding drift and wrap.
#define SEQUENCER_HZ 4233600u
// Logical notes; each owns two of the 24 hardware voices.
#define SEQUENCER_VOICES 12
// Bounded work per service call protects the timer callback deadline.
#define SEQUENCER_BUDGET 2
#define SEQUENCER_TILE_BUDGET 32
// Absolute tick count; uint64_t keeps transport time monotonic across wraps.
typedef uint64_t AudioTime;

/*
 * Sink callbacks consume event times in sequencer ticks; context remains live.
 * on returns zero to drop a note, or a generation token whose low five bits
 * identify a logical slot in 0..11. off receives that same token unchanged.
 */
typedef struct
{
    void* context;
    uint32_t (*on)(void*, AudioTime, int, SoundSettings);
    void (*off)(void*, AudioTime, uint32_t);
    void (*stop)(void*, AudioTime);
    void (*advance)(void*, AudioTime);
} NoteSink;

// Cursor and lock state for a lane that began at `origin`.
typedef struct
{
    int origin;
    int lane;
    int step;
    int playing_lane;
    int playing_step;
    uint32_t lap;
    uint32_t duration;
    int active;
    AudioTime next;
    // Geometry limits a stack to 64 tiles. A reached lock reads its current
    // value until the next step, but never a replacement born in its slot.
    TileId held[SCORE_HEIGHT];
    uint32_t held_generation[SCORE_HEIGHT];
    int held_count;
} Runner;

// Scheduled token release at an absolute clock tick.
typedef struct
{
    AudioTime at;
    uint32_t token;
} NoteOff;

// Caller-owned transport state, including prepared replacement ownership.
typedef struct Sequencer
{
    const Score* score;
    NoteSink sink;
    Runner runners[SCORE_LANES];
    // Runner storage stays put across edits; only the traversal order changes.
    int order[SCORE_LANES];
    NoteOff offs[SEQUENCER_VOICES];
    int count;
    int playing;
    int master;
    struct Sequencer* replacement;
    AudioTime replacement_at;
    uint32_t random;
    uint32_t skipped;
    uint32_t overloads;
    AudioTime slice_at;
    SoundSettings working[SCORE_CHANNELS];
    int slicing;
    int runner_index;
    int visiting;
    int held_index;
    int jump;
    TileId cursor;
} Sequencer;

/*
 * Starts traversal of `snapshot` at `now` using `sink`. The caller keeps the
 * snapshot alive and immutable, and keeps the sink context alive with access
 * serialized, while this state can be serviced.
 * `seq` and `snapshot` must not be NULL.
 */
void sequencer_start(Sequencer* seq, const Score* snapshot, NoteSink sink,
                     AudioTime now);
/*
 * Returns success if the transport can adopt `snapshot` at `now`. Keep the
 * new score alive and immutable while it is active. Resync is accepted only
 * between complete slices; failure leaves the old score in use.
 * `seq` and `snapshot` must not be NULL.
 */
int sequencer_resync(Sequencer* seq, const Score* snapshot, AudioTime now);
/*
 * Arms `prepared` for a full replacement; returns whether accepted. Prepare
 * it with sequencer_start on the main thread, then arm while service is
 * excluded. Do not change either state or score while the handoff is pending;
 * keep the adopted score alive while it is active. Retain every service/stop
 * return value to identify the state that owns playback.
 * `seq` and `prepared` must not be NULL.
 */
int sequencer_replace(Sequencer* seq, Sequencer* prepared, AudioTime now);
/*
 * Stops notes in `seq` at `now` and returns the state owning playback after
 * any pending handoff. Keep the returned state for later service calls.
 * `seq` must not be NULL.
 */
Sequencer* sequencer_stop(Sequencer* seq, AudioTime now);
/*
 * Processes slices due by `now` in `seq` and returns the state now owning
 * playback. Keep the returned state for the next service call.
 * `seq` must not be NULL.
 */
Sequencer* sequencer_service(Sequencer* seq, AudioTime now);

#endif // SEQUENCER_H
