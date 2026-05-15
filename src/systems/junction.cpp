#include "junction.h"
#include "track.h"
#include "../state/game_state.h"
#include "../events/event_bus.h"
#include "../colors.h"
#include "raylib.h"
#include "raymath.h"

static constexpr float SPLIT_SNAP_RADIUS = 1.5f; // world-space snap radius for arc finding

// ---------------------------------------------------------------------------
// Placement state
// ---------------------------------------------------------------------------
struct JunctionPlacement {
    bool     has_hit = false;
    ArcPoint hit     = {};
};

static JunctionPlacement s_place;

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static bool GroundPosRaw(Vector3 *out) {
    Ray ray = GetMouseRay(GetMousePosition(), gs.camera.cam);
    if (fabsf(ray.direction.y) < 0.001f) return false;
    float t = -ray.position.y / ray.direction.y;
    if (t < 0.0f) return false;
    *out = Vector3Add(ray.position, Vector3Scale(ray.direction, t));
    return true;
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

void JunctionSystemInit() {}

void JunctionSystemUpdate() {
    if (gs.events.has(EVENT_START_JUNCTION_EDIT)) {
        s_place              = {};
        gs.app.junction_editing = true;
    }

    if (!gs.app.junction_editing) return;

    if (IsMouseButtonPressed(MOUSE_RIGHT_BUTTON) || IsKeyPressed(KEY_ESCAPE)) {
        s_place              = {};
        gs.app.junction_editing = false;
    }
}

void JunctionSystemDraw3D() {
    if (!gs.app.junction_editing || !s_place.has_hit) return;

    // Show the proposed split point and the arc direction at that point
    DrawSphere(s_place.hit.pos, 0.25f, COL_JUNCTION);
    Vector3 fwd = Vector3Scale(s_place.hit.tangent, 2.0f);
    DrawLine3D(Vector3Subtract(s_place.hit.pos, fwd),
               Vector3Add(s_place.hit.pos, fwd), COL_JUNCTION);
}

void JunctionSystemDraw2D() {
    if (!gs.app.junction_editing) return;
    int sh = GetScreenHeight();
    if (!s_place.has_hit) {
        DrawText("Hover over a track arc to place a junction node",
                 20, sh - 40, 20, COL_JUNCTION);
    } else {
        DrawText("LMB: split arc here | RMB / ESC: cancel",
                 20, sh - 40, 20, COL_UI_TEXT);
    }
}

void JunctionSystemDestroy() {}
