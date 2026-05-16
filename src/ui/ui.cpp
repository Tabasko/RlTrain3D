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
    UiDrawRightPanel();
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
        { ICON_CURSOR_POINTER, "Select"    },
        { ICON_PENCIL,         "Track"     },
        { ICON_ARROW_RIGHT,    "Junction"  },
        { ICON_RUBBER,         "Erase"     },
        { ICON_ROTATE,         "Rotate"    },
        { ICON_GRID,           "Terrain"   },
        { ICON_PLAYER,         "Add Train" },
        { ICON_ALARM,          "Signal" },
        { ICON_GEAR,           "Settings"  },
    };
    static const int tool_count = sizeof(tools) / sizeof(tools[0]);

    int y = TOOLBAR_H + PANEL_PAD;
    for (int i = 0; i < tool_count; i++) {
        Rectangle rect = { (float)PANEL_PAD, (float)y, (float)btn_w, (float)PANEL_ITEM_H };
        if (GuiButton(rect, GuiIconText(tools[i].icon, tools[i].label))) {
            if (i == 1) gs.events.emit(EVENT_START_TRACK_EDIT);
            if (i == 2) gs.events.emit(EVENT_START_JUNCTION_EDIT);
            if (i == 3) gs.events.emit(EVENT_START_ERASE_EDIT);
            if (i == 6) gs.events.emit(EVENT_START_TRAIN_PLACE);
            if (i == 7) gs.events.emit(EVENT_START_SIGNAL_PLACE);
        }
        y += PANEL_ITEM_H + PANEL_PAD / 2;
    }
}

void UiDrawRightPanel(void) {
    int sw    = GetScreenWidth();
    int sh    = GetScreenHeight();
    int btn_w = PANEL_W - 2 * PANEL_PAD;
    int panel_x = sw - PANEL_W;

    // DrawRectangle(panel_x, TOOLBAR_H, PANEL_W, sh - TOOLBAR_H, Color{14, 20, 40, 220});
    // DrawLineEx(Vector2{(float)panel_x, (float)TOOLBAR_H}, Vector2{(float)panel_x, (float)sh},
    //            1.0f, Color{40, 65, 105, 200});

    static const struct { int icon; const char *label; } tools[] = {
        { ICON_FILE_OPEN, "Load" },
        { ICON_FILE_SAVE, "Save" },
    };
    static const int tool_count = sizeof(tools) / sizeof(tools[0]);

    int y = TOOLBAR_H + PANEL_PAD + PANEL_PAD;
    for (int i = 0; i < tool_count; i++) {
        Rectangle rect = { (float)(panel_x + PANEL_PAD), (float)y, (float)btn_w, (float)PANEL_ITEM_H };
        if (GuiButton(rect, GuiIconText(tools[i].icon, tools[i].label))) {
            if (i == 0) gs.events.emit(EVENT_FILE_OPEN);
            if (i == 1) gs.events.emit(EVENT_FILE_SAVE);
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
        if (IsKeyPressed(KEY_ESCAPE) && !gs.app.track_editing && !gs.app.junction_editing && !gs.app.erase_editing && !gs.app.train_placing && !gs.app.signal_placing) {
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
