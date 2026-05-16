#pragma once
#include "../types.h"

// ---------------------------------------------------------------------------
// Component structs — data only, no logic.
// Field names mirror the originating struct so Phase 2+ migration is mechanical.
// ---------------------------------------------------------------------------

typedef enum {
      SIGNAL_PROCEED = 0, // Hp1 — green, full speed
      SIGNAL_CAUTION,     // Hp2 — yellow, reduced speed
      SIGNAL_STOP,        // Hp0 — red, speed = 0
  } SignalState;

typedef struct {
    Vector3     pos;          // world position (endpoint + lateral offset)
    float       radius;       // detection sphere radius
    SignalState aspect;       // current displayed state
    int         tile_idx;     // tile this signal guards
    int         ep_idx;       // endpoint index (approach direction)
    float       rotation_angle; // Y-axis rotation in degrees, baked from heading at placement
} CSignal;

// World position and orientation of any entity.
typedef struct {
    Vector3 pos;
    float   heading; // radians, 0 = +Z
} CTransform;

// Locates a train's front coupling on the tile track.
typedef struct {
    int   tile_idx; // index into placed-tile array; -1 = not on track
    float arc_dist; // distance from eps[0] along the tile arc
} CTileOccupant;

// Signed velocity state for tile-based train movement.
typedef struct {
    float speed;        // current signed velocity (world units/sec)
    float target_speed; // desired signed velocity; flipped on reversal
    float speed_limit;  // braking ceiling: 0 = stop, def->max_speed = full
} CTrainPhysics;

// A* path the train is currently following.
// Mirrors the path fields from TileTrain.
typedef struct {
    int path_arcs[MAX_PATH_ARCS];
    int path_arc_dirs[MAX_PATH_ARCS];
    int path_arc_count;
    int path_arc_pos;
    int path_arc_seg_pos;
    int target_node;
    int route_idx;
    int route_station_pos;
    int ping_dir;
    float wait_timer;
    int   park_seg_idx;
    float park_arc_dist;
} CTrainPath;

// Links a train entity to its static catalog definition.
typedef struct {
    int def_idx; // index into TRAIN_CATALOG
} CTrainDef;

// One placed tile instance on the track network.
typedef struct {
    TileType     type;
    ArcDirection direction;
    Matrix       world;       // world transform, computed once at placement
    TileEndpoint eps[2];      // [0]=entry  [1]=exit
    int          ep_count;
} CTileTrack;

// 3-way junction node.
typedef struct {
    Vector3     pos;
    JunctionLeg legs[3];
    int         leg_count;
    int         thrown; // 0 or 1; selects the active branch
} CJunction;

// Animated world prop (wind turbine, etc.).
typedef struct {
    PropType type;
    Vector3  position;
    float    rotation;       // current rotor angle in radians
    float    rotation_speed; // radians per second
} CProp;

// AABB used for ray-picking a placed tile. Computed once at placement from the
// tile's endpoint positions, padded by the pick tolerance on all four sides.
// tile_idx mirrors the index into s_tiles[] so the hit can be resolved back
// to the PlacedTile without a secondary search.
typedef struct {
    BoundingBox box;
    int         tile_idx;
} CTileBounds;
