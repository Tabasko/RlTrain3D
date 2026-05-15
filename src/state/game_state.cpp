#include "game_state.h"

GameState gs = {
    .app = { .render_ui_left_panel = true },
};

// Separate from the aggregate initializer because init() is a method call.
// Must run before the first frame; called from main before the game loop.
void GameStateInit() {
    gs.events.init();
}
