#ifndef UI_H
#define UI_H

#include "state.h"
#include "task.h"
#include <ncurses.h>

/** One pane's state and current screen geometry. */
typedef struct {
    AppState state;
    int x;
    int y;
    int width;
    int height;
} Pane;

/** Two-pane terminal UI and resources shared between panes. */
typedef struct {
    Pane panes[2];
    int active_pane_index;
    Task task;
    Clipboard clipboard;
    int task_destination_pane;
    bool task_refresh_pending;
} DualPaneUI;

/** Initialize ncurses and terminal modes. On failure, the terminal is restored. */
bool ui_init(void);

/** Restore terminal state after successful ui_init. */
void ui_cleanup(void);

/** Draw both panes and the task status using the active ncurses screen. */
void ui_draw(DualPaneUI *ui);

/** Reconcile completed background tasks and release finished workers. */
void ui_update(DualPaneUI *ui);

/**
 * Initialize both pane states, clipboard state and the task mutex.
 * @return true on success; false if any component fails to initialize.
 * @note On failure, any successfully initialized pane state is cleaned up.
 */
bool ui_dual_init(DualPaneUI *ui);

/** Cancel/reap the task and release resources owned by the dual UI. */
void ui_dual_cleanup(DualPaneUI *ui);

/** Dispatch a key to the active pane; Tab switches panes and Esc cancels a copy. */
void ui_handle_input(DualPaneUI *ui, int ch);

/** Render the filter prompt on the active ncurses screen. */
void ui_render_filter_prompt(const char *query);

/** Read a nonempty prompt response into caller-owned storage. */
bool ui_prompt(const char *prompt, char *buffer, size_t buf_size);

/** Display a modal message; NULL title/message values are treated as defaults. */
void ui_show_message(const char *title, const char *message);

#endif // UI_H
