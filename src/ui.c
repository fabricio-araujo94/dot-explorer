#include "ui.h"
#include "input.h"
#include "config.h"
#include "utils.h"
#include "utils/theme.h"
#include <limits.h>
#include <string.h>
#include <stdio.h>

void ui_init(void) {
    initscr();
    cbreak();
    noecho();
    keypad(stdscr, TRUE);
    curs_set(0);

    if (has_colors()) {
        start_color();
        use_default_colors();
        init_pair(1, COLOR_CYAN, -1);
        init_pair(2, COLOR_WHITE, -1);
        init_pair(3, COLOR_BLACK, COLOR_CYAN);
        init_pair(4, COLOR_WHITE, COLOR_BLUE);
        init_pair(5, COLOR_BLUE, -1);
        init_pair(6, COLOR_GREEN, -1);
        init_pair(7, COLOR_MAGENTA, -1);
        init_pair(8, COLOR_RED, -1);
        init_pair(9, COLOR_CYAN, -1);
    }
}

void ui_cleanup(void) {
    endwin();
}

static void render_status_bar(const AppState *state, int max_y, int max_x) {
    attron(COLOR_PAIR(4));
    mvhline(max_y - 1, 0, ' ', max_x);
    
    mvprintw(max_y - 1, 0, " %.*s | %d items ", max_x - 1,
             state->current_path, state_visible_count(state));
    
    char right_status[128];
    snprintf(right_status, sizeof(right_status), " %s v%s ", APP_NAME, APP_VERSION);
    int len = strlen(right_status);
    if (max_x > len) {
        mvprintw(max_y - 1, max_x - len, "%s", right_status);
    }
    attroff(COLOR_PAIR(4));
}

void ui_render(const AppState *state) {
    int max_y, max_x;
    getmaxyx(stdscr, max_y, max_x);

    erase();

    int list_height = max_y - 1;
    
    for (int i = 0; i < list_height && (i + state->scroll_offset) < state_visible_count(state); ++i) {
        int view_index = i + state->scroll_offset;
        int idx = state_visible_index(state, view_index);
        const FileEntry *entry = &state->dir_list.entries[idx];

        bool is_selected = (idx == state->selected_index);
        
        if (is_selected) {
            attron(COLOR_PAIR(3) | A_BOLD);
            mvhline(i, 0, ' ', max_x);
        } else {
            if (entry->is_dir) {
                attron(COLOR_PAIR(1) | A_BOLD);
            } else {
                attron(COLOR_PAIR(2));
            }
        }

        char dir_prefix = entry->is_dir ? '/' : ' ';
        char sel_prefix = entry->is_selected ? '*' : ' ';
        mvprintw(i, 0, "%c%c %s", sel_prefix, dir_prefix, entry->name);

        if (is_selected) {
            attroff(COLOR_PAIR(3) | A_BOLD);
        } else {
            if (entry->is_dir) {
                attroff(COLOR_PAIR(1) | A_BOLD);
            } else {
                attroff(COLOR_PAIR(2));
            }
        }
    }

    render_status_bar(state, max_y, max_x);

    refresh();
}

void ui_render_filter_prompt(const char *query) {
    int max_y, max_x;
    getmaxyx(stdscr, max_y, max_x);
    mvhline(max_y - 2, 0, ' ', max_x);
    attron(COLOR_PAIR(4));
    mvprintw(max_y - 2, 0, "Filter: [%s]_", query);
    attroff(COLOR_PAIR(4));
    move(max_y - 2, 9 + (int)strlen(query));
    refresh();
}

bool ui_prompt(const char *prompt, char *buffer, size_t buf_size) {
    int max_y, max_x;
    int ret;

    if (!buffer || buf_size < 2) {
        return false;
    }
    getmaxyx(stdscr, max_y, max_x);
    
    int prompt_y = max_y - 2;
    mvhline(prompt_y, 0, ' ', max_x);
    attron(COLOR_PAIR(4));
    mvprintw(prompt_y, 0, "%s", prompt);
    attroff(COLOR_PAIR(4));
    
    echo();
    curs_set(1);

    timeout(-1);
    ret = getnstr(buffer, (int)buf_size - 1);
    timeout(100);
    
    noecho();
    curs_set(0);
    
    return (ret == OK && strlen(buffer) > 0);
}

void ui_show_message(const char *title, const char *message) {
    int max_y, max_x;
    const char *safe_title = title ? title : "Message";
    const char *safe_message = message ? message : "";
    getmaxyx(stdscr, max_y, max_x);

    if (max_y < 3 || max_x < 8) {
        if (max_y > 0 && max_x > 0) {
            mvaddnstr(max_y - 1, 0, safe_message, max_x);
            refresh();
            getch();
        }
        return;
    }

    int box_h = max_y < 10 ? max_y : 10;
    int box_w = max_x < 40 ? max_x : max_x / 2;
    int start_y = (max_y - box_h) / 2;
    int start_x = (max_x - box_w) / 2;

    WINDOW *win = newwin(box_h, box_w, start_y, start_x);
    if (!win) {
        mvaddnstr(max_y - 1, 0, safe_message, max_x);
        refresh();
        getch();
        return;
    }
    box(win, 0, 0);

    mvwaddnstr(win, 0, 2, safe_title, box_w - 4);

    int m_y = 2;
    char *msg_copy = strdup(safe_message);
    if (!msg_copy) {
        delwin(win);
        return;
    }
    char *line = strtok(msg_copy, "\n");
    while (line != NULL && m_y < box_h - 2) {
        mvwaddnstr(win, m_y++, 2, line, box_w - 4);
        line = strtok(NULL, "\n");
    }
    free(msg_copy);

    mvwaddnstr(win, box_h - 2, 2, "[ Press any key to close ]", box_w - 4);

    wrefresh(win);
    wgetch(win);
    delwin(win);
}

static void pane_sync(Pane *pane) {
    snprintf(pane->cwd, sizeof(pane->cwd), "%s", pane->state.current_path);
    pane->selected_index = pane->state.selected_index;
}

static const char *file_extension(const char *name) {
    const char *dot = strrchr(name, '.');
    return dot ? dot : "";
}

static void draw_pane(const Pane *pane, bool active) {
    int list_height = pane->height - 2;
    int visible_count = state_visible_count(&pane->state);
    int content_width = pane->width - 2;

    if (pane->width < 4 || pane->height < 3) return;
    attron(active ? (COLOR_PAIR(3) | A_BOLD) : COLOR_PAIR(2));
    mvaddch(pane->y, pane->x, ACS_ULCORNER);
    mvaddch(pane->y, pane->x + pane->width - 1, ACS_URCORNER);
    mvaddch(pane->y + pane->height - 1, pane->x, ACS_LLCORNER);
    mvaddch(pane->y + pane->height - 1, pane->x + pane->width - 1, ACS_LRCORNER);
    mvhline(pane->y, pane->x + 1, ACS_HLINE, pane->width - 2);
    mvhline(pane->y + pane->height - 1, pane->x + 1, ACS_HLINE, pane->width - 2);
    mvvline(pane->y + 1, pane->x, ACS_VLINE, pane->height - 2);
    mvvline(pane->y + 1, pane->x + pane->width - 1, ACS_VLINE, pane->height - 2);
    attroff(active ? (COLOR_PAIR(3) | A_BOLD) : COLOR_PAIR(2));

    for (int row = 0; row < list_height &&
                       row + pane->state.scroll_offset < visible_count; ++row) {
        int view_index = row + pane->state.scroll_offset;
        int original_index = state_visible_index(&pane->state, view_index);
        const FileEntry *entry = &pane->state.dir_list.entries[original_index];
        int screen_y = pane->y + row + 1;
        bool cursor = original_index == pane->state.selected_index;
        FileTheme theme = get_file_color_and_icon(entry->mode,
                              file_extension(entry->name));
        attr_t attributes = cursor ? (COLOR_PAIR(3) | A_BOLD) :
                    (COLOR_PAIR(theme.color_pair) |
                     (entry->is_dir || theme.color_pair == 6 ? A_BOLD : 0));
        char line[PATH_MAX];

        snprintf(line, sizeof(line), "%c%s %s", entry->is_selected ? '*' : ' ',
             theme.icon, entry->name);
        attron(attributes);
        mvhline(screen_y, pane->x + 1, ' ', content_width);
        mvaddnstr(screen_y, pane->x + 1, line, content_width);
        attroff(attributes);
    }
    attron(active ? (COLOR_PAIR(3) | A_BOLD) : COLOR_PAIR(2));
    mvaddnstr(pane->y + pane->height - 1, pane->x + 2, pane->cwd,
              pane->width - 4);
    attroff(active ? (COLOR_PAIR(3) | A_BOLD) : COLOR_PAIR(2));
}

void ui_dual_init(DualPaneUI *ui) {
    memset(ui, 0, sizeof(*ui));
    state_init(&ui->panes[0].state);
    state_init(&ui->panes[1].state);
    pane_sync(&ui->panes[0]);
    pane_sync(&ui->panes[1]);
    ui->active_pane_index = 0;
    memset(&ui->clipboard, 0, sizeof(ui->clipboard));
    ui->task_destination_pane = -1;
    ui->task_refresh_pending = false;
    task_init(&ui->task);
}

void ui_dual_cleanup(DualPaneUI *ui) {
    task_cleanup(&ui->task);
    clipboard_clear(&ui->clipboard);
    state_cleanup(&ui->panes[0].state);
    state_cleanup(&ui->panes[1].state);
}

void ui_draw(DualPaneUI *ui) {
    int max_y, max_x;
    int left_width;
    getmaxyx(stdscr, max_y, max_x);
    left_width = max_x / 2;

    ui->panes[0].x = 0;
    ui->panes[0].y = 0;
    ui->panes[0].width = left_width;
    ui->panes[0].height = max_y;
    ui->panes[1].x = left_width;
    ui->panes[1].y = 0;
    ui->panes[1].width = max_x - left_width;
    ui->panes[1].height = max_y;
    pane_sync(&ui->panes[0]);
    pane_sync(&ui->panes[1]);

    erase();
    draw_pane(&ui->panes[0], ui->active_pane_index == 0);
    draw_pane(&ui->panes[1], ui->active_pane_index == 1);
    {
        TaskStatus status;
        uint64_t copied, total;
        char error_path[PATH_MAX];
        task_snapshot(&ui->task, &status, &copied, &total,
                      error_path, sizeof(error_path));
        if (status == TASK_RUNNING) {
            int width = max_x > 4 ? max_x - 4 : 1;
            int filled = total > 0 ? (int)((copied * (uint64_t)width) / total) : 0;
            if (filled > width) filled = width;
            mvprintw(max_y - 1, 1, "Copy [");
            mvhline(max_y - 1, 7, '#', filled);
            mvhline(max_y - 1, 7 + filled, '-', width - filled);
            mvprintw(max_y - 1, 8 + width, "] Esc cancel");
        } else if (status == TASK_FAILED) {
            mvprintw(max_y - 1, 1, "Copy failed: %.*s", max_x - 3, error_path);
        } else if (status == TASK_CANCELLED) {
            mvprintw(max_y - 1, 1, "Copy cancelled");
        }
        if (status == TASK_COMPLETED && ui->task_refresh_pending) {
            Pane *destination = &ui->panes[ui->task_destination_pane];
            state_change_dir(&destination->state, ".");
            pane_sync(destination);
            ui->task_refresh_pending = false;
        }
        if (status != TASK_RUNNING && status != TASK_IDLE && ui->task.thread_started) {
            task_reap(&ui->task);
        }
    }
    refresh();
}

void ui_handle_input(DualPaneUI *ui, int ch) {
    Pane *active = &ui->panes[ui->active_pane_index];

    if (task_is_running(&ui->task)) {
        if (ch == 27) task_request_cancel(&ui->task);
        return;
    }

    if (ch == '\t') {
        ui->active_pane_index = 1 - ui->active_pane_index;
        return;
    }
    if (ch == KEY_PASTE && ui->clipboard.count > 0) {
        if (!ui->clipboard.is_cut && ui->clipboard.count == 1) {
            char destination[PATH_MAX];
            const char *source = ui->clipboard.paths[0];
            const char *name = strrchr(source, '/');
            name = name ? name + 1 : source;
            if (utils_join_path(destination, sizeof(destination),
                                active->state.current_path, name) &&
                task_start_copy(&ui->task, source, destination)) {
                ui->task_destination_pane = ui->active_pane_index;
                ui->task_refresh_pending = true;
                return;
            }
        }
        char error_path[PATH_MAX];
        if (!clipboard_apply_operation(&ui->clipboard,
                                       active->state.current_path,
                                       error_path, sizeof(error_path))) {
            ui_show_message("Paste failed", error_path);
        }
        state_change_dir(&active->state, ".");
        pane_sync(active);
        return;
    }
    if (ch != KEY_PASTE) {
        input_handle(&active->state, &ui->clipboard, ch);
    }
    pane_sync(active);
}

