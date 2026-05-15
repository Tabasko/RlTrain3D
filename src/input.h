#pragma once
#include "state/game_state.h"
#include "types.h"

struct InputFrame {
    bool key_escape, key_f1, key_f2, key_g, key_x, key_t, key_r, key_f5, key_f6, key_f7;
    bool lmb_pressed, rmb_pressed, mmb_pressed, mmb_down;
    bool lmb_released_mmb;
    Vector2 mouse_screen;
    Vector2 mouse_world;
    float   scroll;
};

InputFrame InputPoll(const RPGCamera *cam);
void       InputProcess(const InputFrame *in);
