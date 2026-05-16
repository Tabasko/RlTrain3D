#include "train.h"
#include "train_catalog.h"
#include "track_geom.h"
#include "track_tiles.h"
#include "../events/event_bus.h"
#include "../state/game_state.h"
#include "../types.h"
#include "../ui/ui.h"
#include "raylib.h"
#include "raymath.h"
#include <cfloat>
#include <cmath>

static TileTrain s_trains[MAX_TILE_TRAINS];
static int       s_train_count = 0;

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

    if (target_leg < 0 || target_leg >= jn.leg_count) return false;
    *next_tile = jn.legs[target_leg].tile_idx;
    *next_ep   = jn.legs[target_leg].ep_idx;
    return true;
}

// ---------------------------------------------------------------------------
// Tile picking
// ---------------------------------------------------------------------------

// Cast ray against each tile's AABB (stored in gs.ecs.tile_bounds). Returns the
// tile_idx of the nearest hit, or -1 if no tile was struck. Fills arc_dist_out
// with the chord-projected arc distance from eps[0] — one projection, on the
// winner only, so arc_dist remains exact for placement regardless of tile shape.
static int PickTile(Ray ray, float *arc_dist_out) {
    auto& pool      = gs.ecs.tile_bounds;
    float best_dist = FLT_MAX;
    int   best_tile = -1;

    for (int i = 0; i < pool.count; i++) {
        RayCollision col = GetRayCollisionBox(ray, pool.data[i].box);
        if (!col.hit || col.distance >= best_dist) continue;
        best_dist = col.distance;
        best_tile = pool.data[i].tile_idx;
    }

    if (best_tile >= 0 && arc_dist_out) {
        const PlacedTile& t   = s_tiles[best_tile];
        Vector3 a  = t.eps[0].pos;
        Vector3 b  = t.eps[1].pos;
        Vector3 ab = { b.x - a.x, 0.0f, b.z - a.z };
        float ab_len2 = ab.x * ab.x + ab.z * ab.z;
        if (ab_len2 > 1e-6f) {
            // Project the ray-ground intersection onto the chord for arc_dist.
            float denom = ray.direction.y;
            float u     = 0.5f; // fallback: mid-tile
            if (fabsf(denom) > 1e-6f) {
                float   tval = -ray.position.y / denom;
                Vector3 hit  = { ray.position.x + tval * ray.direction.x, 0.0f,
                                 ray.position.z + tval * ray.direction.z };
                u = ((hit.x - a.x) * ab.x + (hit.z - a.z) * ab.z) / ab_len2;
                u = Clamp(u, 0.0f, 1.0f);
            }
            *arc_dist_out = u * s_geom[t.type].length;
        }
    }
    return best_tile;
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
            tr.arc_dist     = (ep_idx == 1) ? s_geom[s_tiles[tr.tile_idx].type].length : 0.0f;
            tr.speed        = 0.0f;
            tr.target_speed = -tr.target_speed;
            return false;
        }
    } else if (ep.linked_tile != TILE_NO_LINK) {
        next_tile = ep.linked_tile;
        next_ep   = ep.linked_ep;
    }

    if (next_tile < 0) {
        // Open endpoint: stop and reverse direction.
        tr.arc_dist     = (ep_idx == 1) ? s_geom[s_tiles[tr.tile_idx].type].length : 0.0f;
        tr.speed        = 0.0f;
        tr.target_speed = -tr.target_speed;
        gs.events.emit(EVENT_TRAIN_ARRIVED_STATION);
        return false;
    }

    tr.tile_idx    = next_tile;
    float next_len = s_geom[s_tiles[next_tile].type].length;

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
            return dist;

        if (dist >= max_dist)
            return max_dist + 1.0f;

        int next_tile, next_ep;
        if (ep.linked_junction != TILE_NO_LINK) {
            if (!JunctionNextTile(cur, ep_dir, &next_tile, &next_ep))
                return dist;
        } else {
            next_tile = ep.linked_tile;
            next_ep   = ep.linked_ep;
        }

        cur    = next_tile;
        ep_dir = 1 - next_ep;
        dist  += s_geom[s_tiles[cur].type].length;
    }
}

// Advance a single train by dt seconds, handling tile transitions.
static void AdvanceTrain(TileTrain& tr, float dt) {
    if (tr.tile_idx < 0 || tr.tile_idx >= (int)s_tiles.size()) return;

    const TrainDef *def      = TrainCatalogGet(tr.def_idx);
    float           max_speed = def ? def->max_speed : 5.0f;
    float           accel     = def ? def->accel     : 1.5f;

    // Clamp target speed to the braking distance from the next open endpoint.
    int   ep_dir    = (tr.target_speed >= 0.0f) ? 1 : 0;
    float brake_d   = (tr.speed * tr.speed) / (2.0f * accel);
    float to_end    = DistToOpenEndpoint(tr.tile_idx, tr.arc_dist, ep_dir, brake_d + 0.1f);
    float sign      = (tr.target_speed >= 0.0f) ? 1.0f : -1.0f;
    float v_allowed = (to_end <= brake_d)
                        ? sqrtf(2.0f * accel * fmaxf(to_end, 0.0f))
                        : max_speed;
    tr.target_speed = sign * v_allowed;

    // Accelerate current speed toward the target speed.
    float step = accel * dt;
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

TrainSystem train_system;

void TrainSystem::Init() {
    for (int i = 0; i < MAX_TILE_TRAINS; i++)
        s_trains[i].tile_idx = -1;
    TrainCatalogInit();
}

void TrainSystem::Update() {
    float dt = GetFrameTime();

    if (gs.events.has(EVENT_START_TRAIN_PLACE)) {
        gs.app.track_editing    = false;
        gs.app.junction_editing = false;
        gs.app.erase_editing    = false;
        gs.app.train_placing    = true;
    }

    if (gs.app.train_placing) {
        if (IsKeyPressed(KEY_ESCAPE)) {
            gs.app.train_placing = false;
        } else if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT) &&
                   !UiMouseInToolbar() && !UiMouseInPanel()) {
            Ray   ray = GetMouseRay(GetMousePosition(), gs.camera.cam);
            float arc = 0.0f;
            int   idx = PickTile(ray, &arc);
            if (idx >= 0 && s_train_count < MAX_TILE_TRAINS) {
                for (int i = 0; i < MAX_TILE_TRAINS; i++) {
                    if (s_trains[i].tile_idx < 0) {
                        s_trains[i] = { 0, idx, arc, 0.0f, 5.0f };
                        s_train_count++;
                        break;
                    }
                }
                gs.app.train_placing = false;
            }
        }
    }

    for (int i = 0; i < MAX_TILE_TRAINS; i++)
        if (s_trains[i].tile_idx >= 0)
            AdvanceTrain(s_trains[i], dt);
}

void TrainSystem::Draw3D() {
    for (int i = 0; i < MAX_TILE_TRAINS; i++) {
        if (s_trains[i].tile_idx < 0) continue;
        const TileTrain& tr = s_trains[i];
        if (tr.tile_idx >= (int)s_tiles.size()) continue;
        int ep_fwd = (tr.target_speed >= 0.0f) ? 1 : 0;
        TrainCatalogDrawConsist(tr.def_idx, tr.tile_idx, tr.arc_dist, ep_fwd);
    }
}

void TrainSystem::Destroy() {
    TrainCatalogDestroy();
}

void TrainSystem::Save(FILE *f) {
    int count = 0;
    for (int i = 0; i < MAX_TILE_TRAINS; i++)
        if (s_trains[i].tile_idx >= 0) count++;

    fprintf(f, "TRAINS %d\n", count);
    for (int i = 0; i < MAX_TILE_TRAINS; i++) {
        const TileTrain& tr = s_trains[i];
        if (tr.tile_idx < 0) continue;
        int dir = (tr.target_speed >= 0.0f) ? 1 : -1;
        fprintf(f, "TRAIN %d %d %f %d\n", tr.def_idx, tr.tile_idx, tr.arc_dist, dir);
    }
}

void TrainSystem::Load(FILE *f) {
    for (int i = 0; i < MAX_TILE_TRAINS; i++)
        s_trains[i].tile_idx = -1;
    s_train_count = 0;

    int count = 0;
    if (fscanf(f, " TRAINS %d", &count) != 1) return;

    for (int i = 0; i < count && i < MAX_TILE_TRAINS; i++) {
        int   def_idx, tile_idx, dir;
        float arc_dist;
        if (fscanf(f, " TRAIN %d %d %f %d", &def_idx, &tile_idx, &arc_dist, &dir) != 4) break;
        const TrainDef *def = TrainCatalogGet(def_idx);
        float max_speed = def ? def->max_speed : 5.0f;
        s_trains[i] = { def_idx, tile_idx, arc_dist, 0.0f, (float)dir * max_speed };
        s_train_count++;
    }
}
