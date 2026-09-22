#ifndef SCORE_H
#define SCORE_H
#include <stdint.h>
#define SCORE_WIDTH 128
#define SCORE_HEIGHT 64
#define SCORE_LANES 16
#define SCORE_STEPS 64
#define SCORE_INITIAL_LENGTH 16
#define SCORE_TILE_CAPACITY 4096
#define SCORE_DIVISIONS 12
typedef enum { TILE_NONE, TILE_NOTE, TILE_CYCLE, TILE_PROBABILITY, TILE_JUMP, TILE_RELATIVE, TILE_KIND_COUNT } TileKind;
// Zero is the empty link. Live IDs stay stable across movement and reordering;
// deletion releases an ID for reuse by a later placement.
typedef uint16_t TileId;
// Pitch counts semitones from C0. Length counts twentieths of a step, so all
// supported 0.05-step edits remain exact without floating point.
#define SOUND_MAX_MS 16000
enum { LOCK_ATTACK=1, LOCK_RELEASE=2 };
typedef struct { int attack, release; } SoundSettings;
typedef struct { TileKind kind; int pitch, length, period, chance; uint32_t pattern;
    int lock_mask, attack, release;
} TileValue;
typedef struct { TileValue value; TileId next; int branch; } Tile;
typedef struct { int active, x, y, length, division; TileId source, tiles[SCORE_STEPS]; } Lane;
typedef struct { Lane lanes[SCORE_LANES]; Tile tiles[SCORE_TILE_CAPACITY + 1]; SoundSettings sound; uint32_t revision; } Score;
typedef enum { CELL_EMPTY, CELL_HEAD, CELL_STEP, CELL_TILE, CELL_END } CellKind;
typedef struct { CellKind kind; int lane, step, depth; TileId tile; } Cell;
typedef enum { SCORE_OK, SCORE_BOUNDS, SCORE_COLLISION, SCORE_FULL, SCORE_TILES, SCORE_INVALID, SCORE_CYCLE } ScoreResult;
typedef struct { int count; TileValue values[SCORE_HEIGHT]; } Clipboard;
typedef struct { int sx, sy, x, y; ScoreResult result; } MovePlan;
extern const int score_divisions[SCORE_DIVISIONS];
void score_init(Score *score);
ScoreResult score_set_sound(Score *score, SoundSettings sound);
Cell score_at(const Score *score, int x, int y);
Cell score_resolve(const Score *score, int x, int y);
TileValue score_default(TileKind kind);
ScoreResult score_edit(Score *score, TileId id, TileValue value);
int score_division(const Score *score, int lane);
ScoreResult score_set_division(Score *score, int lane, int division);
ScoreResult score_can_create(const Score *score, int x, int y, int length);
ScoreResult score_create(Score *score, int x, int y, int length);
ScoreResult score_can_resize(const Score *score, int lane, int length);
ScoreResult score_resize(Score *score, int lane, int length);
ScoreResult score_delete(Score *score, int lane);
ScoreResult score_place(Score *score, int x, int y, TileKind kind);
ScoreResult score_place_value(Score *score, int x, int y, TileValue value);
ScoreResult score_remove(Score *score, int x, int y);
void score_copy(const Score *score, int x, int y, Clipboard *clipboard);
ScoreResult score_paste(Score *score, int x, int y, const Clipboard *clipboard);
MovePlan score_plan_move(const Score *score, int sx, int sy, int x, int y);
ScoreResult score_apply_move(Score *score, MovePlan plan);
const char *score_tile_label(TileKind kind);
const char *score_message(ScoreResult result);
const char *score_note_name(int pitch);
#endif
