#ifndef SCORE_H
#define SCORE_H
#include <stdint.h>
#define SCORE_WIDTH 128
#define SCORE_HEIGHT 64
#define SCORE_LANES 16
#define SCORE_STEPS 64
#define SCORE_INITIAL_LENGTH 16
typedef struct { int active, x, y, length; uint8_t tiles[SCORE_STEPS]; } Lane;
typedef struct { Lane lanes[SCORE_LANES]; } Score;
typedef enum { CELL_EMPTY, CELL_HEAD, CELL_STEP, CELL_TILE, CELL_END } CellKind;
typedef struct { CellKind kind; int lane, step; } Cell;
typedef enum { SCORE_OK, SCORE_BOUNDS, SCORE_COLLISION, SCORE_FULL, SCORE_TILES, SCORE_INVALID } ScoreResult;
void score_init(Score *score);
Cell score_at(const Score *score, int x, int y);
ScoreResult score_can_create(const Score *score, int x, int y, int length);
ScoreResult score_create(Score *score, int x, int y, int length);
ScoreResult score_can_resize(const Score *score, int lane, int length);
ScoreResult score_resize(Score *score, int lane, int length);
ScoreResult score_delete(Score *score, int lane);
ScoreResult score_tile(Score *score, int x, int y, int place);
const char *score_message(ScoreResult result);
#endif
