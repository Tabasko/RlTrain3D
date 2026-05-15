#pragma once

#define GLSL_VERSION 330

#include "raylib.h"
#include "raymath.h"
#include <math.h>
#include <stdbool.h>
#include <string.h>


// ---------------------------------------------------------------------------
// UI
// ---------------------------------------------------------------------------
#define UI_SCALE 2.0f
#define TOOLBAR_H ((int)(36 * UI_SCALE))
#define PANEL_W ((int)(80 * UI_SCALE))

// ---------------------------------------------------------------------------
// Track geometry scale
// ---------------------------------------------------------------------------
#define TRACK_UNIT 16.0f // pixels per abstract unit

// ---------------------------------------------------------------------------
// World / grid
// ---------------------------------------------------------------------------
#define GRID_CELL 1

// ---------------------------------------------------------------------------
// Track
// ---------------------------------------------------------------------------
#define TRACK_BALLAST_W 8.0f
#define TRACK_RAIL_OFFSET 2.5f
#define TRACK_RAIL_W 1.2f
#define TRACK_TIE_SPACING 8.0f
#define TRACK_TIE_LEN 6.0f
#define TRACK_TIE_W 1.5f
#define BEZIER_CP_FACTOR 0.4f
#define BEZIER_STEPS 48

// ---------------------------------------------------------------------------
// Train
// ---------------------------------------------------------------------------
#define TRAIN_LENGTH 22.0f
#define TRAIN_WIDTH 9.0f
#define TRAIN_CAB_LEN 6.0f
#define TRAIN_SPEED 80.0f
#define ARRIVAL_THRESHOLD 10.0f
#define TRAIN_WAIT_TIME 5.0f

typedef enum {
  TRAIN_SPEED_T,
  TRAIN_CARGO,
  TRAIN_SUBWAY,
  TRAIN_TYPE_COUNT
} TrainType;

// ---------------------------------------------------------------------------
// Junction
// ---------------------------------------------------------------------------
#define JUNCTION_RADIUS 7.0f
#define JUNCTION_STUB 14.0f

// ---------------------------------------------------------------------------
// Capacity limits
// ---------------------------------------------------------------------------
#define MAX_SEGMENTS 1024
#define ARC_LUT_SIZE 64
#define MAX_JUNC_ENDS 6
#define MAX_TRAINS 64
#define MAX_CARS_PER_TRAIN 12 // max cars in any TrainDef entry

// Depth of the segment history ring buffer stored in each Train.
// Must cover (longest_train_px / shortest_segment_px) past segments.
// At 24 entries × ~31 px/entry (Arc R15 7.5°) = ~744 px — enough for
// the largest planned train at maximum consist length.
#define TRAIN_SEG_HISTORY 24

// ---------------------------------------------------------------------------
// Node-arc graph limits
// ---------------------------------------------------------------------------
#define MAX_GRAPH_NODES 256
#define MAX_GRAPH_ARCS 256
#define MAX_NODE_ARCS 8 // arcs per node (a 4-way crossing has 4)
#define MAX_ARC_SEGS 64 // segments per arc

// ---------------------------------------------------------------------------
// One-way track direction
//
// Segments and graph arcs carry a direction constraint set at build time.
// ARC_DIR_BOTH   — bidirectional (default, legacy behaviour).
// ARC_DIR_A_TO_B — trains may only traverse from node_a toward node_b.
// ARC_DIR_B_TO_A — trains may only traverse from node_b toward node_a.
//
// For TrackSegment the convention follows the bezier: direction +1 means
// p0→p3 is the allowed direction, -1 means p3→p0, 0 means both.
// ---------------------------------------------------------------------------
typedef enum {
  ARC_DIR_BOTH   = 0,
  ARC_DIR_A_TO_B = 1,
  ARC_DIR_B_TO_A = -1,
} ArcDirection;

// ---------------------------------------------------------------------------
// Tile-based track system
// ---------------------------------------------------------------------------

// Catalog of pre-baked tile shapes. Left-curve variants share the right-curve
// mesh but are rendered with an X-negated world transform.
typedef enum {
  TILE_STRAIGHT_S   = 0, // short straight (~4 m)
  TILE_STRAIGHT_L,       // long straight  (~8 m)
  TILE_CURVE_R1_15,      // R=20 m, 15° right arc
  TILE_CURVE_R1_15_L,    // R=20 m, 15° left  arc
  TILE_CURVE_R2_15,      // R=40 m, 15° right arc
  TILE_CURVE_R2_15_L,    // R=40 m, 15° left  arc
  TILE_CURVE_R2_30,      // R=40 m, 30° right arc
  TILE_CURVE_R2_30_L,    // R=40 m, 30° left  arc
  TILE_TYPE_COUNT
} TileType;

// One endpoint of a placed tile.
typedef struct {
  Vector3 pos;              // world-space position
  float   heading;          // world-space heading in radians (0 = +Z)
  int     linked_tile;      // index into placed-tile array, -1 = open
  int     linked_ep;        // endpoint index on the linked tile
  int     linked_junction;  // index into s_junctions, -1 if not part of one
} TileEndpoint;

// One placed tile instance.
typedef struct {
  TileType     type;
  ArcDirection direction; // traversal constraint
  Matrix       world;     // world transform, computed once at placement
  TileEndpoint eps[2];    // [0]=entry  [1]=exit
  int          ep_count;  // 2 for straight/curve
} PlacedTile;

// One leg of a 3-way junction — identifies which tile endpoint participates.
typedef struct {
    int tile_idx;
    int ep_idx;
} JunctionLeg;

// 3-way junction node. legs[0] is the stem; thrown selects the active branch:
//   thrown=0  →  legs[0] ↔ legs[1]
//   thrown=1  →  legs[0] ↔ legs[2]
typedef struct {
    Vector3     pos;
    JunctionLeg legs[3];
    int         leg_count; // grows as tracks connect; functional at 3
    int         thrown;    // 0 or 1
} JunctionNode;

// Detail level derived from camera zoom each frame.
// All draw functions read this to choose their rendering path.
typedef enum {
  ZOOM_OVERVIEW =
      0, // zoom < 1.5 — schematic map: thin lines, large labels, train dots
  ZOOM_NORMAL = 1, // zoom 1.5–3.5 — standard gameplay
  ZOOM_DETAIL = 2, // zoom > 3.5 — close-up inspection
} ZoomLevel;

// ---------------------------------------------------------------------------
// Structs
// ---------------------------------------------------------------------------
typedef struct {
  Vector2 p0, p1, p2, p3;
  Vector2 exit_tangent;
  int     direction; // one-way constraint: 0=both, +1=p0→p3, -1=p3→p0
} TrackSegment;

typedef struct {
  float t, arc;
} ArcEntry;
typedef struct {
  ArcEntry entries[ARC_LUT_SIZE];
  float    total_len;
} ArcLUT;

// ---------------------------------------------------------------------------
// Segment history — one entry per segment the train front has crossed.
//
// arc_dist tracks only the front car.  To render trailing cars we need to
// look up track positions behind the front, which may lie on older segments.
// Each time the front crosses a boundary the departing segment is pushed
// into a ring buffer so trailing cars can sample it.
//
// forward records the traversal direction:
//   true  = front went from arc_dist=0 toward total_len (speed was > 0)
//   false = front went from arc_dist=total_len toward 0 (speed was < 0)
// This is needed because the sample offset must be measured from the exit
// end, which differs between the two directions.
// ---------------------------------------------------------------------------
typedef struct {
  int   seg_idx;   // index into gs.track.segments[]
  float total_len; // full arc length of this segment in world pixels
  bool  forward;   // traversal direction (see above)
} SegHistEntry;

#define MAX_PATH_ARCS 64

typedef struct {
  int       segment_idx;
  float     arc_dist;
  float     speed;
  TrainType type;
  int       route_idx; // -1 = free roaming
  float     wait_timer;

  // Route ping-pong state
  int route_station_pos; // target slot index in route.stations[]
  int ping_dir;          // +1 or -1

  // A* path to current target station (recomputed at each departure)
  int path_arcs[MAX_PATH_ARCS];
  int path_arc_dirs[MAX_PATH_ARCS]; // 0 = node_a→node_b, 1 = node_b→node_a
  int path_arc_count;
  int path_arc_pos;     // current arc index in path_arcs[]
  int path_arc_seg_pos; // virtual segment position within current arc

  // Signal braking — lerp speed toward this each frame
  float speed_limit; // 0=stop, def->speed=full

  // Platform park position — saved when the train arrives at a station so
  // that on departure it resumes from inside the platform, not from the pin.
  int   park_seg_idx;  // segment the train was on when it arrived
  float park_arc_dist; // arc_dist at the moment of arrival

  // Graph node the current A* path ends at (the far-end pin of the target
  // platform). Arrival detection and approach braking use this position so
  // the train stops with its nose at the far end of the platform.
  int target_node;

  // Ring buffer of recently visited segments, newest at seg_history_head.
  // Populated whenever the front crosses a segment boundary.
  // seg_history_count starts at 0 and grows until the buffer is full.
  SegHistEntry seg_history[TRAIN_SEG_HISTORY];
  int          seg_history_head;  // index of the most recently written entry
  int          seg_history_count; // number of valid entries currently stored
} Train;

typedef struct {
  Camera3D  cam;
  float     zoom_target; // lerp destination for smooth zoom
  Vector2   drag_start;
  bool      dragging;
  ZoomLevel zoom_level; // derived from cam.zoom each frame; controls LOD
  Vector2   zoom_anchor_screen; // screen pos of mouse when zoom started
  Vector2   zoom_anchor_world;  // world pos under mouse when zoom started
  bool      zoom_anchored;      // true while zoom lerp is in flight
} GameCamera;

// ---------------------------------------------------------------------------
// Node-arc graph
// ---------------------------------------------------------------------------

typedef struct {
  Vector2 pos;
  int     arcs[MAX_NODE_ARCS];  // GraphArc indices
  int  arc_ends[MAX_NODE_ARCS]; // 0 = this node is arc.node_a, 1 = arc.node_b
  int  arc_count;
  bool is_switch; // seg_adj_count >= 2 on the connecting end
  bool is_station;
  bool is_signal;   // user-placed signal node
  int  station_idx; // placed_stations[] index, -1 if not a station
  int  network_id;  // connected component; -1 = unvisited
} GraphNode;

typedef struct {
  int node_a, node_b;
  int segs[MAX_ARC_SEGS]; // segments[] indices in traversal order from node_a
  int seg_sides[MAX_ARC_SEGS]; // entry side per segment (0=enter at p0, 1=enter
                               // at p3)
  int   seg_count;
  float length;      // sum of arc_luts[seg].total_len
  int   occupied_by; // -1 = free, else train index
  bool  has_signal;  // true when either endpoint is a switch or station
  ArcDirection
      direction; // one-way constraint derived from constituent segments
} GraphArc;

// ---------------------------------------------------------------------------
// Graph connectivity (placement graph)
// ---------------------------------------------------------------------------
typedef enum { CONN_NONE = 0, CONN_PIECE, CONN_STATION } ConnType;

typedef struct {
  ConnType type;
  int      entity_idx; // index into placed_pieces or placed_stations
  int      pin_idx;
} PinConn;
