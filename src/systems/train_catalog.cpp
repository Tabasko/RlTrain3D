#include "train_catalog.h"
#include "track_geom.h"
#include "track_tiles.h"
#include "raylib.h"
#include "raymath.h"
#include <cmath>
#include <cstring>

// ---------------------------------------------------------------------------
// Catalog
// ---------------------------------------------------------------------------

static const TrainDef CATALOG[] = {
    {
        "Tram",
        5.0f,  // max_speed  (world units/sec)
        1.5f,  // accel      (world units/sec²)
        {
            { "assets/trains/tram.glb", 4.0f, 0.25f },
        },
        1
    },
};

static constexpr int CATALOG_COUNT = (int)(sizeof(CATALOG) / sizeof(CATALOG[0]));

// ---------------------------------------------------------------------------
// Model cache — path-keyed so shared wagon meshes load only once.
// ---------------------------------------------------------------------------

struct ModelEntry {
    const char *path;
    Model       model;
    bool        loaded;
};

// Generous upper bound: entries across all catalog types × cars per type.
static ModelEntry s_cache[MAX_CARS_PER_TRAIN * 4];
static int        s_cache_count = 0;

static Model *GetOrLoadModel(const char *path) {
    for (int i = 0; i < s_cache_count; i++)
        if (strcmp(s_cache[i].path, path) == 0)
            return s_cache[i].loaded ? &s_cache[i].model : nullptr;

    if (s_cache_count >= (int)(sizeof(s_cache) / sizeof(s_cache[0]))) {
        TraceLog(LOG_WARNING, "CATALOG: model cache full, cannot load %s", path);
        return nullptr;
    }

    ModelEntry& e = s_cache[s_cache_count++];
    e.path = path;
    if (FileExists(path)) {
        e.model  = LoadModel(path);
        e.loaded = true;
    } else {
        TraceLog(LOG_WARNING, "CATALOG: model not found: %s", path);
        e.loaded = false;
    }
    return e.loaded ? &e.model : nullptr;
}

// ---------------------------------------------------------------------------
// Tile geometry — local to this TU, used for car positioning.
// ---------------------------------------------------------------------------

static void TileLocalPosHeading(TileType type, float d,
                                Vector3 *pos_out, float *heading_out) {
    const TileGeom& g = s_geom[type];
    float t  = (g.length > 0.0f) ? (d / g.length) : 0.0f;
    float eh = g.exit_heading;

    if (fabsf(eh) < 0.001f) {
        *pos_out     = Vector3Scale(g.exit_pos, t);
        *heading_out = 0.0f;
    } else {
        float theta  = fabsf(eh);
        float R      = g.length / theta;
        float phi    = theta * t;
        float sign   = (eh > 0.0f) ? 1.0f : -1.0f;
        *pos_out     = { sign * R * sinf(phi), 0.0f, R * (1.0f - cosf(phi)) };
        *heading_out = sign * phi;
    }
}

static void TileWorldPosHeading(int tile_idx, float arc,
                                Vector3 *pos_out, float *heading_out) {
    const PlacedTile& tile = s_tiles[tile_idx];
    Vector3 local; float local_h;
    TileLocalPosHeading(tile.type, arc, &local, &local_h);
    *pos_out     = Vector3Add(tile.eps[0].pos, RotateByHeading(local, tile.eps[0].heading));
    *heading_out = NormAngle(tile.eps[0].heading + local_h);
}

// ---------------------------------------------------------------------------
// Backward walk
//
// Walk dist world units "backward" (opposite to ep_fwd) along the tile graph
// from (tile_idx, arc). Fills the output position and the ep_fwd value on the
// output tile (needed to derive the correct car heading there).
// Stacks at open endpoints and junctions rather than falling off the graph.
// ---------------------------------------------------------------------------
static void WalkBack(int tile_idx, float arc, int ep_fwd, float dist,
                     int *out_tile, float *out_arc, int *out_ep_fwd) {
    int   ep_back = 1 - ep_fwd;
    int   cur     = tile_idx;
    float cur_arc = arc;
    float rem     = dist;

    while (rem > 0.0f) {
        float tile_len = s_geom[s_tiles[cur].type].length;
        float avail    = (ep_back == 0) ? cur_arc : (tile_len - cur_arc);

        if (rem <= avail) {
            *out_tile   = cur;
            *out_arc    = (ep_back == 0) ? (cur_arc - rem) : (cur_arc + rem);
            *out_ep_fwd = 1 - ep_back;
            return;
        }

        rem -= avail;
        const TileEndpoint& ep = s_tiles[cur].eps[ep_back];

        // Open endpoint or junction (linked_tile == -1): stack here.
        if (ep.linked_tile == TILE_NO_LINK) {
            *out_tile   = cur;
            *out_arc    = (ep_back == 0) ? 0.0f : tile_len;
            *out_ep_fwd = 1 - ep_back;
            return;
        }

        int next_ep = ep.linked_ep;
        cur         = ep.linked_tile;
        cur_arc     = (next_ep == 0) ? 0.0f : s_geom[s_tiles[cur].type].length;
        ep_back     = 1 - next_ep; // continue away from the entry endpoint
    }

    *out_tile   = cur;
    *out_arc    = cur_arc;
    *out_ep_fwd = 1 - ep_back;
}

// ---------------------------------------------------------------------------
// Public interface
// ---------------------------------------------------------------------------

void TrainCatalogInit() {
    for (int d = 0; d < CATALOG_COUNT; d++)
        for (int c = 0; c < CATALOG[d].car_count; c++)
            GetOrLoadModel(CATALOG[d].cars[c].model_path);
}

void TrainCatalogDestroy() {
    for (int i = 0; i < s_cache_count; i++)
        if (s_cache[i].loaded)
            UnloadModel(s_cache[i].model);
    s_cache_count = 0;
}

const TrainDef *TrainCatalogGet(int idx) {
    if (idx < 0 || idx >= CATALOG_COUNT) return nullptr;
    return &CATALOG[idx];
}

int TrainCatalogCount() { return CATALOG_COUNT; }

void TrainCatalogDrawConsist(int def_idx, int front_tile, float front_arc, int ep_fwd) {
    const TrainDef *def = TrainCatalogGet(def_idx);
    if (!def || front_tile < 0 || front_tile >= (int)s_tiles.size()) return;

    float offset = 0.0f; // accumulated distance from front coupling

    for (int c = 0; c < def->car_count; c++) {
        const CarDef& car = def->cars[c];
        Model *m = GetOrLoadModel(car.model_path);
        if (!m) { offset += car.length + CAR_COUPLING_GAP; continue; }

        // Place the model at the car's center, half a car-length behind the coupling.
        float center_offset = offset + car.length * 0.5f;
        int ct; float ca; int car_ep_fwd;
        WalkBack(front_tile, front_arc, ep_fwd, center_offset, &ct, &ca, &car_ep_fwd);

        Vector3 pos; float tile_heading;
        TileWorldPosHeading(ct, ca, &pos, &tile_heading);

        // Flip 180° when the car is traversing the tile toward ep[0].
        float car_heading = (car_ep_fwd == 1) ? tile_heading
                                               : NormAngle(tile_heading + (float)M_PI);

        DrawModelEx(*m, pos,
                    (Vector3){ 0.0f, 1.0f, 0.0f },
                    car_heading * RAD2DEG,
                    (Vector3){ car.scale, car.scale, car.scale },
                    WHITE);

        offset += car.length + CAR_COUPLING_GAP;
    }
}
