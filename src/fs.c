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
    size_t seg_len;

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
    seg_len = (size_t)((path + length) - name);
    return (seg_len == 1 && name[0] == '.') ||
           (seg_len == 2 && name[0] == '.' && name[1] == '.');
}

static bool parent_is_writable(const char *path) {
    char parent[PATH_MAX];
    const char *separator;

    if (strlen(path) >= sizeof(parent)) {
        errno = ENAMETOOLONG;
        return false;
    }
    strcpy(parent, path);
    separator = path_find_last_separator(parent);
    if (!separator) {
        return access(".", W_OK | X_OK) == 0;
    }
    if (separator == parent) {
        parent[1] = '\0';
    } else {
        parent[separator - parent] = '\0';
    }
    return access(parent, W_OK | X_OK) == 0;
}

static bool path_separator(char character) {
    return character == '/' || character == '\\';
}

static void trim_trailing_separators(char *path) {
    size_t length = strlen(path);
    while (length > 1 && path_separator(path[length - 1])) {
#ifdef _WIN32
        if (length == 3 && path[1] == ':') break;
#endif
        path[--length] = '\0';
    }
}

static bool take_last_path_component(char *path, char *component,
                                     size_t component_size) {
    const char *separator;
    size_t component_length;

    trim_trailing_separators(path);
    if (strcmp(path, ".") == 0 || strcmp(path, "..") == 0) {
        errno = ENOENT;
        return false;
    }
    separator = path_find_last_separator(path);
    if (!separator) {
        component_length = strlen(path);
        if (component_length >= component_size) {
            errno = ENAMETOOLONG;
            return false;
        }
        memcpy(component, path, component_length + 1);
        strcpy(path, ".");
        return true;
    }
    component_length = strlen(separator + 1);
    if (component_length >= component_size) {
        errno = ENAMETOOLONG;
        return false;
    }
    memcpy(component, separator + 1, component_length + 1);
    if (separator == path) {
        path[1] = '\0';
#ifdef _WIN32
    } else if (separator == path + 2 && path[1] == ':') {
        path[3] = '\0';
#endif
    } else {
        *((char *)separator) = '\0';
    }
    return true;
}

static bool prepend_path_component(char *suffix, size_t suffix_size,
                                   const char *component) {
    char combined[PATH_MAX];

    if (!*suffix) {
        int written = snprintf(suffix, suffix_size, "%s", component);
        if (written < 0 || (size_t)written >= suffix_size) {
            errno = ENAMETOOLONG;
            return false;
        }
        return true;
    }
    if (!path_join(combined, sizeof(combined), component, suffix)) {
        return false;
    }
    if (strlen(combined) >= suffix_size) {
        errno = ENAMETOOLONG;
        return false;
    }
    strcpy(suffix, combined);
    return true;
}

static void remove_last_path_component(char *path) {
    const char *separator;

    trim_trailing_separators(path);
    if (strcmp(path, "/") == 0 || strcmp(path, "\\") == 0) return;
#ifdef _WIN32
    if (strlen(path) == 3 && path[1] == ':' && path_separator(path[2])) return;
#endif
    separator = path_find_last_separator(path);
    if (!separator) {
        strcpy(path, ".");
    } else if (separator == path) {
        path[1] = '\0';
#ifdef _WIN32
    } else if (separator == path + 2 && path[1] == ':') {
        path[3] = '\0';
#endif
    } else {
        *((char *)separator) = '\0';
    }
}

static bool append_normalized_suffix(char *path, size_t path_size,
                                     const char *suffix) {
    const char *component = suffix;

    while (*component) {
        char name[PATH_MAX];
        char joined[PATH_MAX];
        const char *start;
        size_t length;

        while (path_separator(*component)) component++;
        if (!*component) break;
        start = component;
        while (*component && !path_separator(*component)) component++;
        length = (size_t)(component - start);
        if (length == 1 && start[0] == '.') continue;
        if (length == 2 && start[0] == '.' && start[1] == '.') {
            remove_last_path_component(path);
            continue;
        }
        if (length >= sizeof(name)) {
            errno = ENAMETOOLONG;
            return false;
        }
        memcpy(name, start, length);
        name[length] = '\0';
        if (!path_join(joined, sizeof(joined), path, name)) {
            return false;
        }
        if (strlen(joined) >= path_size) {
            errno = ENAMETOOLONG;
            return false;
        }
        strcpy(path, joined);
    }
    return true;
}

static bool resolve_destination_path(const char *destination, char *resolved,
                                     size_t resolved_size) {
    char candidate[PATH_MAX];
    char suffix[PATH_MAX] = "";
    char component[PATH_MAX];
    struct stat st;
    size_t length;

    length = strlen(destination);
    if (length >= sizeof(candidate)) {
        errno = ENAMETOOLONG;
        return false;
    }
    memcpy(candidate, destination, length + 1);

    while (stat(candidate, &st) != 0) {
        if (errno != ENOENT && errno != ENOTDIR) {
            return false;
        }
        if (!take_last_path_component(candidate, component, sizeof(component)) ||
            !prepend_path_component(suffix, sizeof(suffix), component)) {
            return false;
        }
    }
    if (!platform_realpath(candidate, resolved, resolved_size)) {
        return false;
    }
    return append_normalized_suffix(resolved, resolved_size, suffix);
}

static int destination_is_inside_source(const char *source, const char *destination,
                                        const FsCopyOptions *options) {
    struct stat source_stat;
    char source_real[PATH_MAX];
    char destination_real[PATH_MAX];
    size_t source_length;
    size_t destination_length;
    int stat_result;

    stat_result = options && options->follow_symlinks ?
                  stat(source, &source_stat) : lstat(source, &source_stat);
    if (stat_result != 0 || !S_ISDIR(source_stat.st_mode)) {
        return 0;
    }
    if (!platform_realpath(source, source_real, sizeof(source_real)) ||
        !resolve_destination_path(destination, destination_real,
                                  sizeof(destination_real))) {
        return -1;
    }
    source_length = strlen(source_real);
    destination_length = strlen(destination_real);
    if (destination_length < source_length ||
        strncmp(destination_real, source_real, source_length) != 0) {
        return 0;
    }
    return destination_length == source_length ||
           path_separator(source_real[source_length - 1]) ||
           path_separator(destination_real[source_length]);
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
        if (!path_join(child, sizeof(child), path, entry->d_name) ||
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

static bool copy_file_contents(const char *src_path, const char *dest_path,
                               const struct stat *source_stat,
                               const FsCopyCallbacks *callbacks,
                               char *error_path, size_t error_path_size) {
    FILE *src;
    FILE *dest;
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
        if (callbacks && callbacks->is_cancelled &&
            callbacks->is_cancelled(callbacks->progress_context)) {
            errno = ECANCELED;
            success = false;
            break;
        }
        if (fwrite(buffer, 1, bytes, dest) != bytes) {
            success = false;
            break;
        }
        copied += bytes;
        if (callbacks && callbacks->progress &&
            !callbacks->progress(copied, (uint64_t)source_stat->st_size,
                                 callbacks->progress_context)) {
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
    if (!success) {
        set_error_path(error_path, error_path_size, dest_path);
    }
    return success;
}

static bool copy_file_metadata(const char *dest_path,
                               const struct stat *source_stat,
                               char *error_path, size_t error_path_size) {
    struct utimbuf times = { source_stat->st_atime, source_stat->st_mtime };

    if (chmod(dest_path, source_stat->st_mode & 07777) != 0 ||
        utime(dest_path, &times) != 0) {
        set_error_path(error_path, error_path_size, dest_path);
        return false;
    }
    return true;
}

static bool copy_regular_file(const char *src_path, const char *dest_path,
                              const struct stat *source_stat,
                              const FsCopyCallbacks *callbacks,
                              char *error_path, size_t error_path_size) {
    if (!copy_file_contents(src_path, dest_path, source_stat, callbacks,
                            error_path, error_path_size) ||
        !copy_file_metadata(dest_path, source_stat, error_path, error_path_size)) {
        return false;
    }
    if (callbacks && callbacks->file_complete) {
        callbacks->file_complete(callbacks->progress_context);
    }
    return true;
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
                      const FsCopyCallbacks *callbacks,
                      char *error_path, size_t error_path_size,
                      unsigned int depth);

static bool create_copy_directory(const char *dest_path, mode_t source_mode,
                                  char *error_path, size_t error_path_size) {
    mode_t temp_mode = (source_mode & 07777) | S_IRWXU;

    if (platform_mkdir(dest_path, temp_mode) != 0 && errno != EEXIST) {
        set_error_path(error_path, error_path_size, dest_path);
        return false;
    }
    if (access(dest_path, W_OK | X_OK) != 0) {
        set_error_path(error_path, error_path_size, dest_path);
        return false;
    }
    return true;
}

static bool copy_directory_children(const char *src_path, const char *dest_path,
                                    const FsCopyOptions *options,
                                    const FsCopyCallbacks *callbacks,
                                    char *error_path, size_t error_path_size,
                                    unsigned int depth) {
    DIR *dir = opendir(src_path);
    struct dirent *entry;

    if (!dir) {
        set_error_path(error_path, error_path_size, src_path);
        return false;
    }
    while ((entry = readdir(dir)) != NULL) {
        char source_child[PATH_MAX];
        char dest_child[PATH_MAX];

        if (callbacks && callbacks->is_cancelled &&
            callbacks->is_cancelled(callbacks->progress_context)) {
            closedir(dir);
            errno = ECANCELED;
            set_error_path(error_path, error_path_size, src_path);
            return false;
        }
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
            continue;
        }
        if (!path_join(source_child, sizeof(source_child), src_path, entry->d_name) ||
            !path_join(dest_child, sizeof(dest_child), dest_path, entry->d_name) ||
            !copy_tree(source_child, dest_child, options, callbacks,
                       error_path, error_path_size, depth + 1)) {
            closedir(dir);
            return false;
        }
    }
    if (closedir(dir) != 0) {
        set_error_path(error_path, error_path_size, dest_path);
        return false;
    }
    return true;
}

static bool copy_tree(const char *src_path, const char *dest_path,
                      const FsCopyOptions *options,
                      const FsCopyCallbacks *callbacks,
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
        if (!create_copy_directory(dest_path, source_stat.st_mode,
                                   error_path, error_path_size) ||
            !copy_directory_children(src_path, dest_path, options, callbacks,
                                     error_path, error_path_size, depth)) {
            return false;
        }
        if (chmod(dest_path, source_stat.st_mode & 07777) != 0) {
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
        return copy_regular_file(src_path, dest_path, &source_stat, callbacks,
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
    if (!list->entries) {
        list->capacity = 0;
    }
}

void fs_free_dir_list(DirectoryList *list) {
    if (list->entries) {
        free(list->entries);
        list->entries = NULL;
    }
    list->count = 0;
    list->capacity = 0;
}

static int compare_entries_common(const FileEntry *entryA, const FileEntry *entryB) {
    bool a_is_dotdot = (strcmp(entryA->name, "..") == 0);
    bool b_is_dotdot = (strcmp(entryB->name, "..") == 0);
    if (a_is_dotdot && b_is_dotdot) return 0;
    if (a_is_dotdot) return -1;
    if (b_is_dotdot) return 1;

    if (entryA->is_dir && !entryB->is_dir) return -1;
    if (!entryA->is_dir && entryB->is_dir) return 1;

    return 0;
}

static int compare_by_name(const void *a, const void *b) {
    const FileEntry *entryA = (const FileEntry *)a;
    const FileEntry *entryB = (const FileEntry *)b;
    int common = compare_entries_common(entryA, entryB);
    if (common != 0) return common;
    return strcasecmp(entryA->name, entryB->name);
}

static int compare_by_size(const void *a, const void *b) {
    const FileEntry *entryA = (const FileEntry *)a;
    const FileEntry *entryB = (const FileEntry *)b;
    int common = compare_entries_common(entryA, entryB);
    if (common != 0) return common;
    if (entryA->size > entryB->size) return -1;
    if (entryA->size < entryB->size) return 1;
    return strcasecmp(entryA->name, entryB->name);
}

static int compare_by_date(const void *a, const void *b) {
    const FileEntry *entryA = (const FileEntry *)a;
    const FileEntry *entryB = (const FileEntry *)b;
    int common = compare_entries_common(entryA, entryB);
    if (common != 0) return common;
    if (entryA->mtime > entryB->mtime) return -1;
    if (entryA->mtime < entryB->mtime) return 1;
    return strcasecmp(entryA->name, entryB->name);
}

typedef int (*EntryComparator)(const void *, const void *);

typedef struct {
    SortType sort_type;
    EntryComparator comparator;
} SortComparator;

static const SortComparator sort_comparators[] = {
    { SORT_NAME, compare_by_name },
    { SORT_SIZE, compare_by_size },
    { SORT_DATE, compare_by_date }
};

static EntryComparator find_sort_comparator(SortType sort_type) {
    for (size_t i = 0; i < sizeof(sort_comparators) / sizeof(sort_comparators[0]); ++i) {
        if (sort_comparators[i].sort_type == sort_type) {
            return sort_comparators[i].comparator;
        }
    }
    return compare_by_name;
}

void fs_sort_dir_list(DirectoryList *list, SortType sort_type) {
    if (!list || list->count <= 1 || !list->entries) {
        return;
    }
    EntryComparator comparator = find_sort_comparator(sort_type);
    qsort(list->entries, list->count, sizeof(FileEntry), comparator);
}

static bool populate_file_entry(FileEntry *entry, const struct dirent *directory_entry,
                                const char *directory_path) {
    char full_path[PATH_MAX];
    struct stat st;

    strncpy(entry->name, directory_entry->d_name, sizeof(entry->name) - 1);
    entry->name[sizeof(entry->name) - 1] = '\0';
    entry->is_selected = false;

    if (!path_join(full_path, sizeof(full_path), directory_path,
                   directory_entry->d_name)) {
        return false;
    }
    if (lstat(full_path, &st) == 0) {
        entry->is_dir = S_ISDIR(st.st_mode);
        entry->size = st.st_size;
        entry->mtime = st.st_mtime;
        entry->mode = st.st_mode;
        entry->is_symlink = S_ISLNK(st.st_mode);
    } else {
        if (directory_entry->d_type == DT_UNKNOWN) {
            return false;
        }
        entry->is_dir = directory_entry->d_type == DT_DIR;
        entry->size = 0;
        entry->mtime = 0;
        entry->mode = 0;
        entry->is_symlink = false;
    }
    return true;
}

bool fs_read_dir(const char *path, DirectoryList *list) {
    DIR *dir = opendir(path);
    if (!dir) {
        return false;
    }

    list->count = 0;
    if (list->capacity == 0 || !list->entries) {
        list->capacity = 128;
        list->entries = (FileEntry *)malloc(list->capacity * sizeof(FileEntry));
        if (!list->entries) {
            list->capacity = 0;
            closedir(dir);
            errno = ENOMEM;
            return false;
        }
    }

    struct dirent *dp;

    while ((dp = readdir(dir)) != NULL) {
        if (strcmp(dp->d_name, ".") == 0) continue;
        
        if (list->count >= list->capacity) {
                size_t new_capacity = list->capacity == 0 ? 128 : list->capacity * 2;
                FileEntry *new_entries = (FileEntry *)realloc(
                    list->entries, new_capacity * sizeof(FileEntry));
            if (!new_entries) {
                closedir(dir);
                errno = ENOMEM;
                return false;
            }
            list->entries = new_entries;
            list->capacity = new_capacity;
        }

        FileEntry *entry = &list->entries[list->count];
        if (!populate_file_entry(entry, dp, path)) {
            int error = errno;
            closedir(dir);
            errno = error;
            return false;
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

bool fs_copy_recursive(const char *src_path, const char *dest_path) {
    FsCopyOptions options = { false, true };
    char error_path[PATH_MAX];
    return fs_copy_recursive_with_options(src_path, dest_path, &options, NULL,
                                          error_path, sizeof(error_path));
}

bool fs_copy_recursive_with_options(const char *src_path, const char *dest_path,
                                    const FsCopyOptions *options,
                                    const FsCopyCallbacks *callbacks,
                                    char *error_path, size_t error_path_size) {
    struct stat destination_stat;
    bool destination_existed;
    int destination_containment;
    FsCopyOptions defaults = { false, true };
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
    destination_containment = destination_is_inside_source(
        src_path, dest_path, effective_options);
    if (destination_containment < 0) {
        int saved_errno = errno;
        set_error_path(error_path, error_path_size, dest_path);
        errno = saved_errno;
        return false;
    }
    if (destination_containment > 0) {
        errno = EINVAL;
        set_error_path(error_path, error_path_size, dest_path);
        return false;
    }
    destination_existed = lstat(dest_path, &destination_stat) == 0;
    if (!copy_tree(src_path, dest_path, effective_options, callbacks,
                   error_path, error_path_size, 0)) {
        if (effective_options->rollback_on_error && !destination_existed) {
            int saved_errno = errno;
            remove_tree(dest_path, NULL, 0);
            errno = saved_errno;
        }
        return false;
    }
    return true;
}

