#include "colors.h"
#include "events/event_system.h"
#include "external/raygui.h"
#include "input.h"
#include "state/game_state.h"
#include "systems/environment.h"
#include "systems/junction.h"
#include "systems/props.h"
#include "systems/saveload.h"
#include "systems/track.h"
#include "systems/train.h"
#include "ui/ui.h"
#include <iostream>
#include <raylib.h>

int main(void) {

  // RL Init
  SetExitKey(KEY_ESCAPE);
  SetConfigFlags(FLAG_WINDOW_RESIZABLE | FLAG_MSAA_4X_HINT);
  InitWindow(1280, 720, "Trains 3D");
  MaximizeWindow();
  SetTargetFPS(60);

  // Style
  GuiLoadStyle("style_cyber.rgs");
  Font f = LoadFontEx("assets/fonts/ConsolaMono-Book.ttf", 32, nullptr, 0);
  GuiSetFont(f);
  GuiSetStyle(DEFAULT, TEXT_SIZE, 32);

  // Game Init
  InitAudioDevice();      // Initialize audio device
  GameStateInit();
  Vector3 player = {0.0f, 0.0f, 0.0f};
  EnvironmentInit();
  PropsInit();
  TrackSystemInit();
  JunctionSystemInit();
  TrainSystemInit();

  gs.events.emit(EVENT_FILE_OPEN);

  // setup event bus system
  Sound fxArrive = LoadSound("assets/arrive.wav");

  int subId =
      gs.bus.subscribe(_EventType::TRAIN_ARRIVED_STATION, [fxArrive](const _Event &e) {
        PlaySound(fxArrive);
        auto station = std::get<std::string>(e.params.at("stationName"));
        auto track = std::get<int>(e.params.at("track"));
        std::cout << "Train arrived at " << station << " on track " << track
                  << "\n";
      });

  // Example: unsubscribe later
  // bus.unsubscribe(subId);

  // Game Loop
  while (!gs.app.exit_requested) {
    float delta = GetFrameTime();

    gs.events.swap(); // promote last frame's write queue to read, clear write

    // Dispatch queued events ONCE per frame
    gs.bus.dispatch();

    // Input
    InputFrame in = InputPoll(&gs.camera);
    InputProcess(&in);
    UiUpdate();
    SaveLoadUpdate();
    TrackSystemUpdate();
    EnvironmentUpdate();
    PropsUpdate();
    JunctionSystemUpdate();
    TrainSystemUpdate();

    // if (IsKeyPressed(KEY_F))
    //   gs.camera.followTarget =
    //       (gs.camera.followTarget == nullptr) ? &player : nullptr;

    // Update
    gs.camera.update(delta);

    // clang-format off
    BeginDrawing();
      ClearBackground(COL_BG);

      // 3D
      BeginMode3D(gs.camera.cam);
        EnvironmentGroundDraw3D();
        PropsDraw3D();
        TrackSystemDraw3D();
        JunctionSystemDraw3D();
        TrainSystemDraw3D();
      EndMode3D();

      // UI
      TrackSystemDraw2D();
      JunctionSystemDraw2D();
      UiDraw();
      
      DrawText("WASD move, Middle Mouse look, Mouse Wheel zoom", 20, 20, 24, COL_UI_TEXT);
      DrawText("Press F to toggle follow mode", 20, 50, 24, COL_UI_TEXT);
      DrawText(TextFormat("FPS: %d", GetFPS()),GetScreenWidth() - 150,70, 24, COL_UI_TEXT);

    EndDrawing();
    // clang-format on
  }

  EnvironmentDestroy();
  PropsDestroy();
  TrackSystemDestroy();
  JunctionSystemDestroy();
  TrainSystemDestroy();
  CloseWindow();
  return 0;
}
