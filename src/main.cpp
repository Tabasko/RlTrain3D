#include "external/raygui.h"
#include "input.h"
#include "state/game_state.h"
#include "ui/ui.h"
#include <raylib.h>
#include "systems/environment.h"

int main(void) {
    SetExitKey(KEY_ESCAPE);
    SetConfigFlags(FLAG_WINDOW_RESIZABLE | FLAG_MSAA_4X_HINT);
    InitWindow(1280, 720, "Trains 3D");
    MaximizeWindow();
    SetTargetFPS(60);

    GuiLoadStyle("style_cyber.rgs");
    Font f = LoadFontEx("assets/fonts/ConsolaMono-Book.ttf", 32, nullptr, 0);
    GuiSetFont(f);
    GuiSetStyle(DEFAULT, TEXT_SIZE, 32);

    Vector3 player = {0.0f, 0.0f, 0.0f};

    EnvironmentCreate();

    while (!gs.app.exit_requested) {
        float delta = GetFrameTime();

        InputFrame in = InputPoll(&gs.camera);
        InputProcess(&in);
        UiUpdate();

        if (IsKeyPressed(KEY_F))
            gs.camera.followTarget = (gs.camera.followTarget == nullptr) ? &player : nullptr;

        gs.camera.update(delta);

        
        BeginDrawing();
        ClearBackground(COL_BG);

        BeginMode3D(gs.camera.cam);
            EnvironmentGroundDraw3D();
        EndMode3D();

        EnvironmentGroundDraw();
        UiDrawToolbar();


        DrawText("WASD move, Middle Mouse look, Mouse Wheel zoom", 20, 20, 20, DARKGRAY);
        DrawText("Press F to toggle follow mode", 20, 50, 20, DARKGRAY);

        EndDrawing();
    }

    CloseWindow();
    return 0;
}
