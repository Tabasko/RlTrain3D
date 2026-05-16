#pragma once
#include "../camera.hpp"
#include "../events/event_bus.h"
#include "../ecs/registry.h"

typedef struct {
    bool exit_requested;
    bool render_grid;
    bool render_terrain;
    bool render_graph_debug;
    bool render_track_debug;
    bool render_ui_left_panel;
    bool render_block_colors;   // color tracks by signal/block state
    bool track_editing;         // true while the user is placing a track line
    bool junction_editing;      // true while the user is placing a junction
    bool erase_editing;         // true while the user is marquee-selecting track to erase
    bool train_placing;         // true while the user is clicking to place a train
    bool signal_placing;        // true while the user is clicking to place a signal
} AppState;

typedef struct {
    bool show_route_manager;
    bool signal_placing;
    bool arc_dir_editing;   // click-to-cycle arc direction mode
} UiState;

typedef struct {
    RPGCamera camera;
    UiState    ui;
    AppState   app;
    EventBus   events;
    Registry   ecs;
} GameState;

extern GameState gs;

void GameStateInit();
