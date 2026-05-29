#ifndef STATE_H
#define STATE_H

#include "fs.h"
#include <limits.h>

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

typedef enum {
    CLIPBOARD_NONE,
    CLIPBOARD_COPY,
    CLIPBOARD_CUT
} ClipboardOp;

typedef struct {
    char current_path[PATH_MAX];
    DirectoryList dir_list;
    int selected_index;
    int scroll_offset;
    bool should_quit;
    SortType sort_type;

    char clipboard_path[PATH_MAX];
    ClipboardOp clipboard_op;
} AppState;

void state_init(AppState *state);
void state_cleanup(AppState *state);
void state_change_dir(AppState *state, const char *new_path);

#endif // STATE_H
