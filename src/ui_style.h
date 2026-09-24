#ifndef UI_STYLE_H
#define UI_STYLE_H
#define SCREEN_W 320
#define SCREEN_H 240
// The 16-pixel grid fills the native 320-by-240 frame exactly.
#define CELL_SIZE 16
#define VIEW_X 0
#define VIEW_Y 0
#define VIEW_COLS (SCREEN_W / CELL_SIZE)
#define VIEW_ROWS (SCREEN_H / CELL_SIZE)
#define UI_BACKGROUND 0x16
#define UI_DOT 0x4e
#define UI_INK 0xf2
#define UI_PANEL 0x1e
#define UI_BORDER 0x3a
#define UI_SHADOW 0x0c
#define UI_RULE 0x60
#define UI_MENU_MARGIN 12
#define UI_MENU_ROW 17
#define UI_MENU_EDGE 16
// 35% of the reference light rail over the plane, rounded to an RGB byte.
#define UI_RAIL 0x60
#endif
