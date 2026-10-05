#include "state.h"
#include "ui.h"
#include "input.h"
#include <locale.h>
#include <ncurses.h>
#include <stdio.h>
#include <stdlib.h>

int main(void) {
    setlocale(LC_ALL, "");
    DualPaneUI dual_pane;

    if (!ui_init()) {
        fputs("Failed to initialize terminal UI.\n", stderr);
        return EXIT_FAILURE;
    }
    if (!ui_dual_init(&dual_pane)) {
        ui_cleanup();
        fputs("Failed to initialize application state.\n", stderr);
        return EXIT_FAILURE;
    }
    timeout(100);

    while (!dual_pane.panes[dual_pane.active_pane_index].state.should_quit) {
        ui_update(&dual_pane);
        ui_draw(&dual_pane);
        
        int ch = getch();
        if (ch != ERR) {
            ui_handle_input(&dual_pane, ch);
        }
    }

    ui_dual_cleanup(&dual_pane);
    ui_cleanup();

    return EXIT_SUCCESS;
}
