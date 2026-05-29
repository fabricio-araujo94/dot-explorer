#include "state.h"
#include "ui.h"
#include "input.h"
#include <ncurses.h>

int main(void) {
    AppState state;
    state_init(&state);

    ui_init();

    while (!state.should_quit) {
        ui_render(&state);
        
        int ch = getch();
        input_handle(&state, ch);
    }

    ui_cleanup();
    state_cleanup(&state);

    return 0;
}
