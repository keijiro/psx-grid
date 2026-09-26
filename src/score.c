/*
 * score.c - C value ABI for the Rust score model
 *
 * Implementation notes:
 *
 * The MIPS C ABI passes aggregate values through hidden return slots and
 * by-value arguments. Pointer adapters keep those calls compatible while
 * score storage, validation and transactions live in Rust.
 */

#include "score.h"

_Static_assert(sizeof(Score) == 199524, "Rust Score ABI changed");
_Static_assert(sizeof(TileValue) == 36, "Rust TileValue ABI changed");
_Static_assert(sizeof(Cell) == 20, "Rust Cell ABI changed");
_Static_assert(sizeof(Clipboard) == 2308, "Rust Clipboard ABI changed");
_Static_assert(sizeof(MovePlan) == 20, "Rust MovePlan ABI changed");

extern void score_default_rust(int kind, TileValue* value);
extern ScoreResult score_edit_rust(Score* score, TileId id,
                                   const TileValue* value);
extern void score_at_rust(const Score* score, int x, int y, Cell* cell);
extern void score_resolve_rust(const Score* score, int x, int y, Cell* cell);
extern ScoreResult score_place_value_rust(Score* score, int x, int y,
                                          const TileValue* value);
extern void score_plan_move_rust(const Score* score, int sx, int sy, int x,
                                  int y, MovePlan* plan);
extern ScoreResult score_apply_move_rust(Score* score, const MovePlan* plan);
extern ScoreResult score_set_reverb_rust(Score* score,
                                         const ReverbSettings* reverb);
extern ScoreResult score_set_sound_rust(Score* score, int channel,
                                        const SoundSettings* sound);

TileValue score_default(TileKind kind)
{
    TileValue value;
    score_default_rust(kind, &value);
    return value;
}

ScoreResult score_edit(Score* score, TileId id, TileValue value)
{
    return score_edit_rust(score, id, &value);
}

Cell score_at(const Score* score, int x, int y)
{
    Cell cell;
    score_at_rust(score, x, y, &cell);
    return cell;
}

Cell score_resolve(const Score* score, int x, int y)
{
    Cell cell;
    score_resolve_rust(score, x, y, &cell);
    return cell;
}

ScoreResult score_place_value(Score* score, int x, int y, TileValue value)
{
    return score_place_value_rust(score, x, y, &value);
}

MovePlan score_plan_move(const Score* score, int sx, int sy, int x, int y)
{
    MovePlan plan;
    score_plan_move_rust(score, sx, sy, x, y, &plan);
    return plan;
}

ScoreResult score_apply_move(Score* score, MovePlan plan)
{
    return score_apply_move_rust(score, &plan);
}

ScoreResult score_set_reverb(Score* score, ReverbSettings reverb)
{
    return score_set_reverb_rust(score, &reverb);
}

ScoreResult score_set_sound(Score* score, int channel, SoundSettings sound)
{
    return score_set_sound_rust(score, channel, &sound);
}
