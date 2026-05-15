#define RAYGUI_IMPLEMENTATION
#include "ui.h"
#include "../external/raygui.h"
#include "../state/game_state.h"
#include <stdio.h>
#include <string.h>

#define BAR_INSET ((int)(4 * UI_SCALE))

static const Color COL_BTN_BG     = {18,  28,  52, 255};
static const Color COL_BTN_HOVER  = {30,  48,  84, 255};
static const Color COL_BTN_BORDER = {40,  65, 105, 255};

static UI_ButtonDef UI_CONFIRM_PANEL_EXIT[] = {
    {"Yes", {50, 130, 180, 255}, {70,  160, 220, 255}},
    {"No",  {180, 100, 50, 255}, {220, 130, 70,  255}},
};

bool UiMouseInToolbar(void) {
    return GetMousePosition().y < TOOLBAR_H;
}

bool UiMouseInPanel(void) {
    Vector2 mp = GetMousePosition();
    return mp.x < PANEL_W && mp.y >= TOOLBAR_H;
}

void UiDraw(void) {
    UiDrawToolbar();
    if (gs.app.render_ui_left_panel)
        UiDrawLeftPanel();
}

void UiDrawToolbar(void) {
    int sw    = GetScreenWidth();
    int btn_h = TOOLBAR_H - BAR_INSET * 2;
    int btn_w = (int)(120 * UI_SCALE);

    DrawRectangle(0, 0, sw, TOOLBAR_H, Color{14, 20, 40, 240});
    DrawLineEx(Vector2{0, (float)TOOLBAR_H - 1}, Vector2{(float)sw, (float)TOOLBAR_H - 1},
               1.0f, Color{40, 65, 105, 200});

    float x = (float)(sw - btn_w - BAR_INSET);
    if (GuiButton(Rectangle{x, (float)BAR_INSET, (float)btn_w, (float)btn_h}, "Exit Game"))
        gs.app.exit_requested = true;
}

void UiDrawLeftPanel(void) {
    int sh    = GetScreenHeight();
    int btn_w = PANEL_W - 2 * PANEL_PAD;

    DrawRectangle(0, TOOLBAR_H, PANEL_W, sh - TOOLBAR_H, Color{14, 20, 40, 220});
    DrawLineEx(Vector2{(float)PANEL_W, (float)TOOLBAR_H}, Vector2{(float)PANEL_W, (float)sh},
               1.0f, Color{40, 65, 105, 200});

    static const struct { int icon; const char *label; } tools[] = {
        { ICON_CURSOR_POINTER, "Select"   },
        { ICON_PENCIL,         "Track"    },
        { ICON_ARROW_RIGHT,    "Junction" },
        { ICON_RUBBER,         "Erase"    },
        { ICON_ROTATE,         "Rotate"   },
        { ICON_GRID,           "Terrain"  },
        { ICON_GEAR,           "Settings" },
    };
    static const int tool_count = sizeof(tools) / sizeof(tools[0]);

    int y = TOOLBAR_H + PANEL_PAD;
    for (int i = 0; i < tool_count; i++) {
        Rectangle rect = { (float)PANEL_PAD, (float)y, (float)btn_w, (float)PANEL_ITEM_H };
        if (GuiButton(rect, GuiIconText(tools[i].icon, tools[i].label))) {
            if (i == 1) gs.events.emit(EVENT_START_TRACK_EDIT);
            if (i == 2) gs.events.emit(EVENT_START_JUNCTION_EDIT);
        }
        y += PANEL_ITEM_H + PANEL_PAD / 2;
    }
}


// ---------------------------------------------------------------------------
// Confirmation panel
// ---------------------------------------------------------------------------

static UI_ConfirmPanel _panel = {};

void UI_ShowConfirm(const char *title, const char *message,
                    UI_ButtonDef *buttonDefs, int count) {
    _panel.active      = true;
    _panel.result      = UI_RESULT_NONE;
    _panel.buttonCount = (count > UI_MAX_BUTTONS) ? UI_MAX_BUTTONS : count;

    strncpy(_panel.title,   title,   UI_MAX_LABEL_LEN - 1);
    strncpy(_panel.message, message, 255);

    for (int i = 0; i < _panel.buttonCount; i++)
        _panel.buttons[i] = buttonDefs[i];
}


void UiDraw_old(void) {
    if (!_panel.active) return;

    int sw = GetScreenWidth();
    int sh = GetScreenHeight();

    int panelW = 420, panelH = 200;
    int panelX = (sw - panelW) / 2;
    int panelY = (sh - panelH) / 2;

    int btnW      = 110, btnH = 36;
    int btnSpacing = 12;
    int totalBtnW  = _panel.buttonCount * btnW + (_panel.buttonCount - 1) * btnSpacing;
    int btnStartX  = panelX + (panelW - totalBtnW) / 2;
    int btnY       = panelY + panelH - btnH - 20;

    Vector2 mouse = GetMousePosition();

    DrawRectangle(0, 0, sw, sh, Color{0, 0, 0, 120});
    DrawRectangleRounded(Rectangle{(float)panelX, (float)panelY, (float)panelW, (float)panelH},
                         0.1f, 8, Color{30, 30, 40, 255});
    DrawRectangleRoundedLines(Rectangle{(float)panelX, (float)panelY, (float)panelW, (float)panelH},
                               0.1f, 8, Color{80, 80, 100, 255});

    DrawText(_panel.title,   panelX + 20, panelY + 18, 20, WHITE);
    DrawLine(panelX + 10, panelY + 48, panelX + panelW - 10, panelY + 48, Color{80, 80, 100, 255});
    DrawText(_panel.message, panelX + 20, panelY + 62, 16, Color{200, 200, 210, 255});

    for (int i = 0; i < _panel.buttonCount; i++) {
        int bx = btnStartX + i * (btnW + btnSpacing);
        Rectangle btnRect = {(float)bx, (float)btnY, (float)btnW, (float)btnH};
        bool hovered = CheckCollisionPointRec(mouse, btnRect);

        Color col = hovered ? _panel.buttons[i].hoverColor : _panel.buttons[i].color;
        DrawRectangleRounded(btnRect, 0.2f, 6, col);
        DrawRectangleRoundedLines(btnRect, 0.2f, 6, Color{255, 255, 255, 40});

        int txtW = MeasureText(_panel.buttons[i].label, 16);
        DrawText(_panel.buttons[i].label,
                 bx + (btnW - txtW) / 2, btnY + (btnH - 16) / 2,
                 16, WHITE);

        if (hovered && IsMouseButtonPressed(MOUSE_LEFT_BUTTON)) {
            _panel.result = i;
            _panel.active = false;
        }
    }
}

int  UI_GetResult(void) { return _panel.result; }
void UI_Dismiss(void)   { _panel.active = false; _panel.result = UI_RESULT_NONE; }
bool UI_IsActive(void)  { return _panel.active; }

// ---------------------------------------------------------------------------
// Per-frame update
// ---------------------------------------------------------------------------

static UI_Dialog_Type s_dialog_type = (UI_Dialog_Type)-1;

void UiUpdate(void) {
    if (!UI_IsActive()) {
        // ESC is consumed by the track system while editing; only open exit dialog otherwise
        if (IsKeyPressed(KEY_ESCAPE) && !gs.app.track_editing && !gs.app.junction_editing) {
            s_dialog_type = CONFIRM_EXIT;
            UI_ShowConfirm("Exit Game", "Do you want to exit the Game.",
                           UI_CONFIRM_PANEL_EXIT, 2);
        }
        if (IsKeyPressed(KEY_TAB))
            gs.events.emit(EVENT_TOGGLE_LEFT_PANEL);
    }

    // Consume events from last frame
    if (gs.events.has(EVENT_TOGGLE_LEFT_PANEL))
        gs.app.render_ui_left_panel = !gs.app.render_ui_left_panel;

    int result = UI_GetResult();
    if (result != UI_RESULT_NONE) {
        if (s_dialog_type == CONFIRM_EXIT && result == UI_YES)
            gs.app.exit_requested = true;
        TraceLog(LOG_INFO, "Button %d pressed", result);
        UI_Dismiss();
    }
}
