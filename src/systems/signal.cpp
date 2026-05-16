#include "signal.h"
#include "track_geom.h"
#include "track_tiles.h"
#include "../state/game_state.h"
#include "../ui/ui.h"
#include "raylib.h"
#include "raymath.h"
#include <cfloat>

// Lateral offset from the track centreline (right of heading by default).
static constexpr float SIGNAL_SIDE       = 1.0f;
// Collision sphere radius used for train-passing detection.
static constexpr float SIGNAL_DETECT_R   = 1.0f;
// Uniform scale applied to the mesh — same convention as TILE_MESH_SCALE.
static constexpr float SIGNAL_MESH_SCALE = TILE_MESH_SCALE;

// Y rotation axis shared by every DrawModelEx call.
static const Vector3 SIGNAL_AXIS_Y = { 0.0f, 1.0f, 0.0f };
// Uniform scale vector derived from SIGNAL_MESH_SCALE.
static const Vector3 SIGNAL_SCALE  = { SIGNAL_MESH_SCALE, SIGNAL_MESH_SCALE, SIGNAL_MESH_SCALE };

// ---------------------------------------------------------------------------
// Placement scratch state — valid only while gs.app.signal_placing is true.
// ---------------------------------------------------------------------------
static bool    s_flip       = false; // R key toggles left/right side + facing
static Vector3 s_ghost_pos  = {};
static float   s_ghost_angle = 0.0f; // degrees, for DrawModelEx
static int     s_snap_tile  = -1;
static int     s_snap_ep    = -1;

SignalSystem signal_system;

void SignalSystem::Init() {
    s_model    = LoadModel("assets/KS-Sig.glb");
    s_model_ok = (s_model.meshCount > 0);
    if (!s_model_ok)
        TraceLog(LOG_WARNING, "SignalSystem: failed to load KS-Sig.glb");
}

// Compute ghost position and rotation from an arbitrary point along a tile.
// track_pos  — interpolated world position on the tile chord
// heading    — interpolated heading at that position (radians)
static void UpdateGhost(Vector3 track_pos, float heading, bool flip) {
    float   side   = flip ? -SIGNAL_SIDE : SIGNAL_SIDE;
    Vector3 offset = RotateByHeading({ side, 0.0f, 0.0f }, heading);
    s_ghost_pos   = { track_pos.x + offset.x, track_pos.y, track_pos.z + offset.z };
    s_ghost_angle = heading * RAD2DEG + (flip ? 180.0f : 0.0f);
}

void SignalSystem::Update() {
    if (gs.events.has(EVENT_START_SIGNAL_PLACE)) {
        gs.app.signal_placing = true;
        s_flip      = false;
        s_snap_tile = -1;
        s_snap_ep   = -1;
    }

    if (!gs.app.signal_placing) return;

    if (IsKeyPressed(KEY_ESCAPE)) {
        gs.app.signal_placing = false;
        return;
    }

    if (IsKeyPressed(KEY_R))
        s_flip = !s_flip;

    // Cast the mouse ray against each tile's AABB — the signal can only be
    // placed when the cursor is directly over a track tile.
    Ray ray = GetMouseRay(GetMousePosition(), gs.camera.cam);

    s_snap_tile = -1;
    s_snap_ep   = -1;
    float best_dist = FLT_MAX;
    {
        auto& pool = gs.ecs.tile_bounds;
        for (int i = 0; i < pool.count; i++) {
            RayCollision col = GetRayCollisionBox(ray, pool.data[i].box);
            if (!col.hit || col.distance >= best_dist) continue;
            best_dist   = col.distance;
            s_snap_tile = pool.data[i].tile_idx;
        }
    }

    // Project the ray-ground intersection onto the struck tile's chord to find
    // the placement point anywhere along the tile, not just at its endpoints.
    if (s_snap_tile >= 0) {
        float denom = ray.direction.y;
        if (fabsf(denom) < 1e-6f) { s_snap_tile = -1; }
        else {
            float   tval   = -ray.position.y / denom;
            Vector3 cursor = { ray.position.x + tval * ray.direction.x,
                               0.0f,
                               ray.position.z + tval * ray.direction.z };

            const PlacedTile& t  = s_tiles[s_snap_tile];
            Vector3 a  = t.eps[0].pos;
            Vector3 b  = t.eps[1].pos;
            Vector3 ab = { b.x - a.x, 0.0f, b.z - a.z };
            float ab_len2 = ab.x * ab.x + ab.z * ab.z;
            float u = 0.5f;
            if (ab_len2 > 1e-6f)
                u = Clamp(((cursor.x - a.x) * ab.x + (cursor.z - a.z) * ab.z) / ab_len2,
                          0.0f, 1.0f);

            // Interpolate world position along the chord.
            Vector3 track_pos = { a.x + u * ab.x, a.y, a.z + u * ab.z };

            // Interpolate heading using the shortest angular path to avoid flipping.
            float h0 = t.eps[0].heading;
            float dh = NormAngle(t.eps[1].heading - h0);
            float heading = NormAngle(h0 + u * dh);

            // ep_idx records which half of the tile the signal is on so the
            // train-detection system knows the guarded approach direction.
            s_snap_ep = (u <= 0.5f) ? 0 : 1;

            UpdateGhost(track_pos, heading, s_flip);
        }
    }

    // Confirm placement on left click.
    if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT) &&
        s_snap_tile >= 0 &&
        !UiMouseInToolbar() && !UiMouseInPanel())
    {
        EntityID eid = gs.ecs.create();
        gs.ecs.signals.add(eid, {
            s_ghost_pos,
            SIGNAL_DETECT_R,
            SIGNAL_PROCEED,
            s_snap_tile,
            s_snap_ep,
            s_ghost_angle,
        });
        gs.app.signal_placing = false;
    }
}

void SignalSystem::Draw3D() {
    if (!s_model_ok) return;

    // Draw placed signals.
    for (int i = 0; i < gs.ecs.signals.count; i++) {
        const CSignal& s = gs.ecs.signals.data[i];
        Color col = (s.aspect == SIGNAL_STOP)    ? RED    :
                    (s.aspect == SIGNAL_CAUTION)  ? YELLOW : GREEN;
        DrawModelEx(s_model, s.pos, SIGNAL_AXIS_Y, s.rotation_angle, SIGNAL_SCALE, col);

        if ( gs.app.render_track_debug ){
            col.a = col.a/2;
            DrawSphere(s.pos, SIGNAL_DETECT_R, col);
            
        }
    }

    // Draw placement ghost when the user is actively placing.
    if (gs.app.signal_placing && s_snap_tile >= 0) {
        DrawModelEx(s_model, s_ghost_pos, SIGNAL_AXIS_Y, s_ghost_angle,
                    SIGNAL_SCALE, Color{ 0, 255, 0, 120 });
    }

}

void SignalSystem::Destroy() {
    if (s_model_ok)
        UnloadModel(s_model);
}
