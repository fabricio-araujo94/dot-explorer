#ifndef FS_H
#define FS_H

#include <sys/types.h>
#include <sys/stat.h>
#include <stdbool.h>
#include <stddef.h>

typedef struct {
    char name[256];
    bool is_dir;
    off_t size;
    time_t mtime;
    mode_t mode;
    bool is_symlink;
    bool is_selected;
} FileEntry;

typedef struct {
    FileEntry *entries;
    int count;
    int capacity;
} DirectoryList;

typedef enum {
    SORT_NAME,
    SORT_SIZE,
    SORT_DATE
} SortType;

typedef struct {
    bool follow_symlinks;
    bool rollback_on_error;
} FsCopyOptions;

void fs_init_dir_list(DirectoryList *list);
void fs_free_dir_list(DirectoryList *list);
bool fs_read_dir(const char *path, DirectoryList *list);
void fs_sort_dir_list(DirectoryList *list, SortType sort_type);

bool fs_create_file(const char *path);
bool fs_create_dir(const char *path);
bool fs_delete(const char *path);
bool fs_delete_recursive(const char *path);
bool fs_rename(const char *old_path, const char *new_path);
bool fs_copy(const char *src_path, const char *dest_path);
bool fs_copy_recursive(const char *src_path, const char *dest_path);
bool fs_copy_recursive_with_options(const char *src_path, const char *dest_path,
                                    const FsCopyOptions *options,
                                    char *error_path, size_t error_path_size);

#endif // FS_H
