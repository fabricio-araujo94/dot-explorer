#include "fs.h"
#include "utils.h"
#include "utils/platform.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <dirent.h>
#include <unistd.h>
#include <utime.h>
#ifndef _WIN32
#include <sys/types.h>
#endif

#define FS_MAX_COPY_DEPTH 128

static bool is_protected_delete_path(const char *path) {
    size_t length;
    const char *name;

    if (!path || !*path) return true;
    length = strlen(path);
    while (length > 1 && (path[length - 1] == '/' || path[length - 1] == '\\')) {
        length--;
    }
    if (length == 1 && (path[0] == '/' || path[0] == '\\')) return true;
#ifdef _WIN32
    if (length == 3 && path[1] == ':' &&
        (path[2] == '/' || path[2] == '\\')) return true;
#endif
    name = path + length;
    while (name > path && name[-1] != '/' && name[-1] != '\\') name--;
    return strcmp(name, ".") == 0 || strcmp(name, "..") == 0;
}

static bool set_error_path(char *error_path, size_t error_path_size, const char *path) {
    int written;
    if (!error_path || error_path_size == 0) {
        return false;
    }
    written = snprintf(error_path, error_path_size, "%s", path);
    if (written < 0 || (size_t)written >= error_path_size) {
        error_path[error_path_size - 1] = '\0';
        errno = ENAMETOOLONG;
        return false;
    }
    return true;
}

static bool parent_is_writable(const char *path) {
    char parent[PATH_MAX];
    char *separator;

    if (strlen(path) >= sizeof(parent)) {
        errno = ENAMETOOLONG;
        return false;
    }
    strcpy(parent, path);
    separator = strrchr(parent, '/');
#ifdef _WIN32
    {
        char *backslash = strrchr(parent, '\\');
        if (backslash && (!separator || backslash > separator)) {
            separator = backslash;
        }
    }
#endif
    if (!separator) {
        return access(".", W_OK | X_OK) == 0;
    }
    if (separator == parent) {
        separator[1] = '\0';
    } else {
        *separator = '\0';
    }
    return access(parent, W_OK | X_OK) == 0;
}

static bool destination_is_inside_source(const char *source, const char *destination) {
    struct stat source_stat;
    char source_real[PATH_MAX];
    char parent[PATH_MAX];
    char parent_real[PATH_MAX];
    char *separator;
    size_t source_length;

    if (lstat(source, &source_stat) != 0 || !S_ISDIR(source_stat.st_mode) ||
        !platform_realpath(source, source_real, sizeof(source_real)) ||
        strlen(destination) >= sizeof(parent)) {
        return false;
    }
    strcpy(parent, destination);
    separator = strrchr(parent, '/');
#ifdef _WIN32
    {
        char *backslash = strrchr(parent, '\\');
        if (backslash && (!separator || backslash > separator)) separator = backslash;
    }
#endif
    if (!separator) {
        strcpy(parent, ".");
    } else if (separator == parent) {
        separator[1] = '\0';
    } else {
        *separator = '\0';
    }
    if (!platform_realpath(parent, parent_real, sizeof(parent_real))) {
        return false;
    }
    source_length = strlen(source_real);
    return strncmp(parent_real, source_real, source_length) == 0 &&
           (parent_real[source_length] == '\0' ||
            parent_real[source_length] == '/' || parent_real[source_length] == '\\');
}

static bool remove_tree(const char *path, char *error_path, size_t error_path_size) {
    struct stat st;
    DIR *dir;
    struct dirent *entry;

    if (is_protected_delete_path(path)) {
        errno = EINVAL;
        set_error_path(error_path, error_path_size, path);
        return false;
    }
    if (lstat(path, &st) != 0) {
        set_error_path(error_path, error_path_size, path);
        return false;
    }
    if (!S_ISDIR(st.st_mode)) {
        if (unlink(path) != 0) {
            set_error_path(error_path, error_path_size, path);
            return false;
        }
        return true;
    }
    if (access(path, R_OK | W_OK | X_OK) != 0) {
        set_error_path(error_path, error_path_size, path);
        return false;
    }
    dir = opendir(path);
    if (!dir) {
        set_error_path(error_path, error_path_size, path);
        return false;
    }
    while ((entry = readdir(dir)) != NULL) {
        char child[PATH_MAX];
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
            continue;
        }
        if (!utils_join_path(child, sizeof(child), path, entry->d_name) ||
            !remove_tree(child, error_path, error_path_size)) {
            closedir(dir);
            return false;
        }
    }
    if (closedir(dir) != 0 || rmdir(path) != 0) {
        set_error_path(error_path, error_path_size, path);
        return false;
    }
    return true;
}

static bool copy_regular_file(const char *src_path, const char *dest_path,
                              const struct stat *source_stat,
                              const FsCopyOptions *options,
                              char *error_path, size_t error_path_size) {
    FILE *src = NULL;
    FILE *dest = NULL;
    char buffer[8192];
    size_t bytes;
    uint64_t copied = 0;
    bool success = true;
    bool read_error;
    bool source_closed;
    bool dest_closed;

    if (access(src_path, R_OK) != 0 ||
        (access(dest_path, F_OK) == 0 ? access(dest_path, W_OK) != 0
                         : !parent_is_writable(dest_path))) {
        set_error_path(error_path, error_path_size, src_path);
        return false;
    }
    src = fopen(src_path, "rb");
    if (!src) {
        set_error_path(error_path, error_path_size, src_path);
        return false;
    }
    dest = fopen(dest_path, "wb");
    if (!dest) {
        set_error_path(error_path, error_path_size, dest_path);
        fclose(src);
        return false;
    }
    while ((bytes = fread(buffer, 1, sizeof(buffer), src)) > 0) {
        if (options && options->is_cancelled &&
            options->is_cancelled(options->progress_context)) {
            errno = ECANCELED;
            success = false;
            break;
        }
        if (fwrite(buffer, 1, bytes, dest) != bytes) {
            success = false;
            break;
        }
        copied += bytes;
        if (options && options->progress &&
            !options->progress(copied, (uint64_t)source_stat->st_size,
                               options->progress_context)) {
            errno = ECANCELED;
            success = false;
            break;
        }
    }
    read_error = ferror(src) != 0;
    source_closed = fclose(src) == 0;
    dest_closed = fclose(dest) == 0;
    if (read_error || !source_closed || !dest_closed) {
        success = false;
    }
    src = NULL;
    dest = NULL;
    if (success && chmod(dest_path, source_stat->st_mode & 07777) != 0) {
        success = false;
    }
    if (success) {
        struct utimbuf times = { source_stat->st_atime, source_stat->st_mtime };
        if (utime(dest_path, &times) != 0) {
            success = false;
        }
    }
    if (!success) {
        set_error_path(error_path, error_path_size, dest_path);
    }
    return success;
}

static bool copy_symlink(const char *src_path, const char *dest_path,
                         char *error_path, size_t error_path_size) {
#ifdef _WIN32
    (void)src_path;
    (void)dest_path;
    set_error_path(error_path, error_path_size, src_path);
    errno = ENOTSUP;
    return false;
#else
    char target[PATH_MAX];
    ssize_t length = readlink(src_path, target, sizeof(target) - 1);
    if (length < 0) {
        set_error_path(error_path, error_path_size, src_path);
        return false;
    }
    target[length] = '\0';
    if (symlink(target, dest_path) != 0) {
        set_error_path(error_path, error_path_size, dest_path);
        return false;
    }
    return true;
#endif
}

static bool copy_tree(const char *src_path, const char *dest_path,
                      const FsCopyOptions *options,
                      char *error_path, size_t error_path_size,
                      unsigned int depth) {
    struct stat source_stat;
    bool follow_symlinks = options && options->follow_symlinks;

    if (depth > FS_MAX_COPY_DEPTH) {
        errno = ELOOP;
        set_error_path(error_path, error_path_size, src_path);
        return false;
    }

    if ((follow_symlinks ? stat(src_path, &source_stat) : lstat(src_path, &source_stat)) != 0) {
        set_error_path(error_path, error_path_size, src_path);
        return false;
    }
    if (S_ISLNK(source_stat.st_mode) && !follow_symlinks) {
        return copy_symlink(src_path, dest_path, error_path, error_path_size);
    }
    if (S_ISDIR(source_stat.st_mode)) {
        DIR *dir;
        struct dirent *entry;
        if (platform_mkdir(dest_path, source_stat.st_mode & 07777) != 0 && errno != EEXIST) {
            set_error_path(error_path, error_path_size, dest_path);
            return false;
        }
        if (access(dest_path, W_OK | X_OK) != 0) {
            set_error_path(error_path, error_path_size, dest_path);
            return false;
        }
        dir = opendir(src_path);
        if (!dir) {
            set_error_path(error_path, error_path_size, src_path);
            return false;
        }
        while ((entry = readdir(dir)) != NULL) {
            char source_child[PATH_MAX];
            char dest_child[PATH_MAX];
            if (options && options->is_cancelled &&
                options->is_cancelled(options->progress_context)) {
                closedir(dir);
                errno = ECANCELED;
                set_error_path(error_path, error_path_size, src_path);
                return false;
            }
            if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
                continue;
            }
            if (!utils_join_path(source_child, sizeof(source_child), src_path, entry->d_name) ||
                !utils_join_path(dest_child, sizeof(dest_child), dest_path, entry->d_name) ||
                !copy_tree(source_child, dest_child, options, error_path, error_path_size,
                            depth + 1)) {
                closedir(dir);
                return false;
            }
        }
        if (closedir(dir) != 0 || chmod(dest_path, source_stat.st_mode & 07777) != 0) {
            set_error_path(error_path, error_path_size, dest_path);
            return false;
        }
        {
            struct utimbuf times = { source_stat.st_atime, source_stat.st_mtime };
            if (utime(dest_path, &times) != 0) {
                set_error_path(error_path, error_path_size, dest_path);
                return false;
            }
        }
        return true;
    }
    if (S_ISREG(source_stat.st_mode)) {
        return copy_regular_file(src_path, dest_path, &source_stat, options,
                                 error_path, error_path_size);
    }
    set_error_path(error_path, error_path_size, src_path);
    errno = ENOTSUP;
    return false;
}

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

        if (!utils_join_path(full_path, sizeof(full_path), path, dp->d_name)) {
            closedir(dir);
            return false;
        }

        struct stat st;
        if (lstat(full_path, &st) == 0) {
            entry->is_dir = S_ISDIR(st.st_mode);
            entry->size = st.st_size;
            entry->mtime = st.st_mtime;
            entry->mode = st.st_mode;
            entry->is_symlink = S_ISLNK(st.st_mode);
        } else {
            entry->is_dir = (dp->d_type == DT_DIR);
            entry->size = 0;
            entry->mtime = 0;
            entry->mode = 0;
            entry->is_symlink = false;
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
    return platform_mkdir(path, 0755) == 0;
#else
    return platform_mkdir(path, 0755) == 0;
#endif
}

bool fs_delete(const char *path) {
    return fs_delete_recursive(path);
}

bool fs_delete_recursive(const char *path) {
    char error_path[PATH_MAX];
    if (is_protected_delete_path(path)) {
        errno = EINVAL;
        return false;
    }
    return remove_tree(path, error_path, sizeof(error_path));
}

bool fs_rename(const char *old_path, const char *new_path) {
    return rename(old_path, new_path) == 0;
}

bool fs_copy(const char *src_path, const char *dest_path) {
    FsCopyOptions options = { false, true, NULL, NULL, NULL };
    char error_path[PATH_MAX];
    return fs_copy_recursive_with_options(src_path, dest_path, &options,
                                          error_path, sizeof(error_path));
}

bool fs_copy_recursive(const char *src_path, const char *dest_path) {
    FsCopyOptions options = { false, true, NULL, NULL, NULL };
    char error_path[PATH_MAX];
    return fs_copy_recursive_with_options(src_path, dest_path, &options,
                                          error_path, sizeof(error_path));
}

bool fs_copy_recursive_with_options(const char *src_path, const char *dest_path,
                                    const FsCopyOptions *options,
                                    char *error_path, size_t error_path_size) {
    struct stat destination_stat;
    bool destination_existed;
    FsCopyOptions defaults = { false, true, NULL, NULL, NULL };
    const FsCopyOptions *effective_options = options ? options : &defaults;

    if (!src_path || !dest_path || !*src_path || !*dest_path ||
        !error_path || error_path_size == 0) {
        errno = EINVAL;
        if (error_path && error_path_size > 0) {
            snprintf(error_path, error_path_size, "%s", dest_path ? dest_path : "");
        }
        return false;
    }
    if (strcmp(src_path, dest_path) == 0) {
        errno = EINVAL;
        set_error_path(error_path, error_path_size, src_path);
        return false;
    }
    if (destination_is_inside_source(src_path, dest_path)) {
        errno = EINVAL;
        set_error_path(error_path, error_path_size, dest_path);
        return false;
    }
    destination_existed = lstat(dest_path, &destination_stat) == 0;
    if (!copy_tree(src_path, dest_path, effective_options, error_path, error_path_size, 0)) {
        if (effective_options->rollback_on_error && !destination_existed) {
            int saved_errno = errno;
            remove_tree(dest_path, NULL, 0);
            errno = saved_errno;
        }
        return false;
    }
    return true;
}

