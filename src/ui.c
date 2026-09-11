#include "ui.h"
#include "config.h"
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
    getmaxyx(stdscr, max_y, max_x);
    
    int prompt_y = max_y - 2;
    mvhline(prompt_y, 0, ' ', max_x);
    attron(COLOR_PAIR(4));
    mvprintw(prompt_y, 0, "%s", prompt);
    attroff(COLOR_PAIR(4));
    
    echo();
    curs_set(1);
    
    int ret = getnstr(buffer, buf_size - 1);
    
    noecho();
    curs_set(0);
    
    return (ret == OK && strlen(buffer) > 0);
}

void ui_show_message(const char *title, const char *message) {
    int max_y, max_x;
    getmaxyx(stdscr, max_y, max_x);
    
    int box_h = 10;
    int box_w = max_x / 2;
    int start_y = (max_y - box_h) / 2;
    int start_x = (max_x - box_w) / 2;
    
    WINDOW *win = newwin(box_h, box_w, start_y, start_x);
    box(win, 0, 0);
    
    mvwprintw(win, 0, 2, " %s ", title);
    
    int m_y = 2;
    char *msg_copy = strdup(message);
    char *line = strtok(msg_copy, "\n");
    while (line != NULL && m_y < box_h - 2) {
        mvwprintw(win, m_y++, 2, "%.*s", box_w - 4, line);
        line = strtok(NULL, "\n");
    }
    free(msg_copy);
    
    mvwprintw(win, box_h - 2, 2, "[ Press any key to close ]");
    
    wrefresh(win);
    wgetch(win);
    delwin(win);
}

