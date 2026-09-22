#include "input.h"
#include <string.h>
void input_init(Input *i) { memset(i, 0, sizeof(*i)); }
void input_reset_repeat(Input *i) { i->direction = -1; i->countdown = INPUT_DELAY; }
InputFrame input_update(Input *i, int connected, uint16_t held) {
    InputFrame f = {0};
    f.connected = connected;
    if (!connected) { input_init(i); return f; }
    int dx = !!(held & INPUT_RIGHT) - !!(held & INPUT_LEFT);
    int dy = dx ? 0 : !!(held & INPUT_DOWN) - !!(held & INPUT_UP);
    int direction = dx ? (dx > 0 ? 1 : 2) : dy ? (dy > 0 ? 3 : 4) : 0;
    if (!i->connected) {
        i->connected = 1; i->previous = held;
        // Ignore all buttons held at reconnection until they are released.
        i->direction = held ? -2 : 0;
        return f;
    }
    if (i->direction == -2) {
        i->previous = held;
        if (!held) i->direction = 0;
        return f;
    }
    f.cross = !!(held & ~i->previous & INPUT_CROSS);
    f.circle = !!(held & ~i->previous & INPUT_CIRCLE);
    i->previous = held;
    if (!direction) { i->direction = 0; i->countdown = 0; }
    else if (i->direction == -1) { i->direction = direction; i->countdown = INPUT_DELAY; }
    else if (direction != i->direction) {
        i->direction = direction; i->countdown = INPUT_DELAY; f.dx = dx; f.dy = dy;
    } else if (--i->countdown <= 0) {
        i->countdown = INPUT_INTERVAL; f.dx = dx; f.dy = dy;
    }
    return f;
}
