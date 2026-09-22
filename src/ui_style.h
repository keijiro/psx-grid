#ifndef UI_STYLE_H
#define UI_STYLE_H
#define SCREEN_W 320
#define SCREEN_H 240
// Native-size study keeps both gate counters and the relative modifier open
// at this pitch. Heads use CH to preserve the compact label's side margins.
#define CELL_SIZE 16
#define VIEW_X 8
#define VIEW_Y 32
#define VIEW_COLS ((SCREEN_W - 2 * VIEW_X) / CELL_SIZE)
#define VIEW_ROWS ((192 - VIEW_Y) / CELL_SIZE)
#define UI_BACKGROUND 0x16
#define UI_DOT 0x4e
#define UI_INK 0xf2
#define UI_PANEL 0x1e
#define UI_BORDER 0x3a
// 35% of the reference light rail over the plane, rounded to an RGB byte.
#define UI_RAIL 0x60
#endif
