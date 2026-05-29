#ifndef UI_H
#define UI_H

#include "state.h"
#include <ncurses.h>

void ui_init(void);
void ui_cleanup(void);
void ui_render(const AppState *state);
bool ui_prompt(const char *prompt, char *buffer, size_t buf_size);
void ui_show_message(const char *title, const char *message);

#endif // UI_H
