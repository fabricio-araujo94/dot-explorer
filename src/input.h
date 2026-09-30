#ifndef INPUT_H
#define INPUT_H

#include "state.h"
#include <stdbool.h>
#include <stddef.h>

#ifndef KEY_DOWN
#define KEY_DOWN 0402
#endif
#ifndef KEY_UP
#define KEY_UP 0403
#endif
#ifndef KEY_LEFT
#define KEY_LEFT 0404
#endif
#ifndef KEY_RIGHT
#define KEY_RIGHT 0405
#endif
#ifndef KEY_BACKSPACE
#define KEY_BACKSPACE 0407
#endif
#ifndef KEY_ENTER
#define KEY_ENTER 0527
#endif

typedef struct InputCallbacks {
    bool (*prompt)(const char *prompt, char *buffer, size_t buf_size, void *userdata);
    void (*show_message)(const char *title, const char *message, void *userdata);
    int (*get_list_height)(void *userdata);
    bool (*open_file)(const char *path, void *userdata);
    void (*render_filter_prompt)(const char *query, void *userdata);
    int (*get_char)(void *userdata);
    void (*set_cursor)(int visibility, void *userdata);
    void *userdata;
} InputCallbacks;

void input_handle(AppState *state, Clipboard *clipboard, int ch, const InputCallbacks *callbacks);
bool open_file_with_editor(const char *path);

#endif // INPUT_H
