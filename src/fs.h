#ifndef FS_H
#define FS_H

#include <sys/types.h>
#include <sys/stat.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/** Metadata for one directory entry. The name is stored inline. */
typedef struct {
    char name[256];
    bool is_dir;
    off_t size;
    time_t mtime;
    mode_t mode;
    bool is_symlink;
    bool is_selected;
} FileEntry;

/** Dynamic directory listing. The entries buffer is owned by this structure. */
typedef struct {
    FileEntry *entries;
    size_t count;
    size_t capacity;
} DirectoryList;

/** Supported built-in directory ordering strategies. */
typedef enum {
    SORT_NAME,
    SORT_SIZE,
    SORT_DATE
} SortType;

/** Copy policies. A NULL options pointer selects the default policies. */
typedef struct {
    bool follow_symlinks;
    bool rollback_on_error;
} FsCopyOptions;

/** Optional synchronous callbacks invoked while a recursive copy runs. */
typedef struct {
    bool (*progress)(uint64_t bytes_copied, uint64_t total_bytes, void *context);
    bool (*is_cancelled)(void *context);
    void (*file_complete)(void *context);
    void *progress_context;
} FsCopyCallbacks;

/**
 * Initialize a directory list and allocate its initial entry buffer.
 * @pre list points to writable storage.
 * @note Allocation failure leaves entries NULL and capacity zero.
 */
void fs_init_dir_list(DirectoryList *list);

/** Release the entries buffer and reset the list. Safe after initialization. */
void fs_free_dir_list(DirectoryList *list);

/**
 * Read and name-sort a directory into list.
 * @param path Directory path.
 * @param list Initialized list whose entries buffer is managed by the caller.
 * @return true on success; false on failure, with the cause reported through errno.
 * @note A failed read may leave a partial listing; call fs_free_dir_list afterward.
 */
bool fs_read_dir(const char *path, DirectoryList *list);

/** Sort an existing list in place; invalid sort values fall back to name order. */
void fs_sort_dir_list(DirectoryList *list, SortType sort_type);

/** Create an empty file. Returns false and sets errno on failure. */
bool fs_create_file(const char *path);
/** Create a directory. Returns false and sets errno on failure. */
bool fs_create_dir(const char *path);
/** Delete a file or directory tree. Protected root/current-directory paths are rejected. */
bool fs_delete(const char *path);
/** Recursively delete a path. Returns false and sets errno on failure. */
bool fs_delete_recursive(const char *path);
/** Rename a filesystem entry. Returns false and sets errno on failure. */
bool fs_rename(const char *old_path, const char *new_path);

/** Copy recursively with default options and no callbacks. */
bool fs_copy_recursive(const char *src_path, const char *dest_path);

/**
 * Recursively copy a file or directory tree.
 * @param src_path Existing source path.
 * @param dest_path Destination path, which must not equal or be inside the source.
 * @param options Optional symlink and rollback policies; NULL selects defaults.
 * @param callbacks Optional progress, cancellation and completion callbacks.
 * @param error_path Writable buffer receiving the failing path on error.
 * @param error_path_size Size of error_path in bytes; must be greater than zero.
 * @return true on success; false on failure, with errno and error_path describing it.
 * @note Callbacks run synchronously on the calling thread and share progress_context.
 *       When used by Task, that thread is the task worker; callback state must be
 *       synchronized if accessed elsewhere.
 */
bool fs_copy_recursive_with_options(const char *src_path, const char *dest_path,
                                    const FsCopyOptions *options,
                                    const FsCopyCallbacks *callbacks,
                                    char *error_path, size_t error_path_size);

#endif // FS_H
