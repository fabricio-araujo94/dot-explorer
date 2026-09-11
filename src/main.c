#include "state.h"
#include "ui.h"
#include "input.h"
#include <ncurses.h>

int main(void) {
    DualPaneUI dual_pane;

    ui_init();
    ui_dual_init(&dual_pane);

    while (!dual_pane.panes[dual_pane.active_pane_index].state.should_quit) {
        ui_draw(&dual_pane);
        
        int ch = getch();
        ui_handle_input(&dual_pane, ch);
    }

    ui_dual_cleanup(&dual_pane);
    ui_cleanup();

    return 0;
}
