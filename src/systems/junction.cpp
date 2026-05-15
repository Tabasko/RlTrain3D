#include "junction.h"
#include "track_tiles.h"
#include "track_geom.h"
#include "../state/game_state.h"
#include "../events/event_bus.h"
#include "../colors.h"
#include "raylib.h"
#include "raymath.h"

void JunctionSystemInit()    {}
void JunctionSystemDestroy() {}

void JunctionSystemUpdate() {
    if (gs.events.has(EVENT_START_JUNCTION_EDIT))
        gs.app.junction_editing = true;
    if (!gs.app.junction_editing) return;
    if (IsMouseButtonPressed(MOUSE_RIGHT_BUTTON) || IsKeyPressed(KEY_ESCAPE)) {
        gs.app.junction_editing = false;
        return;
    }

    // Left-click: throw the nearest junction.
    if (IsMouseButtonPressed(MOUSE_LEFT_BUTTON)) {
        Ray ray = GetMouseRay(GetMousePosition(), gs.camera.cam);
        if (fabsf(ray.direction.y) > 0.001f) {
            float t = -ray.position.y / ray.direction.y;
            if (t > 0.0f) {
                Vector3 pos = Vector3Add(ray.position, Vector3Scale(ray.direction, t));
                pos.y = 0.0f;
                TryThrowJunction(pos);
            }
        }
    }
}

void JunctionSystemDraw3D() {
    if (s_junctions.empty()) return;

    for (const JunctionNode& jn : s_junctions) {
        // Central marker.
        DrawSphere(jn.pos, 0.35f, COL_JUNCTION);

        if (jn.leg_count < 3) continue;

        // Lines to each leg endpoint: stem and active branch bright, inactive dim.
        for (int l = 0; l < jn.leg_count; l++) {
            const TileEndpoint& ep = s_tiles[jn.legs[l].tile_idx].eps[jn.legs[l].ep_idx];
            bool active = (l == 0) || (l == jn.thrown + 1);
            DrawLine3D(jn.pos, ep.pos, active ? COL_JUNCTION : COL_JUNCTION_DIM);
        }
    }
}

void JunctionSystemDraw2D() {
    if (!gs.app.junction_editing) return;
    int count = (int)s_junctions.size();
    DrawText(TextFormat("Junction mode — %d junction%s  [LMB] throw  [RMB/ESC] exit",
             count, count == 1 ? "" : "s"),
             20, GetScreenHeight() - 40, 20, COL_JUNCTION);
}
