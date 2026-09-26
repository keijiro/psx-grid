/*
 * abi_checks.c - Compile-time checks for the shared C/Rust interface
 *
 * Implementation notes:
 *
 * C and Rust mirror aggregate layouts and numeric tags independently.
 * Keep their C-side checks together so every linked target verifies the
 * boundary before linking.
 */

#include "editor.h"
#include "input.h"
#include "score_format.h"
#include "storage.h"
#include "ui/ui_style.h"

// The Rust codec uses these discriminants as stable wire tags.
_Static_assert(WAVE_SINE == 0 && WAVE_TRIANGLE == 1 && WAVE_SAW == 2 &&
                   WAVE_SQUARE == 3 && WAVE_NOISE == 4,
               "Rust waveform tags must match the C model");
_Static_assert(TILE_NOTE == 1 && TILE_CYCLE == 2 && TILE_PROBABILITY == 3 &&
                   TILE_JUMP == 4 && TILE_RELATIVE == 5,
               "Rust tile tags must match the C model");
_Static_assert(sizeof(Score) == 199524, "Rust Score ABI changed");
_Static_assert(sizeof(TileValue) == 36, "Rust TileValue ABI changed");
_Static_assert(sizeof(Cell) == 20, "Rust Cell ABI changed");
_Static_assert(sizeof(Clipboard) == 2308, "Rust Clipboard ABI changed");
_Static_assert(sizeof(MovePlan) == 20, "Rust MovePlan ABI changed");
_Static_assert(sizeof(Input) == 40, "Rust Input ABI changed");
_Static_assert(sizeof(InputFrame) == 48, "Rust InputFrame ABI changed");
_Static_assert(sizeof(CardFile) == 28, "Rust CardFile ABI changed");

// These shared values also shape Rust menu arrays and value ranges.
_Static_assert(EDITOR_MENU_ITEMS == 8 && EDITOR_ROWS == 16, "editor menu bounds");
_Static_assert(SCORE_WIDTH == 128 && SCORE_HEIGHT == 64, "editor grid bounds");
_Static_assert(SCORE_FILE_BYTES == 8192 && STORAGE_SLOTS == 15,
               "editor card bounds");
_Static_assert(SCORE_CHANNELS == 8 && SCORE_DIVISIONS == 12,
               "editor score bounds");
_Static_assert(SOUND_MAX_MS == 16000 && SOUND_MAX_MIX_MS == 500 &&
               SOUND_MAX_DECAY_MS == 2000 && SOUND_MAX_SWEEP == 24 &&
               WAVE_COUNT == 5,
               "editor sound bounds");
_Static_assert(EDIT_REVERB == 8 && ACTION_LOCK_RELEASE == 18 &&
                   TILE_RELATIVE == 5 && CELL_END == 4,
               "editor discriminants");

// Rust layout and clipping mirror these C-side presentation constants.
_Static_assert(SCREEN_W == 320 && SCREEN_H == 240 && CELL_SIZE == 16 &&
                   VIEW_X == 0 && VIEW_Y == 0 && VIEW_COLS == 20 &&
                   VIEW_ROWS == 15 && UI_MENU_MARGIN == 12 &&
                   UI_MENU_ROW == 17 && UI_MENU_EDGE == 16,
               "Rust render geometry");
_Static_assert(UI_DOT == 0x4e && UI_INK == 0xf2 && UI_PANEL == 0x1e &&
                   UI_BORDER == 0x3a && UI_SHADOW == 0x0c &&
                   UI_RULE == 0x60 && UI_RAIL == 0x60,
               "Rust render colors");
