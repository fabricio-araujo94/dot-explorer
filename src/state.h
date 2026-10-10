#ifndef STATE_H
#define STATE_H

#include "fs.h"
#include "utils/platform.h"

/** Clipboard paths are owned copies; clipboard_clear releases them. */
typedef struct {
    char **paths;
    size_t count;
    size_t capacity;
    bool is_cut;
} Clipboard;

/** Saved navigation location and selection index. */
typedef struct {
    char path[PATH_MAX];
    int selected_index;
} HistoryEntry;

/** Dynamically allocated back/forward history stacks. */
typedef struct {
    HistoryEntry *back;
    size_t back_count;
    size_t back_capacity;
    HistoryEntry *forward;
    size_t forward_count;
    size_t forward_capacity;
} NavigationHistory;

/** Dynamic list of indexes into a DirectoryList. */
typedef struct {
    int *indices;
    size_t count;
    size_t capacity;
} FilteredIndexList;

/**
 * Application state for one pane. Owns dir_list, filtered_entries and history;
 * do not copy this structure by value.
 */
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

/**
 * Initialize state from the current working directory.
 * @param state Writable storage.
 * @return true on success; false on failure, with errno set by the failing operation.
 * @note A state for which initialization returns false must not be used.
 */
bool state_init(AppState *state);

/** Release resources owned by a successfully initialized state. */
void state_cleanup(AppState *state);

/**
 * Change to an absolute path or a path relative to the current directory.
 * @return true on success; false on failure. On failure, the current location is kept.
 */
bool state_change_dir(AppState *state, const char *new_path);

/** Free and reset a filtered index list; accepts NULL. */
void filtered_index_list_clear(FilteredIndexList *list);

/**
 * Populate list with source indexes whose names fuzzy-match query.
 * Reuses list storage when possible.
 * @return true on success; false on invalid arguments, allocation failure or overflow.
 */
bool filter_entries(const char *query, const DirectoryList *source, FilteredIndexList *list);

/** Number of entries visible under the current filter. */
size_t state_visible_count(const AppState *state);

/** Resolve a visible row to its directory-list index, or -1 if out of range. */
int state_visible_index(const AppState *state, int view_index);

/** Add a copied path to the clipboard; the clipboard owns its copy. */
bool clipboard_add_entry(Clipboard *clipboard, const char *path);

/** Free all clipboard paths and reset its state. */
void clipboard_clear(Clipboard *clipboard);

/**
 * Apply clipboard copy/cut operations into destination_dir.
 * @param error_path Writable buffer for the failing path.
 * @param error_path_size Buffer size in bytes.
 * @return true on success; false on failure, with errno and error_path set.
 */
bool clipboard_apply_operation(Clipboard *clipboard, const char *destination_dir,
                               char *error_path, size_t error_path_size);

/** Push a location onto the back stack and clear the forward stack. */
bool history_push(AppState *state, const char *path, int selected_index);

/** Pop the back stack into caller-provided outputs; false if empty or invalid. */
bool history_pop_back(AppState *state, char *path, size_t path_size,
                      int *selected_index);

/** Pop the forward stack into caller-provided outputs; false if empty or invalid. */
bool history_pop_forward(AppState *state, char *path, size_t path_size,
                         int *selected_index);

#endif // STATE_H
