#ifndef SEQUENCER_H
#define SEQUENCER_H
#include "score.h"
// CLK/8 gives an exact integral duration for every division and twentieth-step
// gate at 120 BPM. A 64-bit absolute clock avoids both rounding drift and wrap.
#define SEQUENCER_HZ 4233600u
// Logical notes; each owns two of the 24 hardware voices.
#define SEQUENCER_VOICES 12
#define SEQUENCER_BUDGET 2
#define SEQUENCER_TILE_BUDGET 32
typedef uint64_t AudioTime;
// on returns zero to drop a note, or a generation token whose low five bits
// identify a logical slot in 0..11. off receives that same token unchanged.
typedef struct {
    void *context;
    uint32_t (*on)(void *, AudioTime, int, SoundSettings);
    void (*off)(void *, AudioTime, uint32_t);
    void (*stop)(void *, AudioTime);
    void (*advance)(void *, AudioTime);
} NoteSink;
typedef struct {
    int origin, lane, step, playing_lane, playing_step;
    uint32_t lap, duration;
    int active;
    AudioTime next;
    // Geometry limits a stack to 64 tiles. A reached lock reads its current
    // value until the next step, but never a replacement born in its slot.
    TileId held[SCORE_HEIGHT];
    uint32_t held_generation[SCORE_HEIGHT];
    int held_count;
} Runner;
typedef struct { AudioTime at; uint32_t token; } NoteOff;
typedef struct {
    const Score *score;
    NoteSink sink;
    Runner runners[SCORE_LANES];
    // Runner storage stays put across edits; only the traversal order changes.
    int order[SCORE_LANES];
    NoteOff offs[SEQUENCER_VOICES];
    int count, playing;
    uint32_t random, skipped, overloads;
    AudioTime slice_at;
    SoundSettings working;
    int slicing, runner_index, visiting, held_index, jump;
    TileId cursor;
} Sequencer;
void sequencer_start(Sequencer *seq, const Score *snapshot, NoteSink sink, AudioTime now);
// Both scores must remain immutable through this call. Resync is accepted
// only between complete slices; failure leaves the old score in use.
int sequencer_resync(Sequencer *seq, const Score *snapshot, AudioTime now);
void sequencer_stop(Sequencer *seq, AudioTime now);
void sequencer_service(Sequencer *seq, AudioTime now);
#endif
