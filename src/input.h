#ifndef INPUT_H
#define INPUT_H

#include "state.h"
#include <ncurses.h>

void input_handle(AppState *state, Clipboard *clipboard, int ch);
bool open_file_with_editor(const char *path);

#endif // INPUT_H
