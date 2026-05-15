#include "track.h"
#include "track_geom.h"
#include "track_tiles.h"
#include "../state/game_state.h"
#include "../events/event_bus.h"
#include "../colors.h"
#include "raylib.h"
#include "raymath.h"
#include <vector>
#include <cstdio>

// ---------------------------------------------------------------------------
// Tile name strings — used for save/load and the debug HUD.
// ---------------------------------------------------------------------------
static const char *TILE_NAMES[TILE_TYPE_COUNT] = {
    "STRAIGHT_S", "STRAIGHT_L",
    "CURVE_R1_15", "CURVE_R1_15_L",
    "CURVE_R2_15", "CURVE_R2_15_L",
    "CURVE_R2_30", "CURVE_R2_30_L",
};

// ---------------------------------------------------------------------------
// Mesh / material state
// ---------------------------------------------------------------------------
// Mesh indices: L-variants share the mesh of their R counterpart.
static const int MESH_IDX[TILE_TYPE_COUNT] = {
    0, // STRAIGHT_S    → mesh 0
    1, // STRAIGHT_L    → mesh 1
    2, // CURVE_R1_15   → mesh 2
    2, // CURVE_R1_15_L → mesh 2 (mirrored)
    3, // CURVE_R2_15   → mesh 3
    3, // CURVE_R2_15_L → mesh 3
    4, // CURVE_R2_30   → mesh 4
    4, // CURVE_R2_30_L → mesh 4
};
static constexpr int NUM_MESHES = 5;

static Model    s_meshes[NUM_MESHES];
static bool     s_mesh_ok[NUM_MESHES];
static Material s_mat_track;
static Material s_mat_ghost;
static Material s_mat_ghost_bad;

// ---------------------------------------------------------------------------
// Placement state machine
// ---------------------------------------------------------------------------
struct PlacementState {
    bool    active        = false;
    Vector3 start_pos     = {};
    float   start_heading = 0.0f;
    int     start_tile    = TILE_NO_LINK; // existing tile we continued from
    int     start_ep      = TILE_NO_LINK;
    std::vector<Vector3> waypoints;       // intermediate anchors
    ArcDirection dir      = ARC_DIR_BOTH;
};
static PlacementState s_place;

// ---------------------------------------------------------------------------
// Ghost sequence — recomputed each frame during placement
// ---------------------------------------------------------------------------
struct GhostTile { TileType type; Matrix world; };
static std::vector<GhostTile> s_ghost;
static bool                   s_ghost_invalid = false;

// ---------------------------------------------------------------------------
// Cursor
// ---------------------------------------------------------------------------
static Vector3 s_cursor;
static bool    s_has_cursor;
static bool    s_cursor_snapped; // cursor locked to an existing open endpoint
static int     s_snap_tile = TILE_NO_LINK;
static int     s_snap_ep   = TILE_NO_LINK;

// ---------------------------------------------------------------------------
// Erase mode
// ---------------------------------------------------------------------------
static bool    s_erase_drag;
static Vector2 s_erase_start;
static Vector2 s_erase_end;

// ---------------------------------------------------------------------------
// Debug placement (T cycles tile type, R rotates heading)
// ---------------------------------------------------------------------------
static TileType s_dbg_tile      = TILE_STRAIGHT_S;
static float    s_heading_offset = 0.0f;

// ---------------------------------------------------------------------------
// Dubins path (currently unused — will drive automatic path generation)
// ---------------------------------------------------------------------------
// Returns ghost tiles for the best-fitting tile path from (start_pos, start_heading)
// to target_pos. If has_target_heading, curves to match the arrival direction.
static void ComputePath(Vector3 start_pos, float start_heading,
                        Vector3 target_pos,
                        bool has_target_heading, float target_heading,
                        std::vector<GhostTile>& out, bool& invalid) {
    out.clear();
    invalid = false;

    float dx = target_pos.x - start_pos.x;
    float dz = target_pos.z - start_pos.z;
    float bearing = atan2f(dx, dz);

    float turn_delta  = NormAngle(bearing - start_heading);
    int   steps       = (int)roundf(turn_delta / DEG15);
    float snapped_turn = steps * DEG15;
    bool  left_turn   = (snapped_turn < 0.0f);
    int   abs_steps   = abs(steps);

    TileType curve_r  = left_turn ? TILE_CURVE_R2_15_L : TILE_CURVE_R2_15;
    Vector3 cur_pos   = start_pos;
    float cur_heading = start_heading;

    for (int i = 0; i < abs_steps; i++) {
        GhostTile gt;
        gt.type  = curve_r;
        gt.world = TileMatrix(cur_pos, cur_heading);
        if (IsLeftCurve(curve_r)) gt.world = MirrorX(gt.world);
        out.push_back(gt);
        WalkTile(curve_r, cur_pos, cur_heading, &cur_pos, &cur_heading);
    }

    float dist  = Vector3Distance(cur_pos, target_pos);
    float len_l = s_geom[TILE_STRAIGHT_L].exit_pos.z;
    float len_s = s_geom[TILE_STRAIGHT_S].exit_pos.z;

    int arrival_steps = 0;
    if (has_target_heading) {
        float arrival_delta = NormAngle(target_heading - cur_heading);
        int arr_steps = (int)roundf(arrival_delta / DEG15);
        arrival_steps = abs(arr_steps);
        dist -= arrival_steps * s_geom[TILE_CURVE_R2_15].length;
        if (dist < 0.0f) { invalid = true; }
    }

    if (!invalid && dist > 0.0f) {
        int n_long  = (int)(dist / len_l);
        float rem   = dist - n_long * len_l;
        int n_short = (rem >= len_s * 0.5f) ? 1 : 0;
        for (int i = 0; i < n_long; i++) {
            GhostTile gt;
            gt.type  = TILE_STRAIGHT_L;
            gt.world = TileMatrix(cur_pos, cur_heading);
            out.push_back(gt);
            WalkTile(TILE_STRAIGHT_L, cur_pos, cur_heading, &cur_pos, &cur_heading);
        }
        for (int i = 0; i < n_short; i++) {
            GhostTile gt;
            gt.type  = TILE_STRAIGHT_S;
            gt.world = TileMatrix(cur_pos, cur_heading);
            out.push_back(gt);
            WalkTile(TILE_STRAIGHT_S, cur_pos, cur_heading, &cur_pos, &cur_heading);
        }
    }

    if (has_target_heading && !invalid) {
        float arrival_delta = NormAngle(target_heading - cur_heading);
        int arr_steps = (int)roundf(arrival_delta / DEG15);
        bool arr_left = (arr_steps < 0);
        TileType arr_curve = arr_left ? TILE_CURVE_R2_15_L : TILE_CURVE_R2_15;
        for (int i = 0; i < abs(arr_steps); i++) {
            GhostTile gt;
            gt.type  = arr_curve;
            gt.world = TileMatrix(cur_pos, cur_heading);
            if (IsLeftCurve(arr_curve)) gt.world = MirrorX(gt.world);
            out.push_back(gt);
            WalkTile(arr_curve, cur_pos, cur_heading, &cur_pos, &cur_heading);
        }
    }
}

// ---------------------------------------------------------------------------
// Ground picking — projects mouse ray onto Y=0 plane.
// ---------------------------------------------------------------------------
static bool GroundPos(Vector3 *out) {
    Ray ray = GetMouseRay(GetMousePosition(), gs.camera.cam);
    if (fabsf(ray.direction.y) < 0.001f) return false;
    float t = -ray.position.y / ray.direction.y;
    if (t < 0.0f) return false;
    *out = Vector3Add(ray.position, Vector3Scale(ray.direction, t));
    out->y = 0.0f;
    return true;
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

void TrackSystemInit() {
    TrackGeomInit();

    // Load only assets that currently exist; missing slots are skipped at draw time.
    static const char *mesh_paths[NUM_MESHES] = {
        "assets/track-straight-s.glb",  // 0 — available
        nullptr,                         // 1 — STRAIGHT_L, pending
        nullptr,                         // 2 — CURVE_R1_15, pending
        nullptr,                         // 3 — CURVE_R2_15, pending
        "assets/track-curve-r2-30.glb", // 4 — CURVE_R2_30
    };

    for (int i = 0; i < NUM_MESHES; i++) {
        s_mesh_ok[i] = false;
        if (!mesh_paths[i]) continue;
        s_meshes[i]  = LoadModel(mesh_paths[i]);
        s_mesh_ok[i] = (s_meshes[i].meshCount > 0);
        if (!s_mesh_ok[i])
            TraceLog(LOG_WARNING, "TRACK: failed to load %s", mesh_paths[i]);
    }

    s_mat_track = LoadMaterialDefault();
    s_mat_track.maps[MATERIAL_MAP_DIFFUSE].color = COL_TRACK;

    s_mat_ghost = LoadMaterialDefault();
    s_mat_ghost.maps[MATERIAL_MAP_DIFFUSE].color = COL_TRACK_GHOST;

    s_mat_ghost_bad = LoadMaterialDefault();
    s_mat_ghost_bad.maps[MATERIAL_MAP_DIFFUSE].color = COL_TRACK_GHOST_BAD;
}

void TrackSystemUpdate() {
    if (gs.events.has(EVENT_START_TRACK_EDIT)) {
        gs.app.track_editing = true;
        gs.app.erase_editing = false;
        s_place          = {};
        s_heading_offset = 0.0f;
    }
    if (gs.events.has(EVENT_START_ERASE_EDIT)) {
        gs.app.erase_editing = true;
        gs.app.track_editing = false;
        s_erase_drag         = false;
        s_place              = {};
    }

    if (!gs.app.track_editing && !gs.app.erase_editing) return;

    // --- Erase mode ---
    if (gs.app.erase_editing) {
        if (IsMouseButtonPressed(MOUSE_LEFT_BUTTON)) {
            s_erase_start = s_erase_end = GetMousePosition();
            s_erase_drag  = true;
        }
        if (s_erase_drag && IsMouseButtonDown(MOUSE_LEFT_BUTTON))
            s_erase_end = GetMousePosition();

        if (s_erase_drag && IsMouseButtonReleased(MOUSE_LEFT_BUTTON)) {
            s_erase_drag = false;
            Rectangle r = {
                fminf(s_erase_start.x, s_erase_end.x),
                fminf(s_erase_start.y, s_erase_end.y),
                fabsf(s_erase_end.x - s_erase_start.x),
                fabsf(s_erase_end.y - s_erase_start.y),
            };
            for (int i = (int)s_tiles.size() - 1; i >= 0; i--) {
                Vector2 sp = GetWorldToScreen(s_tiles[i].eps[0].pos, gs.camera.cam);
                if (CheckCollisionPointRec(sp, r))
                    RemoveTile(i);
            }
            RebuildInstanceBuffers();
        }
        if (IsMouseButtonPressed(MOUSE_RIGHT_BUTTON) || IsKeyPressed(KEY_ESCAPE)) {
            s_erase_drag         = false;
            gs.app.erase_editing = false;
        }
        return;
    }

    // --- Track placement mode ---
    s_has_cursor = GroundPos(&s_cursor);
    if (!s_has_cursor) return;

    // Grid snap.
    s_cursor.x = roundf(s_cursor.x / GRID_CELL) * GRID_CELL;
    s_cursor.z = roundf(s_cursor.z / GRID_CELL) * GRID_CELL;

    // Endpoint snap (overrides grid snap).
    // Snaps to any endpoint not yet in a junction — both open endpoints (for direct
    // linking) and directly-linked endpoints (for junction upgrade).
    s_cursor_snapped = false;
    s_snap_tile      = TILE_NO_LINK;
    s_snap_ep        = TILE_NO_LINK;
    for (int ti = 0; ti < (int)s_tiles.size(); ti++) {
        for (int ep = 0; ep < s_tiles[ti].ep_count; ep++) {
            if (s_tiles[ti].eps[ep].linked_junction != TILE_NO_LINK) continue;
            if (Vector3Distance(s_cursor, s_tiles[ti].eps[ep].pos) < SNAP_EP_R) {
                s_cursor         = s_tiles[ti].eps[ep].pos;
                s_cursor_snapped = true;
                s_snap_tile      = ti;
                s_snap_ep        = ep;
                break;
            }
        }
        if (s_cursor_snapped) break;
    }

    // T cycles tile type; R / Shift+R rotates placement heading in 15° / 45° steps.
    if (IsKeyPressed(KEY_T))
        s_dbg_tile = (TileType)((s_dbg_tile + 1) % TILE_TYPE_COUNT);
    if (IsKeyPressed(KEY_R)) {
        float step = (IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_RIGHT_SHIFT))
                     ? DEG45 : DEG15;
        s_heading_offset = NormAngle(s_heading_offset + step);
    }

    // Returns the natural continuation heading when snapping to an open endpoint:
    // ep[1] (exit end) → continue forward; ep[0] (entry end) → extend backward.
    auto SnapHeading = [&](int tile, int ep) -> float {
        float eh = s_tiles[tile].eps[ep].heading;
        return (ep == 0) ? NormAngle(eh + (float)M_PI) : eh;
    };

    // Build ghost tile — always follows the cursor (grid/endpoint snap applied above).
    // Chaining works naturally: the placed tile's exit becomes an open endpoint that
    // the cursor snaps to on the next frame, aligning the next ghost automatically.
    s_ghost.clear();
    s_ghost_invalid = false;
    {
        float base_heading  = s_cursor_snapped ? SnapHeading(s_snap_tile, s_snap_ep) : 0.0f;
        float ghost_heading = NormAngle(base_heading + s_heading_offset);

        GhostTile gt;
        gt.type  = s_dbg_tile;
        gt.world = TileMatrix(s_cursor, ghost_heading);
        if (IsLeftCurve(s_dbg_tile)) gt.world = MirrorX(gt.world);
        s_ghost.push_back(gt);

        Vector3 ghost_exit; float ghost_exit_h;
        WalkTile(s_dbg_tile, s_cursor, ghost_heading, &ghost_exit, &ghost_exit_h);
        if (GhostCollides(s_cursor, ghost_exit))
            s_ghost_invalid = true;
    }

    // Left-click: place tile at cursor (snapped position when applicable).
    if (IsMouseButtonPressed(MOUSE_LEFT_BUTTON) && !s_ghost_invalid) {
        float base_h = s_cursor_snapped ? SnapHeading(s_snap_tile, s_snap_ep) : 0.0f;
        float ph     = NormAngle(base_h + s_heading_offset);
        PlaceTile(s_dbg_tile, s_cursor, ph, s_place.dir);
        s_heading_offset = 0.0f; // reset so next tile continues naturally from snap direction
        RebuildInstanceBuffers();
    }

    // Right-click / ESC: exit placement.
    if (IsMouseButtonPressed(MOUSE_RIGHT_BUTTON) || IsKeyPressed(KEY_ESCAPE)) {
        s_place              = {};
        gs.app.track_editing = false;
        s_ghost.clear();
    }

    // D: cycle direction constraint.
    if (IsKeyPressed(KEY_D)) {
        if      (s_place.dir == ARC_DIR_BOTH)   s_place.dir = ARC_DIR_A_TO_B;
        else if (s_place.dir == ARC_DIR_A_TO_B) s_place.dir = ARC_DIR_B_TO_A;
        else                                     s_place.dir = ARC_DIR_BOTH;
    }
}

void TrackSystemDraw3D() {
    for (const PlacedTile& tile : s_tiles) {
        int mi = MESH_IDX[tile.type];
        if (!s_mesh_ok[mi]) continue;
        Model& m = s_meshes[mi];
        for (int msh = 0; msh < m.meshCount; msh++)
            DrawMesh(m.meshes[msh], m.materials[m.meshMaterial[msh]], tile.world);
    }

    // Open endpoints — shown in both editing modes.
    if (gs.app.track_editing || gs.app.erase_editing) {
        for (const PlacedTile& tile : s_tiles)
            for (int ep = 0; ep < tile.ep_count; ep++)
                if (tile.eps[ep].linked_tile == TILE_NO_LINK)
                    DrawSphere(tile.eps[ep].pos, 0.3f, COL_SNAP_ENDPOINT);
    }

    if (gs.app.track_editing && s_has_cursor)
        DrawSphere(s_cursor, 0.2f, COL_SNAP_ENDPOINT);

    if (gs.app.track_editing && s_has_cursor) {
        for (const GhostTile& g : s_ghost) {
            int mi = MESH_IDX[g.type];
            if (!s_mesh_ok[mi]) continue;
            Material& mat = s_ghost_invalid ? s_mat_ghost_bad : s_mat_ghost;
            Model& m = s_meshes[mi];
            for (int msh = 0; msh < m.meshCount; msh++)
                DrawMesh(m.meshes[msh], mat, g.world);
        }

        if (s_cursor_snapped)
            DrawCircle3D(s_cursor, 0.6f, {1,0,0}, 90.0f, COL_SNAP_ENDPOINT);

    }
}

void TrackSystemDraw2D() {
    if (gs.app.erase_editing && s_erase_drag) {
        Rectangle r = {
            fminf(s_erase_start.x, s_erase_end.x),
            fminf(s_erase_start.y, s_erase_end.y),
            fabsf(s_erase_end.x - s_erase_start.x),
            fabsf(s_erase_end.y - s_erase_start.y),
        };
        DrawRectangleRec(r, Color{220, 60, 60, 40});
        DrawRectangleLinesEx(r, 1.5f, Color{220, 60, 60, 200});
    }

    if (!gs.app.track_editing || !s_has_cursor) return;

    Vector2 sc = GetWorldToScreen(s_cursor, gs.camera.cam);
    DrawText(TextFormat("[T] %s", TILE_NAMES[s_dbg_tile]),
             (int)sc.x + 14, (int)sc.y - 10, 20, COL_ACCENT);
}

void TrackSystemDestroy() {
    for (int i = 0; i < NUM_MESHES; i++)
        if (s_mesh_ok[i]) UnloadModel(s_meshes[i]);
    s_tiles.clear();
    s_junctions.clear();
    s_ghost.clear();
}

// ---------------------------------------------------------------------------
// Save / Load
// ---------------------------------------------------------------------------

void TrackSystemSave(FILE *f) {
    fprintf(f, "TILES %d\n", (int)s_tiles.size());
    for (const PlacedTile& t : s_tiles) {
        const Matrix& m = t.world;
        fprintf(f, "%s %d  "
                "%.6f %.6f %.6f %.6f  %.6f %.6f %.6f %.6f  "
                "%.6f %.6f %.6f %.6f  %.6f %.6f %.6f %.6f\n",
                TILE_NAMES[t.type], (int)t.direction,
                m.m0,  m.m1,  m.m2,  m.m3,
                m.m4,  m.m5,  m.m6,  m.m7,
                m.m8,  m.m9,  m.m10, m.m11,
                m.m12, m.m13, m.m14, m.m15);
    }
    // Save junction thrown states; topology is reconstructed from tiles on load.
    fprintf(f, "JUNCTIONS %d\n", (int)s_junctions.size());
    for (const JunctionNode& jn : s_junctions)
        fprintf(f, "%.6f %.6f %.6f %d\n", jn.pos.x, jn.pos.y, jn.pos.z, jn.thrown);
}

void TrackSystemLoad(FILE *f) {
    s_tiles.clear();
    s_junctions.clear();
    s_ghost.clear();

    int n = 0;
    if (fscanf(f, " TILES %d", &n) != 1) return;

    for (int i = 0; i < n; i++) {
        char name[32];
        int  dir_i;
        Matrix m;
        if (fscanf(f, " %31s %d  "
                   "%f %f %f %f  %f %f %f %f  "
                   "%f %f %f %f  %f %f %f %f",
                   name, &dir_i,
                   &m.m0,  &m.m1,  &m.m2,  &m.m3,
                   &m.m4,  &m.m5,  &m.m6,  &m.m7,
                   &m.m8,  &m.m9,  &m.m10, &m.m11,
                   &m.m12, &m.m13, &m.m14, &m.m15) != 18) {
            TraceLog(LOG_ERROR, "TRACK: parse error at tile %d", i);
            break;
        }

        TileType type = TILE_STRAIGHT_S;
        for (int t = 0; t < TILE_TYPE_COUNT; t++)
            if (strcmp(name, TILE_NAMES[t]) == 0) { type = (TileType)t; break; }

        PlacedTile tile;
        tile.type      = type;
        tile.direction = (ArcDirection)dir_i;
        tile.world     = m;
        tile.ep_count  = 2;

        // Recover placement position and heading from the stored world matrix.
        // Translation is in column 3; heading from atan2(sin, cos) in Ry columns.
        Vector3 pos     = { m.m12, m.m13, m.m14 };
        float   heading = atan2f(m.m8, m.m0);

        tile.eps[0].pos             = pos;
        tile.eps[0].heading         = heading;
        tile.eps[0].linked_tile     = TILE_NO_LINK;
        tile.eps[0].linked_ep       = TILE_NO_LINK;
        tile.eps[0].linked_junction = TILE_NO_LINK;

        Vector3 np; float nh;
        WalkTile(type, pos, heading, &np, &nh);
        tile.eps[1].pos             = np;
        tile.eps[1].heading         = nh;
        tile.eps[1].linked_tile     = TILE_NO_LINK;
        tile.eps[1].linked_ep       = TILE_NO_LINK;
        tile.eps[1].linked_junction = TILE_NO_LINK;

        s_tiles.push_back(tile);
    }

    for (int i = 0; i < (int)s_tiles.size(); i++)
        AutoLink(i);

    RebuildInstanceBuffers();

    // Apply saved junction thrown states (topology was rebuilt by AutoLink above).
    int njunc = 0;
    if (fscanf(f, " JUNCTIONS %d", &njunc) == 1) {
        for (int i = 0; i < njunc; i++) {
            Vector3 pos; int thrown;
            if (fscanf(f, " %f %f %f %d", &pos.x, &pos.y, &pos.z, &thrown) != 4) break;
            for (JunctionNode& jn : s_junctions)
                if (Vector3Distance(jn.pos, pos) < SNAP_EP_R) { jn.thrown = thrown; break; }
        }
    }
}
