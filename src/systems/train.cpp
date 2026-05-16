#include "train.h"
#include "track_geom.h"
#include "track_tiles.h"
#include "../state/game_state.h"
#include "../types.h"
#include "../ui/ui.h"
#include "raylib.h"
#include "raymath.h"
#include <cmath>

// World units / second. Tune to taste.
static constexpr float TRAM_SPEED  = 5.0f;
// Acceleration / deceleration rate (world units / sec²).
static constexpr float TRAM_ACCEL  = 1.5f;
// Uniform scale applied to the loaded tram mesh.
static constexpr float TRAM_SCALE  = 0.25f;
// Click-to-tile pick radius in world units.
static constexpr float TRAM_PICK_R = 2.0f;

static Model     s_model;
static bool      s_model_loaded = false;
static TileTrain s_trains[MAX_TILE_TRAINS];
static int       s_train_count = 0;

// ---------------------------------------------------------------------------
// Arc interpolation
// ---------------------------------------------------------------------------

// Compute local-space position and heading on a tile at arc distance d from eps[0].
// Local space: entry at origin, heading 0 = +Z.
// Straight: local forward may be +Z or -Z depending on exit_pos sign.
// Curve:    exact arc, radius derived from arc_length / |exit_heading|.
static void TileLocalPosHeading(TileType type, float d,
                                Vector3 *pos_out, float *heading_out) {
    const TileGeom& g  = s_geom[type];
    float t  = (g.length > 0.0f) ? (d / g.length) : 0.0f;
    float eh = g.exit_heading;

    if (fabsf(eh) < 0.001f) {
        // Straight: exit_pos gives the direction vector (length = arc length).
        *pos_out     = Vector3Scale(g.exit_pos, t);
        *heading_out = 0.0f;
    } else {
        // Curve: R = arc_length / |theta|.  Positive eh = right, negative = left.
        float theta = fabsf(eh);
        float R     = g.length / theta;
        float phi   = theta * t;
        float sign  = (eh > 0.0f) ? 1.0f : -1.0f;
        // Arc parameterisation: right-curve in XZ with entry along +Z.
        // pos = (R*sin(phi), 0, R*(1-cos(phi))), mirrored for left curves.
        *pos_out     = { sign * R * sinf(phi), 0.0f, R * (1.0f - cosf(phi)) };
        *heading_out = sign * phi;
    }
}

// Compute world-space position and heading on tile at arc distance d from eps[0].
static void TileWorldPosHeading(int tile_idx, float d,
                                Vector3 *pos_out, float *heading_out) {
    const PlacedTile& tile = s_tiles[tile_idx];
    Vector3 local;
    float   local_h;
    TileLocalPosHeading(tile.type, d, &local, &local_h);
    *pos_out     = Vector3Add(tile.eps[0].pos, RotateByHeading(local, tile.eps[0].heading));
    *heading_out = NormAngle(tile.eps[0].heading + local_h);
}

// ---------------------------------------------------------------------------
// Junction routing helper
// ---------------------------------------------------------------------------

// Given a tile/ep arriving at a junction, return the next tile_idx and ep_idx
// to traverse. Returns false if the junction blocks passage (inactive branch).
static bool JunctionNextTile(int tile_idx, int ep_idx,
                              int *next_tile, int *next_ep) {
    int ji = s_tiles[tile_idx].eps[ep_idx].linked_junction;
    if (ji < 0 || ji >= (int)s_junctions.size()) return false;

    const JunctionNode& jn = s_junctions[ji];

    // Find our leg index.
    int our_leg = -1;
    for (int l = 0; l < jn.leg_count; l++)
        if (jn.legs[l].tile_idx == tile_idx && jn.legs[l].ep_idx == ep_idx)
            our_leg = l;
    if (our_leg < 0) return false;

    // legs[0] = stem; legs[1] and legs[2] = branches.
    // thrown=0 → active branch is legs[1], thrown=1 → legs[2].
    int target_leg = -1;
    if (our_leg == 0)
        target_leg = 1 + jn.thrown;
    else if (our_leg == 1 + jn.thrown)
        target_leg = 0;
    // Otherwise we are on the inactive branch → blocked.

    if (target_leg < 0 || target_leg >= jn.leg_count) return false;
    *next_tile = jn.legs[target_leg].tile_idx;
    *next_ep   = jn.legs[target_leg].ep_idx;
    return true;
}

// ---------------------------------------------------------------------------
// Tile picking
// ---------------------------------------------------------------------------

// Project click onto each tile's chord (eps[0]→eps[1]) and return the closest
// tile index, or -1 if none is within TRAM_PICK_R. Fills arc_dist_out with the
// approximate arc distance from eps[0] (chord-based, sufficient for placement).
static int PickTile(Vector3 click, float *arc_dist_out) {
    int   best      = -1;
    float best_dist = TRAM_PICK_R;

    for (int i = 0; i < (int)s_tiles.size(); i++) {
        const PlacedTile& t = s_tiles[i];
        Vector3 a  = t.eps[0].pos;
        Vector3 b  = t.eps[1].pos;
        Vector3 ab = { b.x - a.x, 0.0f, b.z - a.z };
        float ab_len2 = ab.x * ab.x + ab.z * ab.z;
        if (ab_len2 < 1e-6f) continue;
        float u = ((click.x - a.x) * ab.x + (click.z - a.z) * ab.z) / ab_len2;
        u = Clamp(u, 0.0f, 1.0f);
        Vector3 closest = { a.x + u * ab.x, 0.0f, a.z + u * ab.z };
        float dist = Vector3Distance(click, closest);
        if (dist < best_dist) {
            best_dist     = dist;
            best          = i;
            *arc_dist_out = u * s_geom[t.type].length;
        }
    }
    return best;
}

// ---------------------------------------------------------------------------
// Movement
// ---------------------------------------------------------------------------

// Resolve the next tile when crossing an endpoint boundary.
// ep_idx: the endpoint being left (0 = leaving through eps[0], 1 = through eps[1]).
// Returns false and reverses in place if no passage is available.
static bool CrossBoundary(TileTrain& tr, int ep_idx, float overflow) {
    const TileEndpoint& ep = s_tiles[tr.tile_idx].eps[ep_idx];

    int next_tile = -1;
    int next_ep   = -1;

    if (ep.linked_junction != TILE_NO_LINK) {
        if (!JunctionNextTile(tr.tile_idx, ep_idx, &next_tile, &next_ep)) {
            // Blocked branch: reverse.
            if (ep_idx == 1)
                tr.arc_dist = s_geom[s_tiles[tr.tile_idx].type].length - overflow;
            else
                tr.arc_dist = overflow;
            tr.speed = -tr.speed;
            return false;
        }
    } else if (ep.linked_tile != TILE_NO_LINK) {
        next_tile = ep.linked_tile;
        next_ep   = ep.linked_ep;
    }

    if (next_tile < 0) {
        // Open endpoint: stop and reverse direction.
        tr.arc_dist    = (ep_idx == 1) ? s_geom[s_tiles[tr.tile_idx].type].length : 0.0f;
        tr.speed       = 0.0f;
        tr.target_speed = -tr.target_speed;
        return false;
    }

    tr.tile_idx    = next_tile;
    float next_len = s_geom[s_tiles[next_tile].type].length;

    // Enter the new tile. If connecting at ep[0] continue forward;
    // if at ep[1] flip direction (entering the tile from its exit end).
    if (next_ep == 0) {
        tr.arc_dist     = overflow;
        tr.speed        =  fabsf(tr.speed);
        tr.target_speed =  fabsf(tr.target_speed);
    } else {
        tr.arc_dist     = next_len - overflow;
        tr.speed        = -fabsf(tr.speed);
        tr.target_speed = -fabsf(tr.target_speed);
    }
    return true;
}

// Walk the tile graph forward from (tile_idx, arc_dist) in the direction given
// by ep_dir (1 = toward eps[1], 0 = toward eps[0]) and return the distance to
// the nearest open endpoint, or max_dist + 1 if none is found within max_dist.
static float DistToOpenEndpoint(int tile_idx, float arc_dist, int ep_dir, float max_dist) {
    int   cur  = tile_idx;
    float dist = (ep_dir == 1) ? (s_geom[s_tiles[cur].type].length - arc_dist) : arc_dist;

    while (true) {
        const TileEndpoint& ep = s_tiles[cur].eps[ep_dir];

        if (ep.linked_tile == TILE_NO_LINK && ep.linked_junction == TILE_NO_LINK)
            return dist; // open endpoint found

        if (dist >= max_dist)
            return max_dist + 1.0f; // nothing within budget

        // Advance to the next tile.
        int next_tile, next_ep;
        if (ep.linked_junction != TILE_NO_LINK) {
            if (!JunctionNextTile(cur, ep_dir, &next_tile, &next_ep))
                return dist; // blocked junction counts as endpoint
        } else {
            next_tile = ep.linked_tile;
            next_ep   = ep.linked_ep;
        }

        cur    = next_tile;
        ep_dir = 1 - next_ep; // if entered at ep[0] head toward ep[1], and vice versa
        dist  += s_geom[s_tiles[cur].type].length;
    }
}

// Advance a single train by dt seconds, handling tile transitions.
static void AdvanceTrain(TileTrain& tr, float dt) {
    if (tr.tile_idx < 0 || tr.tile_idx >= (int)s_tiles.size()) return;

    // Determine the maximum speed allowed by the braking distance to the next
    // open endpoint in the direction of travel.
    int   ep_dir    = (tr.target_speed >= 0.0f) ? 1 : 0;
    float brake_d   = (tr.speed * tr.speed) / (2.0f * TRAM_ACCEL);
    float to_end    = DistToOpenEndpoint(tr.tile_idx, tr.arc_dist, ep_dir, brake_d + 0.1f);
    float sign      = (tr.target_speed >= 0.0f) ? 1.0f : -1.0f;
    float v_allowed = (to_end <= brake_d)
                        ? sqrtf(2.0f * TRAM_ACCEL * fmaxf(to_end, 0.0f))
                        : TRAM_SPEED;
    tr.target_speed = sign * v_allowed;

    // Accelerate current speed toward the target speed.
    float step = TRAM_ACCEL * dt;
    if (tr.speed < tr.target_speed)
        tr.speed = fminf(tr.speed + step, tr.target_speed);
    else if (tr.speed > tr.target_speed)
        tr.speed = fmaxf(tr.speed - step, tr.target_speed);

    tr.arc_dist += tr.speed * dt;

    // Forward overflow: reached eps[1].
    while (tr.arc_dist >= s_geom[s_tiles[tr.tile_idx].type].length) {
        float len      = s_geom[s_tiles[tr.tile_idx].type].length;
        float overflow = tr.arc_dist - len;
        if (!CrossBoundary(tr, 1, overflow)) break;
    }

    // Backward underflow: reached eps[0].
    while (tr.arc_dist < 0.0f) {
        float overflow = -tr.arc_dist;
        if (!CrossBoundary(tr, 0, overflow)) break;
    }
}

// ---------------------------------------------------------------------------
// Public interface
// ---------------------------------------------------------------------------

void TrainSystemInit() {
    for (int i = 0; i < MAX_TILE_TRAINS; i++)
        s_trains[i].tile_idx = -1;

    if (FileExists("assets/trains/tram.glb")) {
        s_model        = LoadModel("assets/trains/tram.glb");
        s_model_loaded = true;
    } else {
        TraceLog(LOG_WARNING, "TRAIN: assets/trains/tram.glb not found");
    }
}

void TrainSystemUpdate() {
    float dt = GetFrameTime();

    // Start placing mode.
    if (gs.events.has(EVENT_START_TRAIN_PLACE)) {
        gs.app.track_editing   = false;
        gs.app.junction_editing = false;
        gs.app.erase_editing   = false;
        gs.app.train_placing   = true;
    }

    if (gs.app.train_placing) {
        if (IsKeyPressed(KEY_ESCAPE)) {
            gs.app.train_placing = false;
        } else if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT) &&
                   !UiMouseInToolbar() && !UiMouseInPanel()) {
            // Ray-cast onto Y=0 plane.
            Ray       ray  = GetMouseRay(GetMousePosition(), gs.camera.cam);
            float     denom = ray.direction.y;
            if (fabsf(denom) > 1e-6f) {
                float     t    = -ray.position.y / denom;
                Vector3   hit  = { ray.position.x + t * ray.direction.x,
                                   0.0f,
                                   ray.position.z + t * ray.direction.z };
                float arc = 0.0f;
                int   idx = PickTile(hit, &arc);
                if (idx >= 0 && s_train_count < MAX_TILE_TRAINS) {
                    // Find a free slot.
                    for (int i = 0; i < MAX_TILE_TRAINS; i++) {
                        if (s_trains[i].tile_idx < 0) {
                            s_trains[i] = { idx, arc, 0.0f, TRAM_SPEED };
                            s_train_count++;
                            break;
                        }
                    }
                    gs.app.train_placing = false;
                }
            }
        }
    }

    // Advance all active trains.
    for (int i = 0; i < MAX_TILE_TRAINS; i++) {
        if (s_trains[i].tile_idx >= 0)
            AdvanceTrain(s_trains[i], dt);
    }
}

void TrainSystemDraw3D() {
    if (!s_model_loaded) return;

    for (int i = 0; i < MAX_TILE_TRAINS; i++) {
        if (s_trains[i].tile_idx < 0) continue;
        const TileTrain& tr = s_trains[i];
        if (tr.tile_idx >= (int)s_tiles.size()) continue;

        Vector3 pos;
        float   heading;
        TileWorldPosHeading(tr.tile_idx, tr.arc_dist, &pos, &heading);

        // Flip heading when driving backward so the model always faces forward.
        float draw_heading = (tr.speed >= 0.0f) ? heading : NormAngle(heading + (float)M_PI);

        DrawModelEx(s_model, pos,
                    (Vector3){ 0.0f, 1.0f, 0.0f },
                    draw_heading * RAD2DEG,
                    (Vector3){ TRAM_SCALE, TRAM_SCALE, TRAM_SCALE },
                    WHITE);
    }
}

void TrainSystemDestroy() {
    if (s_model_loaded) {
        UnloadModel(s_model);
        s_model_loaded = false;
    }
}

void TrainSystemSave(FILE *f) {
    int count = 0;
    for (int i = 0; i < MAX_TILE_TRAINS; i++)
        if (s_trains[i].tile_idx >= 0) count++;

    fprintf(f, "TRAINS %d\n", count);
    for (int i = 0; i < MAX_TILE_TRAINS; i++) {
        const TileTrain& tr = s_trains[i];
        if (tr.tile_idx < 0) continue;
        int dir = (tr.target_speed >= 0.0f) ? 1 : -1;
        fprintf(f, "TRAIN %d %f %d\n", tr.tile_idx, tr.arc_dist, dir);
    }
}

void TrainSystemLoad(FILE *f) {
    for (int i = 0; i < MAX_TILE_TRAINS; i++)
        s_trains[i].tile_idx = -1;
    s_train_count = 0;

    int count = 0;
    if (fscanf(f, " TRAINS %d", &count) != 1) return;

    for (int i = 0; i < count && i < MAX_TILE_TRAINS; i++) {
        int   tile_idx, dir;
        float arc_dist;
        if (fscanf(f, " TRAIN %d %f %d", &tile_idx, &arc_dist, &dir) != 3) break;
        s_trains[i] = { tile_idx, arc_dist, 0.0f, (float)dir * TRAM_SPEED };
        s_train_count++;
    }
}
