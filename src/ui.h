#ifndef UI_H
#define UI_H

#include "state.h"
#include "task.h"
#include <ncurses.h>

typedef struct {
	AppState state;
	char cwd[PATH_MAX];
	int selected_index;
	int x;
	int y;
	int width;
	int height;
} Pane;

typedef struct {
	Pane panes[2];
	int active_pane_index;
	Task task;
	Clipboard clipboard;
	int task_destination_pane;
	bool task_refresh_pending;
} DualPaneUI;

void ui_init(void);
void ui_cleanup(void);
void ui_render(const AppState *state);
void ui_draw(DualPaneUI *ui);
void ui_dual_init(DualPaneUI *ui);
void ui_dual_cleanup(DualPaneUI *ui);
void ui_handle_input(DualPaneUI *ui, int ch);
void ui_render_filter_prompt(const char *query);
bool ui_prompt(const char *prompt, char *buffer, size_t buf_size);
void ui_show_message(const char *title, const char *message);

#endif // UI_H
