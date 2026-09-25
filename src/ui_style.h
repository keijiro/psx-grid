/*
 * ui_style.h - Shared dimensions and grayscale colors for the grid UI
 *
 * Render constants live here so grid geometry and panel spacing remain
 * consistent across the renderer and its host-side checks.
 */

#ifndef UI_STYLE_H
#define UI_STYLE_H

// Native output dimensions in pixels.
#define SCREEN_W 320
#define SCREEN_H 240
// The 16-pixel grid fills the native 320-by-240 frame exactly.
#define CELL_SIZE 16
// Grid origin and view extent in native pixels and cells.
#define VIEW_X 0
#define VIEW_Y 0
#define VIEW_COLS (SCREEN_W / CELL_SIZE)
#define VIEW_ROWS (SCREEN_H / CELL_SIZE)
// Eight-bit grayscale values used for background, drawing and panel layers.
#define UI_BACKGROUND 0x16
#define UI_DOT 0x4e
#define UI_INK 0xf2
#define UI_PANEL 0x1e
#define UI_BORDER 0x3a
#define UI_SHADOW 0x0c
#define UI_RULE 0x60
// Fixed panel spacing in native pixels.
#define UI_MENU_MARGIN 12
#define UI_MENU_ROW 17
#define UI_MENU_EDGE 16
// 35% of the reference light rail over the plane, rounded to an RGB byte.
#define UI_RAIL 0x60

#endif // UI_STYLE_H
