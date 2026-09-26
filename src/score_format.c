/*
 * score_format.c - C ABI guards for the Rust file codec
 *
 * Implementation notes:
 *
 * The codec shares score and wire discriminants with C callers.
 * Compile-time checks catch incompatible changes before linking.
 */

#include "score_format.h"

// The Rust codec uses these discriminants as stable wire tags.
_Static_assert(WAVE_SINE == 0 && WAVE_TRIANGLE == 1 && WAVE_SAW == 2 &&
                   WAVE_SQUARE == 3 && WAVE_NOISE == 4,
               "Rust waveform tags must match the C model");
_Static_assert(TILE_NOTE == 1 && TILE_CYCLE == 2 && TILE_PROBABILITY == 3 &&
                   TILE_JUMP == 4 && TILE_RELATIVE == 5,
               "Rust tile tags must match the C model");
_Static_assert(sizeof(Score) == 199524, "Rust Score ABI changed");
_Static_assert(sizeof(TileValue) == 36, "Rust TileValue ABI changed");
