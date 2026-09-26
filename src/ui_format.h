/*
 * ui_format.h - Display text for editor menu values
 *
 * Formats a menu row from caller-owned editor state into a bounded C string.
 * Rendering reads the result without changing the editor.
 */

#ifndef UI_FORMAT_H
#define UI_FORMAT_H

#include "editor.h"

/*
 * Formats a valid row `id` in `editor` into `buffer`. A row without a value
 * leaves an empty string. `size` must be positive.
 * `editor` and `buffer` must not be NULL.
 */
void ui_format_row_value(const Editor* editor, int id, char* buffer, int size);

#endif // UI_FORMAT_H
