#ifndef STATE_H
#define STATE_H

#include "fs.h"
#include <limits.h>

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

typedef struct {
    char **paths;
    size_t count;
    size_t capacity;
    bool is_cut;
    char source_dir[PATH_MAX];
} Clipboard;

typedef struct {
    char current_path[PATH_MAX];
    DirectoryList dir_list;
    int selected_index;
    int scroll_offset;
    bool should_quit;
    SortType sort_type;

    Clipboard clipboard;
} AppState;

void state_init(AppState *state);
void state_cleanup(AppState *state);
void state_change_dir(AppState *state, const char *new_path);
bool clipboard_add_entry(Clipboard *clipboard, const char *path);
void clipboard_clear(Clipboard *clipboard);
void clipboard_invert_selection(DirectoryList *list);
bool clipboard_apply_operation(Clipboard *clipboard, const char *destination_dir,
                               char *error_path, size_t error_path_size);

#endif // STATE_H
