/*
 * value_api.h - Expression helpers for C score and input tests
 *
 * Test fixtures use values in assertions and compound initializers. These
 * helpers keep those expressions readable while exercising the pointer-only
 * C/Rust interface used by the application.
 */

#ifndef VALUE_API_H
#define VALUE_API_H

#include "ui/editor.h"

static inline TileValue test_score_default(TileKind kind)
{
    TileValue value;
    score_default(kind, &value);
    return value;
}

static inline ScoreResult test_score_edit(Score* score, TileId id,
                                         TileValue value)
{
    return score_edit(score, id, &value);
}

static inline Cell test_score_at(const Score* score, int x, int y)
{
    Cell cell;
    score_at(score, x, y, &cell);
    return cell;
}

static inline Cell test_score_resolve(const Score* score, int x, int y)
{
    Cell cell;
    score_resolve(score, x, y, &cell);
    return cell;
}

static inline ScoreResult test_score_place_value(Score* score, int x, int y,
                                                 TileValue value)
{
    return score_place_value(score, x, y, &value);
}

static inline MovePlan test_score_plan_move(const Score* score, int sx, int sy,
                                            int x, int y)
{
    MovePlan plan;
    score_plan_move(score, sx, sy, x, y, &plan);
    return plan;
}

static inline ScoreResult test_score_apply_move(Score* score, MovePlan plan)
{
    return score_apply_move(score, &plan);
}

static inline ScoreResult test_score_set_reverb(Score* score,
                                                ReverbSettings reverb)
{
    return score_set_reverb(score, &reverb);
}

static inline ScoreResult test_score_set_sound(Score* score, int channel,
                                               SoundSettings sound)
{
    return score_set_sound(score, channel, &sound);
}

static inline InputFrame test_input_update(Input* input, int connected,
                                            uint16_t held)
{
    InputFrame frame;
    input_update(input, connected, held, &frame);
    return frame;
}

static inline void test_editor_update(Editor* editor, InputFrame frame)
{
    editor_update(editor, &frame);
}

static inline void test_storage_init(Storage* storage, CardBackend backend)
{
    storage_init(storage, &backend);
}

#endif // VALUE_API_H
