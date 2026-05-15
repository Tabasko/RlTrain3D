#include "types.h"
#include "input.h"
#include "state/game_state.h"
#include <raylib.h>

void InputProcess(const InputFrame *in) {
    if (in->key_f1) gs.app.render_grid         = !gs.app.render_grid;
    if (in->key_f2) gs.app.render_ui_left_panel         = !gs.app.render_ui_left_panel;
    if (in->key_f5) gs.app.render_graph_debug  = !gs.app.render_graph_debug;
    if (in->key_f6) gs.app.render_track_debug  = !gs.app.render_track_debug;
    if (in->key_f7) gs.app.render_block_colors = !gs.app.render_block_colors;
    if (in->key_t)  gs.events.emit(EVENT_START_TRACK_EDIT);
    if (in->key_escape)  gs.app.exit_requested      = true;
}

InputFrame InputPoll(const RPGCamera *cam) {
    InputFrame in = {};
    in.key_escape  = IsKeyPressed(KEY_ESCAPE);
    in.key_x       = IsKeyPressed(KEY_X);
    in.key_f1      = IsKeyPressed(KEY_F1);
    in.key_f2      = IsKeyPressed(KEY_F2);
    in.key_f5      = IsKeyPressed(KEY_F5);
    in.key_f6      = IsKeyPressed(KEY_F6);
    in.key_f7      = IsKeyPressed(KEY_F7);
    in.key_t       = IsKeyPressed(KEY_T);

    in.lmb_pressed  = IsMouseButtonPressed(MOUSE_BUTTON_LEFT);
    in.mmb_down     = IsMouseButtonDown(MOUSE_BUTTON_MIDDLE);
    in.mouse_screen = GetMousePosition();
    in.scroll       = GetMouseWheelMove();

    return in;
}
