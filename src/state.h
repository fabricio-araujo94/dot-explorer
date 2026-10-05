#ifndef STATE_H
#define STATE_H

#include "fs.h"
#include "utils/platform.h"

typedef struct {
    char **paths;
    size_t count;
    size_t capacity;
    bool is_cut;
    char source_dir[PATH_MAX];
} Clipboard;

typedef struct {
    char path[PATH_MAX];
    int selected_index;
} HistoryEntry;

typedef struct {
    HistoryEntry *back;
    size_t back_count;
    size_t back_capacity;
    HistoryEntry *forward;
    size_t forward_count;
    size_t forward_capacity;
} NavigationHistory;

typedef struct {
    int *indices;
    size_t count;
    size_t capacity;
} FilteredIndexList;

typedef struct {
    char current_path[PATH_MAX];
    DirectoryList dir_list;
    int selected_index;
    int scroll_offset;
    bool should_quit;
    SortType sort_type;
    FilteredIndexList filtered_entries;
    bool filter_active;
    char filter_query[256];

    NavigationHistory history;
} AppState;

bool state_init(AppState *state);
void state_cleanup(AppState *state);
bool state_change_dir(AppState *state, const char *new_path);
void filtered_index_list_clear(FilteredIndexList *list);
bool filter_entries(const char *query, const DirectoryList *source, FilteredIndexList *list);
size_t state_visible_count(const AppState *state);
int state_visible_index(const AppState *state, int view_index);
bool clipboard_add_entry(Clipboard *clipboard, const char *path);
void clipboard_clear(Clipboard *clipboard);

bool clipboard_apply_operation(Clipboard *clipboard, const char *destination_dir,
                               char *error_path, size_t error_path_size);
bool history_push(AppState *state, const char *path, int selected_index);
bool history_pop_back(AppState *state, char *path, size_t path_size,
                      int *selected_index);
bool history_pop_forward(AppState *state, char *path, size_t path_size,
                         int *selected_index);

#endif // STATE_H
