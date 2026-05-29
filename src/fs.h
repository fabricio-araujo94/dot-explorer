#ifndef FS_H
#define FS_H

#include <sys/types.h>
#include <sys/stat.h>
#include <stdbool.h>

typedef struct {
    char name[256];
    bool is_dir;
    off_t size;
    time_t mtime;
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

void fs_init_dir_list(DirectoryList *list);
void fs_free_dir_list(DirectoryList *list);
bool fs_read_dir(const char *path, DirectoryList *list);
void fs_sort_dir_list(DirectoryList *list, SortType sort_type);

bool fs_create_file(const char *path);
bool fs_create_dir(const char *path);
bool fs_delete(const char *path);
bool fs_rename(const char *old_path, const char *new_path);
bool fs_copy(const char *src_path, const char *dest_path);

#endif // FS_H
