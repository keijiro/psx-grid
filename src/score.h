/*
 * score.h - Editable grid score and validated model operations
 *
 * A Score owns lanes, tiles, sounds and stable birth generations. Mutating
 * operations report a ScoreResult and leave the score unchanged on failure.
 */

#ifndef SCORE_H
#define SCORE_H

#include <stdint.h>

// Fixed model limits; geometry and tile pool storage have no dynamic allocation.
#define SCORE_WIDTH 128
#define SCORE_HEIGHT 64
#define SCORE_LANES 16
#define SCORE_CHANNELS 8
#define SCORE_STEPS 64
#define SCORE_INITIAL_LENGTH 16
#define SCORE_TILE_CAPACITY 4096
#define SCORE_DIVISIONS 12

// Tile behavior interpreted by the sequencer at a lane step.
typedef enum
{
    TILE_NONE,
    TILE_NOTE,
    TILE_CYCLE,
    TILE_PROBABILITY,
    TILE_JUMP,
    TILE_RELATIVE,
    TILE_KIND_COUNT
} TileKind;

// Zero is the empty link. Live IDs stay stable across movement and reordering;
// deletion releases an ID for reuse by a later placement.
typedef uint16_t TileId;
// Maximum envelope duration and lock offset magnitude in milliseconds.
#define SOUND_MAX_MS 16000

// Independent override bits for relative envelope locks.
enum
{
    LOCK_ATTACK = 1,
    LOCK_RELEASE = 2
};

// Maximum mix ramp, pitch sweep duration and semitone sweep depth.
#define SOUND_MAX_MIX_MS 500
#define SOUND_MAX_DECAY_MS 2000
#define SOUND_MAX_SWEEP 24

// Wave bank choices available to each half of a logical voice.
typedef enum
{
    WAVE_SINE,
    WAVE_TRIANGLE,
    WAVE_SAW,
    WAVE_SQUARE,
    WAVE_NOISE,
    WAVE_COUNT
} Waveform;

// Channel sound parameters; time fields use milliseconds.
typedef struct
{
    int attack, release;
    int wave_a, wave_b, mix_attack, mix_release, sweep, decay, reverb;
} SoundSettings;

// New channels start with this dry dual-sine sound.
#define SOUND_DEFAULT ((SoundSettings){5, 5, WAVE_SINE, WAVE_SINE, 120, 280, 0, 200, 0})
// User-editable tempo bounds in beats per minute.
#define SCORE_MIN_BPM 30
#define SCORE_MAX_BPM 300

// Shared reverb preset index and wet-return percentage.
typedef struct
{
    int size, amount;
} ReverbSettings;

// Medium room with a 30% wet return for newly initialized scores.
#define REVERB_DEFAULT ((ReverbSettings){1, 30})
/* Returns a display label, or "?" for an invalid preset index. */
const char* score_reverb_size(int size);
/* Returns a display label, or "?" for an invalid waveform. */
const char* score_wave_name(int wave);

// Per-tile playback values; fields not used by `kind` keep default values.
// Pitch counts semitones from C0. Length counts twentieths of a step, so all
// supported 0.05-step edits remain exact without floating point.
typedef struct
{
    TileKind kind;
    int pitch, length, period, chance;
    uint32_t pattern;
    int lock_mask, attack, release;
} TileValue;

// Pool tile with an optional stack successor or jump branch.
typedef struct
{
    TileValue value;
    TileId next;
    int branch;
} Tile;

// Only regular lanes own a channel index; branches resolve it through source.
typedef struct
{
    int active, x, y, length, division, channel;
    TileId source, tiles[SCORE_STEPS];
} Lane;

// Fixed-pool model; `revision` changes after each accepted edit.
typedef struct
{
    Lane lanes[SCORE_LANES];
    Tile tiles[SCORE_TILE_CAPACITY + 1];
    SoundSettings sounds[SCORE_CHANNELS];
    int bpm;
    ReverbSettings reverb;
    uint32_t revision;
    // Pool positions survive moves but can be reused after deletion. Birth
    // generations distinguish those objects across skipped playback revisions.
    // Allocation stops at UINT32_MAX rather than aliasing an earlier birth.
    uint32_t generation, lane_generation[SCORE_LANES], tile_generation[SCORE_TILE_CAPACITY + 1];
} Score;

// What a coordinate addresses before or after insertion-point resolution.
typedef enum
{
    CELL_EMPTY,
    CELL_HEAD,
    CELL_STEP,
    CELL_TILE,
    CELL_END
} CellKind;

// Located cell and its lane, step, stack depth and optional tile ID.
typedef struct
{
    CellKind kind;
    int lane, step, depth;
    TileId tile;
} Cell;

// Edit outcomes; nonzero results leave the original score untouched.
typedef enum
{
    SCORE_OK,
    SCORE_BOUNDS,
    SCORE_COLLISION,
    SCORE_FULL,
    SCORE_TILES,
    SCORE_INVALID,
    SCORE_CYCLE
} ScoreResult;

// Copied non-jump tile values, independent of source pool IDs.
typedef struct
{
    int count;
    TileValue values[SCORE_HEIGHT];
} Clipboard;

// Move coordinates and preview result; apply revalidates current score state.
typedef struct
{
    int sx, sy, x, y;
    ScoreResult result;
} MovePlan;

// Supported step divisions in ascending order.
extern const int score_divisions[SCORE_DIVISIONS];
/*
 * Model calls require non-NULL pointers and caller-owned, serialized access.
 * Coordinates and lane/channel indices are zero-based. ScoreResult edits
 * leave `score` unchanged on failure unless a function says otherwise.
 */
/* Initializes a caller-owned empty score with default sounds and tempo. */
void score_init(Score* score);
/* Validates links, tile values and geometry before accepting an imported score. */
ScoreResult score_validate_import(const Score* score);
/* Sets a tempo in SCORE_MIN_BPM..SCORE_MAX_BPM. */
ScoreResult score_set_bpm(Score* score, int bpm);
/* Sets a preset 0..2 and wet amount 0..100. */
ScoreResult score_set_reverb(Score* score, ReverbSettings reverb);
/* Sets one 0-based channel after validating all sound parameters. */
ScoreResult score_set_sound(Score* score, int channel, SoundSettings sound);
/* Returns the exact cell at a grid coordinate, or CELL_EMPTY. */
Cell score_at(const Score* score, int x, int y);
/* Resolves an empty coordinate just below a stack as an insertion step. */
Cell score_resolve(const Score* score, int x, int y);
/* Returns default values for a tile kind; the caller supplies a valid kind. */
TileValue score_default(TileKind kind);
/* Edits a live tile by ID; its kind must remain unchanged. */
ScoreResult score_edit(Score* score, TileId id, TileValue value);
/* Returns the inherited channel of a live lane, or -1 if unresolved. */
int score_channel(const Score* score, int lane);
/* Sets a root lane's 0-based channel; branches inherit their source channel. */
ScoreResult score_set_channel(Score* score, int lane, int channel);
/* Returns the inherited division of a live lane, or 16 if unresolved. */
int score_division(const Score* score, int lane);
/* Sets a root lane's division to one of score_divisions. */
ScoreResult score_set_division(Score* score, int lane, int division);
/* Previews creating a root lane of `length` steps at `(x, y)`. */
ScoreResult score_can_create(const Score* score, int x, int y, int length);
/* Creates a root lane when the same geometry and file limits are satisfied. */
ScoreResult score_create(Score* score, int x, int y, int length);
/* Previews a lane resize; trailing occupied steps cannot be removed. */
ScoreResult score_can_resize(const Score* score, int lane, int length);
/* Resizes a live lane after the preview checks pass. */
ScoreResult score_resize(Score* score, int lane, int length);
/* Deletes a live lane and its descendants through jump tiles. */
ScoreResult score_delete(Score* score, int lane);
/* Places default values for `kind` at a valid step or endpoint. */
ScoreResult score_place(Score* score, int x, int y, TileKind kind);
/* Places validated values, growing a lane at its endpoint if needed. */
ScoreResult score_place_value(Score* score, int x, int y, TileValue value);
/* Removes the tile at `(x, y)` and any branch it owns. */
ScoreResult score_remove(Score* score, int x, int y);
/* Copies a non-jump suffix from `(x, y)`; otherwise leaves clipboard unchanged. */
void score_copy(const Score* score, int x, int y, Clipboard* clipboard);
/* Pastes clipboard values as a stack; rejects jump tiles and invalid geometry. */
ScoreResult score_paste(Score* score, int x, int y, const Clipboard* clipboard);
/* Previews moving a lane head or tile suffix between coordinates. */
MovePlan score_plan_move(const Score* score, int sx, int sy, int x, int y);
/* Rechecks and applies a plan against the current score. */
ScoreResult score_apply_move(Score* score, MovePlan plan);
/* Returns a display label for a tile kind, or "?" if invalid. */
const char* score_tile_label(TileKind kind);
/* Returns the short editor message for a valid ScoreResult. */
const char* score_message(ScoreResult result);
/* Returns the pitch-class name for a nonnegative semitone index. */
const char* score_note_name(int pitch);

#endif // SCORE_H
