#pragma once
#include "../types.h"

#define UI_MAX_BUTTONS  4
#define UI_MAX_LABEL_LEN 64

inline constexpr int UI_YES    = 0;
inline constexpr int UI_NO     = 1;
inline constexpr int UI_CANCEL = 2;

#define PANEL_FONT_SZ   ((int)(16 * UI_SCALE))
#define PANEL_CAT_FSZ   ((int)(12 * UI_SCALE))
#define PANEL_ITEM_H    (PANEL_FONT_SZ + (int)(8  * UI_SCALE))
#define PANEL_CAT_H     (PANEL_CAT_FSZ + (int)(12 * UI_SCALE))
#define PANEL_PAD       ((int)(8  * UI_SCALE))

typedef enum {
    UI_RESULT_NONE = -1,
} UI_Result;

typedef enum {
    CONFIRM_EXIT = 1,
} UI_Dialog_Type;

typedef struct {
    char  label[UI_MAX_LABEL_LEN];
    Color color;
    Color hoverColor;
} UI_ButtonDef;

typedef struct {
    bool         active;
    char         title[UI_MAX_LABEL_LEN];
    char         message[256];
    UI_ButtonDef buttons[UI_MAX_BUTTONS];
    int          buttonCount;
    int          result;
} UI_ConfirmPanel;

void UI_ShowConfirm(const char *title, const char *message,
                    UI_ButtonDef *buttonDefs, int count);
void UiDraw(void);
// int  UI_GetResult(void);
// void UI_Dismiss(void);
// bool UI_IsActive(void);

void UiUpdate(void);
bool UiMouseInToolbar(void);
bool UiMouseInPanel(void);
void UiDrawToolbar(void);
void UiDrawLeftPanel(void);
void UiDrawRightPanel(void);
// void UiDrawRoutesWorld(void);
