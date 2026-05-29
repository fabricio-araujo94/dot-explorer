#include "fs.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <dirent.h>
#include <limits.h>

void fs_init_dir_list(DirectoryList *list) {
    list->count = 0;
    list->capacity = 128;
    list->entries = (FileEntry *)malloc(list->capacity * sizeof(FileEntry));
}

void fs_free_dir_list(DirectoryList *list) {
    if (list->entries) {
        free(list->entries);
        list->entries = NULL;
    }
    list->count = 0;
    list->capacity = 0;
}

static SortType current_sort_type = SORT_NAME;

static int compare_entries(const void *a, const void *b) {
    const FileEntry *entryA = (const FileEntry *)a;
    const FileEntry *entryB = (const FileEntry *)b;

    if (entryA->is_dir && !entryB->is_dir) return -1;
    if (!entryA->is_dir && entryB->is_dir) return 1;

    if (current_sort_type == SORT_SIZE) {
        if (entryA->size > entryB->size) return -1;
        if (entryA->size < entryB->size) return 1;
    } else if (current_sort_type == SORT_DATE) {
        if (entryA->mtime > entryB->mtime) return -1;
        if (entryA->mtime < entryB->mtime) return 1;
    }

    return strcasecmp(entryA->name, entryB->name);
}

void fs_sort_dir_list(DirectoryList *list, SortType sort_type) {
    current_sort_type = sort_type;
    if (list->count > 0) {
        qsort(list->entries, list->count, sizeof(FileEntry), compare_entries);
    }
}

bool fs_read_dir(const char *path, DirectoryList *list) {
    DIR *dir = opendir(path);
    if (!dir) {
        return false;
    }

    list->count = 0;
    struct dirent *dp;
    char full_path[PATH_MAX];

    while ((dp = readdir(dir)) != NULL) {
        if (strcmp(dp->d_name, ".") == 0) continue;
        
        if (list->count >= list->capacity) {
            list->capacity *= 2;
            list->entries = (FileEntry *)realloc(list->entries, list->capacity * sizeof(FileEntry));
        }

        FileEntry *entry = &list->entries[list->count];
        strncpy(entry->name, dp->d_name, sizeof(entry->name) - 1);
        entry->name[sizeof(entry->name) - 1] = '\0';
        entry->is_selected = false;

        snprintf(full_path, sizeof(full_path), "%s/%s", path, dp->d_name);

        struct stat st;
        if (stat(full_path, &st) == 0) {
            entry->is_dir = S_ISDIR(st.st_mode);
            entry->size = st.st_size;
            entry->mtime = st.st_mtime;
        } else {
            entry->is_dir = (dp->d_type == DT_DIR);
            entry->size = 0;
            entry->mtime = 0;
        }

        list->count++;
    }

    closedir(dir);
    fs_sort_dir_list(list, SORT_NAME);
    return true;
}

bool fs_create_file(const char *path) {
    FILE *f = fopen(path, "w");
    if (f) {
        fclose(f);
        return true;
    }
    return false;
}

bool fs_create_dir(const char *path) {
#ifdef _WIN32
    return mkdir(path) == 0;
#else
    return mkdir(path, 0755) == 0;
#endif
}

bool fs_delete(const char *path) {
    return remove(path) == 0;
}

bool fs_rename(const char *old_path, const char *new_path) {
    return rename(old_path, new_path) == 0;
}

bool fs_copy(const char *src_path, const char *dest_path) {
    FILE *src = fopen(src_path, "rb");
    if (!src) return false;
    FILE *dest = fopen(dest_path, "wb");
    if (!dest) {
        fclose(src);
        return false;
    }
    
    char buf[8192];
    size_t bytes;
    while ((bytes = fread(buf, 1, sizeof(buf), src)) > 0) {
        fwrite(buf, 1, bytes, dest);
    }
    
    fclose(src);
    fclose(dest);
    return true;
}

