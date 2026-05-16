#include "colors.h"
#include "external/raygui.h"
#include "input.h"
#include "state/game_state.h"
#include "systems/environment.h"
#include "systems/junction.h"
#include "systems/props.h"
#include "systems/saveload.h"
#include "systems/track.h"
#include "systems/train.h"
#include "systems/signal.h"
#include "ui/ui.h"
#include <raylib.h>

// clang-format off
// Ordered list of simulation systems. main.cpp drives them through ISystem;
// concrete types are not referenced here.
static ISystem *systems[] = {
    &environment_system, 
    &props_system, 
    &track_system,
    &junction_system,    
    &train_system,
    &signal_system,
};
// clang-format on
static constexpr int SYSTEM_COUNT = sizeof(systems) / sizeof(systems[0]);

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

  // Init
  InitAudioDevice();
  GameStateInit();

  // ECS Init
  for (int i = 0; i < SYSTEM_COUNT; i++)
    systems[i]->Init();

  // Auto load saved game
  gs.events.emit(EVENT_FILE_OPEN);

  // Game Loop
  // ---------
  while (!gs.app.exit_requested) {
    float delta = GetFrameTime();

    gs.events.swap(); // promote last frame's write queue to read, clear write

    // Input
    InputFrame in = InputPoll(&gs.camera);
    InputProcess(&in);
    UiUpdate();
    SaveLoadUpdate();

    // ECS Update
    for (int i = 0; i < SYSTEM_COUNT; i++)
      systems[i]->Update();

    gs.camera.update(delta);

    // clang-format off
    BeginDrawing();
      ClearBackground(COL_BG);

      // 3D
      // -----------
      BeginMode3D(gs.camera.cam);

        // ECS Draw3D
        for (int i = 0; i < SYSTEM_COUNT; i++) systems[i]->Draw3D();

      EndMode3D();

      // 2D overlays
      // -----------

      // ECS Draw2D
      for (int i = 0; i < SYSTEM_COUNT; i++) systems[i]->Draw2D();

      UiDraw();

      DrawText("WASD move, Middle Mouse look, Mouse Wheel zoom", 20, 20, 24, COL_UI_TEXT);
      DrawText("Press F to toggle follow mode", 20, 50, 24, COL_UI_TEXT);
      DrawText(TextFormat("FPS: %d", GetFPS()),GetScreenWidth() - 150,70, 24, COL_UI_TEXT);

    EndDrawing();
    // clang-format on
  }

  for (int i = SYSTEM_COUNT - 1; i >= 0; i--)
    systems[i]->Destroy();
  CloseWindow();
  return 0;
}
