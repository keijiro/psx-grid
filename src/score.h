/*
 * score.h - Editable grid score and validated model operations
 *
 * A caller-owned Score holds lanes, tiles, sounds and stable birth generations
 * in fixed storage. Mutating operations report a ScoreResult and leave it
 * unchanged on failure. Callers serialize access to the mutable score.
 */

#ifndef SCORE_H
#define SCORE_H

#include <stdint.h>

// Fixed model limits; geometry and tile pool storage have no dynamic
// allocation.
#define SCORE_WIDTH 128
#define SCORE_HEIGHT 64
#define SCORE_LANES 16
#define SCORE_CHANNELS 8
#define SCORE_STEPS 64
#define SCORE_INITIAL_LENGTH 16
#define SCORE_TILE_CAPACITY 4096
#define SCORE_DIVISIONS 12

// Maximum envelope duration and lock offset magnitude in milliseconds.
#define SOUND_MAX_MS 16000
// Maximum mix ramp, pitch sweep duration and semitone sweep depth.
#define SOUND_MAX_MIX_MS 500
#define SOUND_MAX_DECAY_MS 2000
#define SOUND_MAX_SWEEP 24
// New channels start with this dry dual-sine sound.
#define SOUND_DEFAULT                                                          \
    ((SoundSettings){5, 5, WAVE_SINE, WAVE_SINE, 120, 280, 0, 200, 0})
// User-editable tempo bounds in beats per minute.
#define SCORE_MIN_BPM 30
#define SCORE_MAX_BPM 300
// Medium room with a 30% wet return for newly initialized scores.
#define REVERB_DEFAULT ((ReverbSettings){1, 30})

// Tile behavior interpreted by the sequencer at a lane step.
typedef enum
{
    TILE_NONE,                      // The pool slot is unoccupied.
    TILE_NOTE,                      // Play a note at this step.
    TILE_CYCLE,                     // Gate following events by pattern.
    TILE_PROBABILITY,               // Gate following events by chance.
    TILE_JUMP,                      // Traverse a branch lane.
    TILE_RELATIVE,                  // Apply relative envelope locks.
    TILE_KIND_COUNT                 // The number of tile kinds.
} TileKind;

// Zero is the empty link. Live IDs stay stable across movement and reordering;
// deletion releases an ID for reuse by a later placement.
typedef uint16_t TileId;
// Independent override bits for relative envelope locks.
enum
{
    LOCK_ATTACK = 1,                // Override the attack interval.
    LOCK_RELEASE = 2                // Override the release interval.
};

// Wave bank choices available to each half of a logical voice.
typedef enum
{
    WAVE_SINE,                      // Use the sine sample bank.
    WAVE_TRIANGLE,                  // Use the triangle sample bank.
    WAVE_SAW,                       // Use the sawtooth sample bank.
    WAVE_SQUARE,                    // Use the square sample bank.
    WAVE_NOISE,                     // Use the noise sample bank.
    WAVE_COUNT                      // The number of wave banks.
} Waveform;

// Channel sound parameters; time fields use milliseconds.
typedef struct
{
    int attack;
    int release;
    int wave_a;
    int wave_b;
    int mix_attack;
    int mix_release;
    int sweep;
    int decay;
    int reverb;
} SoundSettings;

// Shared reverb preset index and wet-return percentage.
typedef struct
{
    int size;
    int amount;
} ReverbSettings;

// Per-tile playback values; fields not used by `kind` keep default values.
// Pitch counts semitones from C0. Length counts twentieths of a step, so all
// supported 0.05-step edits remain exact without floating point.
typedef struct
{
    TileKind kind;
    int pitch;
    int length;
    int period;
    int chance;
    uint32_t pattern;
    int lock_mask;
    int attack;
    int release;
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
    int active;
    int x;
    int y;
    int length;
    int division;
    int channel;
    TileId source;
    TileId tiles[SCORE_STEPS];
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
    uint32_t generation;
    uint32_t lane_generation[SCORE_LANES];
    uint32_t tile_generation[SCORE_TILE_CAPACITY + 1];
} Score;

// What a coordinate addresses before or after insertion-point resolution.
typedef enum
{
    CELL_EMPTY,                     // No score object occupies the cell.
    CELL_HEAD,                      // The cell is a lane head.
    CELL_STEP,                      // The cell is an empty lane step.
    CELL_TILE,                      // The cell contains a tile.
    CELL_END                        // The cell extends a lane endpoint.
} CellKind;

// Located cell and its lane, step, stack depth and optional tile ID.
typedef struct
{
    CellKind kind;
    int lane;
    int step;
    int depth;
    TileId tile;
} Cell;

// Edit outcomes; nonzero results leave the original score untouched.
typedef enum
{
    SCORE_OK,                       // The edit was accepted.
    SCORE_BOUNDS,                   // A coordinate is outside the grid.
    SCORE_COLLISION,                // The edit overlaps live geometry.
    SCORE_FULL,                     // The lane or tile pool is full.
    SCORE_TILES,                    // The edit exceeds tile constraints.
    SCORE_INVALID,                  // The input or state is invalid.
    SCORE_CYCLE                     // A branch would form a cycle.
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
    int sx;
    int sy;
    int x;
    int y;
    ScoreResult result;
} MovePlan;

// Supported step divisions in ascending order.
extern const int score_divisions[SCORE_DIVISIONS];

/*
 * Returns a display label, or "?" for an invalid preset index.
 */
const char* score_reverb_size(int size);

/*
 * Returns a display label, or "?" for an invalid waveform.
 */
const char* score_wave_name(int wave);

/*
 * Model calls use caller-owned, serialized state. Coordinates and lane/channel
 * indices are zero-based. ScoreResult edits leave `score` unchanged on failure
 * unless a function says otherwise.
 */
/*
 * Initializes a caller-owned empty score with default sounds and tempo.
 * `score` must not be NULL.
 */
void score_init(Score* score);
/*
 * Validates links, tile values and geometry before accepting an imported score.
 * `score` must not be NULL.
 */
ScoreResult score_validate_import(const Score* score);
/*
 * Sets a tempo in SCORE_MIN_BPM..SCORE_MAX_BPM.
 * `score` must not be NULL.
 */
ScoreResult score_set_bpm(Score* score, int bpm);
/*
 * Sets a preset 0..2 and wet amount 0..100.
 * `score` and `reverb` must not be NULL or overlap.
 */
ScoreResult score_set_reverb(Score* score, const ReverbSettings* reverb);
/*
 * Sets one 0-based channel after validating all sound parameters.
 * `score` and `sound` must not be NULL or overlap.
 */
ScoreResult score_set_sound(Score* score, int channel,
                            const SoundSettings* sound);
/*
 * Writes the exact cell at a grid coordinate, or CELL_EMPTY.
 * `score` and `cell` must not be NULL or overlap.
 */
void score_at(const Score* score, int x, int y, Cell* cell);
/*
 * Resolves an empty coordinate just below a stack as an insertion step.
 * `score` and `cell` must not be NULL or overlap.
 */
void score_resolve(const Score* score, int x, int y, Cell* cell);
/*
 * Writes default values for a valid tile kind to `value`.
 * `value` must not be NULL.
 */
void score_default(TileKind kind, TileValue* value);
/*
 * Edits a live tile by ID; its kind must remain unchanged.
 * `score` and `value` must not be NULL or overlap.
 */
ScoreResult score_edit(Score* score, TileId id, const TileValue* value);
/*
 * Returns the inherited channel of a live lane, or -1 if unresolved.
 * `score` must not be NULL.
 */
int score_channel(const Score* score, int lane);
/*
 * Sets a root lane's 0-based channel; branches inherit their source channel.
 * `score` must not be NULL.
 */
ScoreResult score_set_channel(Score* score, int lane, int channel);
/*
 * Returns the inherited division of a live lane, or 16 if unresolved.
 * `score` must not be NULL.
 */
int score_division(const Score* score, int lane);
/*
 * Sets a root lane's division to one of score_divisions.
 * `score` must not be NULL.
 */
ScoreResult score_set_division(Score* score, int lane, int division);
/*
 * Previews creating a root lane of `length` steps at `(x, y)`.
 * `score` must not be NULL.
 */
ScoreResult score_can_create(const Score* score, int x, int y, int length);
/*
 * Creates a root lane when the same geometry and file limits are satisfied.
 * `score` must not be NULL.
 */
ScoreResult score_create(Score* score, int x, int y, int length);
/*
 * Previews a lane resize; trailing occupied steps cannot be removed.
 * `score` must not be NULL.
 */
ScoreResult score_can_resize(const Score* score, int lane, int length);
/*
 * Resizes a live lane after the preview checks pass.
 * `score` must not be NULL.
 */
ScoreResult score_resize(Score* score, int lane, int length);
/*
 * Deletes a live lane and its descendants through jump tiles.
 * `score` must not be NULL.
 */
ScoreResult score_delete(Score* score, int lane);
/*
 * Places default values for `kind` at a valid step or endpoint.
 * `score` must not be NULL.
 */
ScoreResult score_place(Score* score, int x, int y, TileKind kind);
/*
 * Places validated values, growing a lane at its endpoint if needed.
 * `score` and `value` must not be NULL or overlap.
 */
ScoreResult score_place_value(Score* score, int x, int y,
                              const TileValue* value);
/*
 * Removes the tile at `(x, y)` and any branch it owns.
 * `score` must not be NULL.
 */
ScoreResult score_remove(Score* score, int x, int y);
/*
 * Copies a non-jump suffix from `(x, y)`; otherwise leaves clipboard unchanged.
 * `score` and `clipboard` must not be NULL.
 */
void score_copy(const Score* score, int x, int y, Clipboard* clipboard);
/*
 * Pastes clipboard values as a stack; rejects jump tiles and invalid geometry.
 * `score` and `clipboard` must not be NULL.
 */
ScoreResult score_paste(Score* score, int x, int y, const Clipboard* clipboard);
/*
 * Writes a preview of moving a lane head or tile suffix between coordinates.
 * `score` and `plan` must not be NULL or overlap.
 */
void score_plan_move(const Score* score, int sx, int sy, int x, int y,
                     MovePlan* plan);
/*
 * Rechecks and applies a plan against the current score.
 * `score` and `plan` must not be NULL or overlap.
 */
ScoreResult score_apply_move(Score* score, const MovePlan* plan);
/*
 * Returns a display label for a tile kind, or "?" if invalid.
 */
const char* score_tile_label(TileKind kind);
/*
 * Returns the short editor message for a valid ScoreResult.
 */
const char* score_message(ScoreResult result);
/*
 * Returns the pitch-class name for a nonnegative semitone index.
 */
const char* score_note_name(int pitch);

// C and Rust share these fixed layouts across the pointer-only interface.
_Static_assert(sizeof(Score) == 199524, "Rust Score ABI changed");
_Static_assert(sizeof(TileValue) == 36, "Rust TileValue ABI changed");
_Static_assert(sizeof(Cell) == 20, "Rust Cell ABI changed");
_Static_assert(sizeof(Clipboard) == 2308, "Rust Clipboard ABI changed");
_Static_assert(sizeof(MovePlan) == 20, "Rust MovePlan ABI changed");

#endif // SCORE_H
