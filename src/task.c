#include "task.h"
#include "fs.h"
#include "utils.h"
#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static uint64_t tree_size(const char *path) {
    struct stat st;
    uint64_t total = 0;
    if (lstat(path, &st) != 0) return 0;
    if (!S_ISDIR(st.st_mode)) return S_ISREG(st.st_mode) ? (uint64_t)st.st_size : 0;
    DIR *dir = opendir(path);
    if (!dir) return 0;
    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        char child[PATH_MAX];
        if (!strcmp(entry->d_name, ".") || !strcmp(entry->d_name, "..")) continue;
        if (snprintf(child, sizeof(child), "%s/%s", path, entry->d_name) < (int)sizeof(child)) {
            total += tree_size(child);
        }
    }
    closedir(dir);
    return total;
}

static bool task_progress(uint64_t copied, uint64_t total, void *context) {
    Task *task = context;
    pthread_mutex_lock(&task->mutex);
    if (copied < task->current_file_bytes) {
        task->bytes_copied += task->current_file_bytes;
    }
    task->current_file_bytes = copied;
    if (total > 0 && task->total_bytes == 0) task->total_bytes = total;
    bool cancelled = task->cancel_requested;
    pthread_mutex_unlock(&task->mutex);
    return !cancelled;
}

static bool task_cancelled(void *context) {
    Task *task = context;
    pthread_mutex_lock(&task->mutex);
    bool cancelled = task->cancel_requested;
    pthread_mutex_unlock(&task->mutex);
    return cancelled;
}

static void *copy_worker(void *context) {
    Task *task = context;
    char error_path[PATH_MAX];
    error_path[0] = '\0';
    FsCopyOptions options = { false, true, task_progress, task_cancelled, task };
    bool success = fs_copy_recursive_with_options(task->source, task->destination,
                                                  &options, error_path,
                                                  sizeof(error_path));
    pthread_mutex_lock(&task->mutex);
    snprintf(task->error_path, sizeof(task->error_path), "%s", error_path);
    task->bytes_copied += task->current_file_bytes;
    task->current_file_bytes = 0;
    if (task->cancel_requested || (errno == ECANCELED && !success)) {
        task->status = TASK_CANCELLED;
    } else {
        task->status = success ? TASK_COMPLETED : TASK_FAILED;
    }
    pthread_mutex_unlock(&task->mutex);
    return NULL;
}

void task_init(Task *task) {
    memset(task, 0, sizeof(*task));
    pthread_mutex_init(&task->mutex, NULL);
    task->status = TASK_IDLE;
}

void task_cleanup(Task *task) {
    if (task_is_running(task)) {
        task_request_cancel(task);
    }
    task_reap(task);
    pthread_mutex_destroy(&task->mutex);
}

void task_reap(Task *task) {
    pthread_t thread;

    if (!task) return;
    pthread_mutex_lock(&task->mutex);
    if (!task->thread_started) {
        pthread_mutex_unlock(&task->mutex);
        return;
    }
    thread = task->thread;
    pthread_mutex_unlock(&task->mutex);

    pthread_join(thread, NULL);

    pthread_mutex_lock(&task->mutex);
    if (task->thread_started && pthread_equal(task->thread, thread)) {
        task->thread_started = false;
    }
    pthread_mutex_unlock(&task->mutex);
}

bool task_start_copy(Task *task, const char *source, const char *destination) {
    struct stat st;
    bool previous_thread;

    if (!task || !source || !destination ||
        strlen(source) >= sizeof(task->source) || strlen(destination) >= sizeof(task->destination) ||
        stat(source, &st) != 0) {
        errno = EINVAL;
        return false;
    }

    pthread_mutex_lock(&task->mutex);
    previous_thread = task->thread_started;
    bool running = task->status == TASK_RUNNING;
    pthread_mutex_unlock(&task->mutex);
    if (running) {
        errno = EBUSY;
        return false;
    }
    if (previous_thread) {
        task_reap(task);
    }

    pthread_mutex_lock(&task->mutex);
    snprintf(task->source, sizeof(task->source), "%s", source);
    snprintf(task->destination, sizeof(task->destination), "%s", destination);
    task->total_bytes = tree_size(source);
    task->bytes_copied = 0;
    task->current_file_bytes = 0;
    task->error_path[0] = '\0';
    task->cancel_requested = false;
    task->status = TASK_RUNNING;
    pthread_mutex_unlock(&task->mutex);
    if (pthread_create(&task->thread, NULL, copy_worker, task) != 0) {
        pthread_mutex_lock(&task->mutex);
        task->status = TASK_FAILED;
        pthread_mutex_unlock(&task->mutex);
        return false;
    }
    pthread_mutex_lock(&task->mutex);
    task->thread_started = true;
    pthread_mutex_unlock(&task->mutex);
    return true;
}

void task_request_cancel(Task *task) {
    if (!task) return;
    pthread_mutex_lock(&task->mutex);
    task->cancel_requested = true;
    pthread_mutex_unlock(&task->mutex);
}

bool task_is_running(Task *task) {
    bool running;
    if (!task) return false;
    pthread_mutex_lock(&task->mutex);
    running = task->status == TASK_RUNNING;
    pthread_mutex_unlock(&task->mutex);
    return running;
}

void task_snapshot(Task *task, TaskStatus *status, uint64_t *bytes_copied,
                   uint64_t *total_bytes, char *error_path, size_t error_size) {
    pthread_mutex_lock(&task->mutex);
    if (status) *status = task->status;
    if (bytes_copied) *bytes_copied = task->bytes_copied + task->current_file_bytes;
    if (total_bytes) *total_bytes = task->total_bytes;
    if (error_path && error_size > 0) snprintf(error_path, error_size, "%s", task->error_path);
    pthread_mutex_unlock(&task->mutex);
}
