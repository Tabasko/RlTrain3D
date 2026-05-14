#pragma once
#include "../rpg_controller.hpp"

typedef struct {
    bool exit_requested;
    bool render_grid;
    bool render_terrain;
    bool render_graph_debug;
    bool render_track_debug;
    bool render_block_colors;   // color tracks by signal/block state
} AppState;

typedef struct {
    bool show_route_manager;
    bool signal_placing;
    bool arc_dir_editing;   // click-to-cycle arc direction mode
} UiState;

typedef struct {
    RPGCamera   camera;
    UiState		    ui;
    AppState       app;
} GameState;

extern GameState gs;
