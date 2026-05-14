
# Trains 2D


A top down 2d trains simulation game writtein in C with Raylib

# The Graph — core concept

## What it is

The graph is a simplified map of the track network that lets trains plan routes. Instead of thinking about hundreds of individual bezier curve segments, it models the track as a small set of **nodes** and **arcs**.

```
  [Station A] ---arc 0--- [Switch] ---arc 1--- [Station B]
                               \
                                ---arc 2--- [Station C]
```

A **node** is any interesting point on the network:
- a switch / turnout (where the track branches)
- a station platform entry/exit
- a user-placed signal

An **arc** is everything in between — a straight run of track from one node to the next, which may contain many bezier segments internally. The arc stores their indices in order so a train knows exactly which segments to traverse.

## The data structures

```c
GraphNode {
    Vector2 pos;           // world position
    int     arcs[];        // which arcs touch this node
    int     arc_ends[];    // am I node_a or node_b of that arc?
    bool    is_switch;
    bool    is_station;
    bool    is_signal;
    int     station_idx;   // index into placed_stations[], -1 if not a station
    int     network_id;    // which connected island this node belongs to
}

GraphArc {
    int   node_a, node_b;  // the two nodes this arc connects
    int   segs[];          // the raw bezier segments between them, in order
    int   seg_sides[];     // which end of each segment to enter from (0=p0, 1=p3)
    float length;          // total length in pixels — used by A*
    int   occupied_by;     // -1 = free, else train index (for signals/blocking)
    ArcDirection direction;// ARC_DIR_BOTH / A_TO_B / B_TO_A (one-way tracks)
}
```

## How it is built (graph_rebuild)

Every time the layout changes, `graph_rebuild()` runs four steps:

1. **Find nodes** — scan every segment endpoint. If three or more segments meet there (`seg_adj_count >= 2`), it is a switch. If `force_node` is set (user signal), it is a signal node. Station pins are also turned into nodes.

2. **Trace arcs** — starting from each node, walk forward through segments until another node is reached, collecting every segment along the way into a `GraphArc`. A `claimed[]` flag prevents the same segment from appearing in two arcs.

3. **Flag has_signal** — mark arcs whose endpoints are switches, stations, or signals. Trains use this to know where to check for blocking.

4. **Assign network_id** — BFS flood-fill across arcs. Nodes that end up with the same `network_id` are reachable from each other. `graph_astar` returns false immediately if the two nodes are on different networks.

## How A* uses it

`graph_astar(from_node, to_node, ...)` runs a standard A* over the node/arc graph:

- **g-cost** — sum of `arc.length` along the path so far (real distance)
- **h-cost (heuristic)** — straight-line pixel distance to the target node
- **One-way check** — if `arc.direction == ARC_DIR_B_TO_A` and the train is sitting at `node_a`, that arc is skipped
- Returns an ordered list of `(arc_idx, direction)` pairs the train will follow

The train stores this list in `path_arcs[]` / `path_arc_dirs[]` and walks through it segment by segment using `graph_arc_seg_at()`.

## Directional arcs (one-way track)

Every arc has a `direction` field with three possible values:

```c
ARC_DIR_BOTH    =  0   // bidirectional — trains can travel either way (default)
ARC_DIR_A_TO_B  =  1   // one-way: only node_a → node_b is allowed
ARC_DIR_B_TO_A  = -1   // one-way: only node_b → node_a is allowed
```

The direction is **derived automatically** from the underlying `TrackSegment.direction` field of the first segment in the arc when `graph_rebuild` runs. You never set it on the arc directly — you set it on the segment and the graph picks it up.

```
  [A] ----arc (A→B only)----> [B]   trains can go A→B, not B→A
  [A] <---arc (B→A only)----- [B]   trains can go B→A, not A→B
  [A] ----arc (both)--------- [B]   trains can go either way
```

A* enforces this in `graph_astar` (`graph.c:376`). When expanding a node's neighbours it skips any arc whose direction contradicts the intended travel direction:

```c
if (aend == 0 && adir == ARC_DIR_B_TO_A) continue;  // at node_a, can't go A→B
if (aend == 1 && adir == ARC_DIR_A_TO_B) continue;  // at node_b, can't go B→A
```

So a one-way arc is simply invisible to A* from the wrong end — the pathfinder will route around it automatically, or return false if there is no valid path.

**Current state:** `TrackSegment.direction` defaults to `0` (both), so all arcs are bidirectional until a UI is added to let the player mark a segment as one-way. No graph code needs to change when that UI is built — the rebuild will propagate the flag automatically.

## Signals

Signals are user-placed points that split an arc into two separate arcs, creating a new node in the graph. This lets `occupied_by` block just the section of track between two signals rather than the entire run between stations.

Placement flow:
1. `graph_signal_add(world_pos)` snaps to the nearest track point and records it in `gs.graph.user_signal_pos[]`.
2. `layout_rebuild_segments()` is called, which calls `graph_signals_inject()` — this physically splits the bezier segment at that point (`segment_split`) and sets `force_node` on both halves.
3. `graph_rebuild()` then sees the `force_node` flags and creates a proper graph node there.

## Quick reference — what to call

| Goal | Function |
|---|---|
| Find the node for a station | `find_station_node(station_idx)` |
| Plan a route between two stations | `graph_astar(from_node, to_node, arcs, dirs, &count)` |
| Resolve arc + position → segment | `graph_arc_seg_at(arc, dir, vpos, &seg, &entry_side)` |
| Place a signal at a world point | `graph_signal_add(world_pos)` |
| Remove the nearest signal | `graph_signal_remove_nearest(world_pos)` |
| Rebuild after layout change | called automatically by `layout_rebuild_segments()` |

# Bugs

## Bug a
now there are two more bugs: a) the trains stops at exact the starting point of the station, it should be more realistic, and the train should start along the platform not outside of the platform

## Bug b

why curves misalign: draw_arc_piece draws using arc_point_at = true circle math (center + radius * cos/sin). But layout_rebuild_segments converts each arc into a bezier via segment_make, which guesses the exit tangent as normalize(p3 − p1) — not the real circle tangent at p3. So the train follows an inaccurate bezier while the visual track is a true circle.

### Codebase Refinement Roadmap

The following structural improvements are planned but not yet implemented. They are independent of gameplay features and can be done in any order.

**1. Naming convention**

All public functions should follow `<module>_<verb>` snake_case. Currently the UI module is inconsistent:

| Current | Target |
|---------|--------|
| `UiDrawToolbar()` | `ui_draw_toolbar()` |
| `UiDrawLeftPanel()` | `ui_draw_left_panel()` |
| `UiDrawRoutesWorld()` | `ui_draw_routes_world()` |
| `UI_Draw()` | `ui_draw()` |
| `UI_ShowConfirm()` | `ui_confirm_show()` |
| `UI_IsActive()` | `ui_is_active()` |

One mechanical find-and-replace pass across all `.c`/`.h` files, low risk.

**2. Centralised input (InputFrame)**

Every system currently calls `IsKeyPressed`/`IsMouseButtonPressed` directly. The cleaner pattern is to poll all input once per frame into a struct and pass it down:

```c
// io/input.h
typedef struct {
    bool escape, f1, g, x, r;
    bool lmb_pressed, rmb_pressed, mmb_pressed, mmb_down;
    bool lmb_released_mmb;
    Vector2 mouse_screen;
    Vector2 mouse_world;   // pre-transformed by camera
    float   scroll;
} InputFrame;

InputFrame input_poll(const GameCamera *cam);
```

`main.c` calls `input_poll` once, then passes `&in` to `layout_update(&in)`, `camera_update(&in)`, etc. Systems stop calling raylib input functions directly. Benefits: one place to see all active inputs, easy to add input remapping, systems become easier to test.

#### How InputFrame works

Instead of every system asking raylib "was this key pressed?", one function asks raylib everything at the start of the frame and stores the answers in a plain struct. That struct is then passed to each system.

**Current situation — each system polls raylib directly:**

```c
// camera.c
void camera_update(void) {
    if (IsMouseButtonPressed(MOUSE_BUTTON_MIDDLE)) { ... }
    if (IsKeyDown(KEY_W)) { ... }
}

// layout.c
void layout_update(void) {
    if (IsKeyPressed(KEY_ESCAPE)) { ... }
    if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) { ... }
}
```

Raylib's `IsKeyPressed` returns true only on the first call per frame — subsequent calls return false. So call order starts to matter and you can't tell from reading one file what input is consumed elsewhere.

**With InputFrame — polled once, passed everywhere:**

```c
// io/input.h
typedef struct {
    bool escape, x, r, f1, g;
    bool lmb, rmb, mmb;
    bool mmb_down;
    Vector2 mouse_screen;
    Vector2 mouse_world;   // pre-transformed by camera
    float   scroll;
} InputFrame;

InputFrame input_poll(const GameCamera *cam) {
    InputFrame in = {0};
    in.escape       = IsKeyPressed(KEY_ESCAPE);
    in.x            = IsKeyPressed(KEY_X);
    in.lmb          = IsMouseButtonPressed(MOUSE_BUTTON_LEFT);
    in.mmb_down     = IsMouseButtonDown(MOUSE_BUTTON_MIDDLE);
    in.mouse_screen = GetMousePosition();
    in.mouse_world  = GetScreenToWorld2D(in.mouse_screen, cam->cam);
    in.scroll       = GetMouseWheelMove();
    // ...
    return in;
}
```

```c
// main.c — called ONCE per frame
InputFrame in = input_poll(&gs.camera);

camera_update(&in);
layout_update(&in);
ui_update(&in);
```

```c
// camera.c — reads from the struct, never calls raylib
void camera_update(const InputFrame *in) {
    if (in->mmb_down) { ... }
    if (in->scroll != 0.0f) { ... }
}

// layout.c
void layout_update(const InputFrame *in) {
    if (in->escape) { ... }
    if (in->lmb)    { ... }
}
```

Key benefits:
- `IsKeyPressed` is called only once — no ambiguity about who consumed it
- `mouse_world` is pre-transformed once instead of each system calling `GetScreenToWorld2D` separately
- The entire input surface of the game is visible in one struct definition
- Remapping only requires changing `input_poll`; nothing else moves

The struct is a stack value created in `main.c`, costs nothing, and lives only for that frame.

---

**3. Folder structure**

The flat `src/` directory will grow unwieldy. Proposed layout:

```
src/
  core/          types.h, constants split out from types.h, colors.h
  state/         game_state.h, game_state.c
  systems/       camera, layout, train, graph, route, track
  ui/            ui, toolbar, panels (split from monolithic ui.c)
  io/            savegame, input
  render/        render.c
  external/      raygui.h (already here)
  main.c
```

Requires updating the Makefile from `$(wildcard src/*.c)` to `$(wildcard src/**/*.c)` or explicit paths.

---

### Implementation Plan


1. Basic passenger/station logic
   - Stations: placeable nodes snapped to grid, attached to a nearby track segment at a fixed arc position
   - Passengers: counters per station representing waiting passengers with a destination station
   - Train routes: manually assigned ordered list of stations a train visits in sequence
   - Train stops: train halts briefly at each station, picks up passengers bound for its next stops, drops off arrivals
2. Schedules and management UI


### Train Dispatch Error

Looking at compute_snap in track.c — the bezier segment placement tool only snaps to other segment endpoints or grid, never to station/junction piece pins. So stations and bezier segments are two parallel systems that don't share endpoint positions.

The BFS I wrote assumed segment endpoints land exactly on station straights, but they don't. The fix is to abandon BFS-based pathfinding and
instead:

1. Find any segment that passes near station[0] (sampling multiple points along each segment with CheckCollisionPointLine)
2. Use the existing free-roaming navigation (find_next_segment) — which already ping-pongs at dead ends
3. Detect station arrival using CheckCollisionPointLine against the target station's straights


# You must implement:

Track graph structure

A\* pathfinding

Train movement along paths

Station logic

Cargo logic

Signals / routing

Economy

Save/load

Game rules


# Layout .c / .h

layout.h declares the public interface for everything related to placing and managing track pieces and stations in the world. layout.c implements
  it. Here's a breakdown by responsibility:

  ---
  Data it owns

  PlacedPiece   placed_pieces[MAX_PLACED];    // all placed junction/track pieces
  int           placed_count;

  PlacedStation placed_stations[MAX_PLACED_STATIONS]; // all placed stations
  int           placed_station_count;

  These are the two authoritative arrays for the layout. Everything else (segments, junctions, trains) is derived from them.

  ---
  The segment network (layout_rebuild_segments)

  This is the most critical function. Whenever the layout changes it rebuilds segments[] and arc_luts[] (in track.c) from scratch:

  1. Iterates every placed_pieces[i].world_geom — converts each straight and arc into a Bezier TrackSegment and stores it in the global segments[]
  array.
  2. Iterates every placed_stations[i].world_geom — same thing for the station's own platform track.
  3. Calls junctions_rebuild() so the junction graph stays in sync.

  Trains and routing only work through segments[], so this rebuild must happen any time pieces or stations are added or removed.

  ---
  Placement state machine (layout_update)

  Per-frame logic driven by mouse and keyboard. It has three modes:

  Normal — handles double-click to rename a station, and X key to enter delete mode.

  Delete mode — highlights the nearest piece on hover; left-click calls delete_piece() and rebuilds segments.

  Placing (piece or station selected in the panel):
  - R rotates the ghost.
  - Each frame calls compute_snap() to find the nearest open pin within SNAP_PIN_RADIUS. If found, the ghost snaps precisely to that pin and the
  rotation is adjusted so the two pins face each other (angles differ by π).
  - For pieces, the attach pin is determined by which ghost pin is geometrically closest to the target — so you can connect any end of the piece, not
   just pin 0.
  - For stations, the snap always aligns pin 0 (left entry) to the target pin.
  - On left-click, the ghost is committed: connections are recorded bidirectionally (connections[] on the new entity AND record_back on the target),
  the entity is appended to its array, and layout_rebuild_segments() is called.

  ---
  Connectivity (PinConn connections[])

  Each PlacedPiece and PlacedStation carries a connections[GEOM_MAX_PINS] array. Each slot is either CONN_NONE or points to another entity
  (CONN_PIECE or CONN_STATION) with its index and pin index. This is a bidirectional graph — when A connects to B, both A's and B's arrays are
  updated. The snap system uses it to skip already-occupied pins.

  delete_piece() maintains this graph: it erases all back-references to the deleted piece across both arrays, compacts the placed_pieces array, and
  re-indexes any references that pointed past the removed element.

  ---
  Drawing

  - layout_draw() — draws all pieces and stations (with delete-hover highlight); while placing, also draws the open-pin indicators (orange circles +
  direction lines) so the user knows where they can snap.
  - layout_draw_placement() — draws the floating ghost (green = will snap, red = blocked, translucent blue = free), with a snap ring when snapping.
  - layout_draw_overlay() — screen-space pass: station name labels above each station, plus the name-editing modal.
  - layout_hint() — returns the context-sensitive hint string shown at the bottom of the screen.

  ---
  Public API summary (layout.h)

  ┌───────────────────────────────────────────┬────────────────────────────────────────────────┐
  │                 Function                  │                    Purpose                     │
  ├───────────────────────────────────────────┼────────────────────────────────────────────────┤
  │ layout_select / layout_select_station     │ Pick what's being placed                       │
  ├───────────────────────────────────────────┼────────────────────────────────────────────────┤
  │ layout_deselect                           │ Cancel placement                               │
  ├───────────────────────────────────────────┼────────────────────────────────────────────────┤
  │ layout_is_placing / layout_is_delete_mode │ Query current mode                             │
  ├───────────────────────────────────────────┼────────────────────────────────────────────────┤
  │ layout_rebuild_segments                   │ Regenerate segments[] from placed geometry     │
  ├───────────────────────────────────────────┼────────────────────────────────────────────────┤
  │ layout_clear                              │ Reset everything (New Track)                   │
  ├───────────────────────────────────────────┼────────────────────────────────────────────────┤
  │ layout_hit_station                        │ World-pos → station index (for route building) │
  ├───────────────────────────────────────────┼────────────────────────────────────────────────┤
  │ layout_is_name_editing                    │ Suppress other input while typing a name       │
  └───────────────────────────────────────────┴────────────────────────────────────────────────┘


  # Internal structure


  There are actually two separate representations that serve different purposes, and they don't overlap.

  ---
  1. The placement graph (connections[])

  placed_pieces[i].connections[p] and placed_stations[i].connections[p] form a bidirectional adjacency graph where nodes are placed entities and edges are pin connections.

  Only used during placement — compute_snap reads it to skip already-occupied pins, and delete_piece maintains it when removing a piece. Trains never touch it.

  ---
  2. The navigation layer (segments[] + junctions[])

  This is what trains actually run on. It's a flat derived structure rebuilt by layout_rebuild_segments() whenever the layout changes:

  - segments[] — a flat array of Bezier curves, one per straight/arc in the world. No graph structure, just geometry.
  - arc_luts[] — arc-length lookup tables for each segment, so the train can move at constant speed.
  - junctions[] — built by junctions_rebuild(): scans all segment endpoints and creates a junction only where 3 or more segment  ends meet within 1 pixel. Stores which segment pairs are the active through-route.

  Train navigation (find_next_segment in train.c) is purely geometric:
  1. Check if a junction exists at the current endpoint (within 1px) → follow the junction's active pair.
  2. Otherwise, scan all segments for one whose p0 or p3 is within 1px of the current position and whose tangent aligns with the exit direction →
  that's the next segment.

  No graph traversal, no pointer following — just proximity checks on a flat array.

  ---
  Summary

  ┌─────────────────┬──────────────────────────────────────┬─────────────────────────────────────┐
  │      Layer      │              Structure               │               Used by               │
  ├─────────────────┼──────────────────────────────────────┼─────────────────────────────────────┤
  │ Placement graph │ connections[] on each entity         │ Snap system only                    │
  ├─────────────────┼──────────────────────────────────────┼─────────────────────────────────────┤
  │ Navigation      │ segments[] + junctions[] flat arrays │ Trains, dispatch, arrival detection │
  └─────────────────┴──────────────────────────────────────┴─────────────────────────────────────┘

  The connections[] graph is essentially unused at runtime — it encodes the same topology as the segment endpoint proximity, but trains rediscover connectivity geometrically on the fly. The two layers are kept in sync only in the sense that layout_rebuild_segments regenerates the navigation layer from the placed entities whenever something changes.


  # Graph Improvement


  could this be conceptualy improved? is there a better algorithm to handle this

● Yes, there are real conceptual problems with the current design and a cleaner architecture. Let me lay it out.

  ---
  What's wrong now

  The topology is encoded twice but used inconsistently.

  You have connections[] — an explicit bidirectional graph of which pins connect to which. But trains completely ignore it. Instead find_next_segment
   rediscovers connectivity at runtime by scanning all segments for endpoint proximity (< 1px). You're throwing away a graph you already built and
  re-deriving it geometrically on every segment transition.

  Junctions are a special case bolted on top. They only exist for 3+ endpoints, so 2-segment connections use a different code path (the proximity
  scan fallback). This inconsistency means there are actually two navigation algorithms running.

  ---
  The better architecture

  Build an adjacency table once at rebuild time, derived from connections[], and use it exclusively for navigation.

  typedef struct {
      int seg_idx;  // -1 = dead end
      int side;     // 0 = enter at p0, 1 = enter at p3
  } SegAdj;

  // Per segment, per side (0=p0 end, 1=p3 end), up to N adjacencies
  // (usually 1, at a turnout heel: 2)
  SegAdj seg_adj[MAX_SEGMENTS][2][MAX_JUNC_ENDS];
  int    seg_adj_count[MAX_SEGMENTS][2];

  During layout_rebuild_segments you walk the connections[] graph to populate this. find_next_segment becomes a direct array lookup — no scan, no
  proximity test.

  The "junction active pair" becomes an index into seg_adj[seg][side] for the end that has multiple options. The junctions[] array simplifies to just
   storing which option is currently selected for multi-exit points.

  ---
  Bigger picture: node-arc graph

  For a more capable simulation you'd go one step further and model it as a proper node-arc graph:

  - Nodes = stations and switches (anything with more than 2 connections)
  - Arcs = track segments between nodes, with a measured length
  - Train position = (arc_id, distance_along_arc) — which is already what you have

  This gives you automatic route finding (Dijkstra / A* between station nodes), conflict detection (two trains on the same arc), and signal blocks
  naturally — because arcs are the atomic unit of reservation.

  ---
  Practical recommendation

  For this codebase the highest-value change is step one: replace the proximity scan with an adjacency table built from connections[]. That
  eliminates the dual-encoding, makes navigation O(1), and collapses the junction special-case into the same lookup. It's a contained refactor inside
   layout_rebuild_segments and find_next_segment — the rest of the code stays the same.

  The node-arc graph is the right long-term direction if you want automatic routing or conflict detection, but it's a larger redesign.


  # Move to C++?


  Short answer: not significantly, and possibly not at all for this codebase.

  ---
  Why C++ wouldn't help much here

  The complexity in this code isn't the kind C++ solves. There's no:
  - Deep class hierarchies where virtual dispatch matters
  - Generic algorithms that need templates
  - Resource management complexity where RAII would help (no heap allocation at all — everything is flat global arrays)
  - String manipulation or container gymnastics

  The "hard" parts are geometry math, rendering calls, and the navigation algorithm — C++ adds nothing to those.

  ---
  What you'd actually gain

  struct methods — instead of geom_transform(&g, pos, rot) you'd write g.transform(pos, rot). Cosmetic.

  Operator overloading — raylib already gives you Vector2Add etc. With C++ you could write pos + dir * speed. Nice but shallow.

  std::vector instead of fixed-size arrays. The only real flexibility gain, but your current fixed arrays are intentional (bounded memory, no
  allocator) and work fine at this scale.

  ---
  The library worth knowing: raylib works fine in C++

  raylib itself compiles as C++. Some people use it with a thin C++ wrapper called raylib-cpp which gives you the operator overloads and method
  syntax above. You'd get:

  // instead of:
  Vector2Add(Vector2Scale(dir, speed), pos)
  // you'd write:
  pos + dir * speed

  That's the realistic upside.

  ---
  What would actually reduce complexity

  The geometry and math verbosity you feel is a raymath problem, not a C vs C++ problem. You could solve it in pure C by adding a few local inline
  helpers:

  static inline Vector2 v2(float x, float y)   { return (Vector2){x, y}; }
  static inline Vector2 v2_lerp(Vector2 a, Vector2 b, float t) { ... }

  The architectural complexity (dual topology encoding, proximity-scan navigation) is fixed by the graph refactor we discussed — language has no
  bearing on that.

  ---
  Recommendation

  Stay in C. The codebase is clean, flat, and well-structured for what it is. The only concrete improvement a language switch would give you is pos +
   dir * speed instead of Vector2Add(pos, Vector2Scale(dir, speed)) — and you can get that in C with a macro or inline wrapper if it bothers you.


# Useful helpers

```c
Vector2 dir = { 1.0f, 0.5f };   // example direction
float angle = atan2f(dir.y, dir.x) * RAD2DEG;

// Normalize direction if needed
float len = sqrtf(dir.x*dir.x + dir.y*dir.y);
if (len > 0) dir = (Vector2){ dir.x/len, dir.y/len };
```

| Direction vector | Expected angle |
| --- | --- |
| ``{1, ``0}`` | ``0°`` |
| ``{0, ``1}`` | ``90°`` |
| ``{-1, ``0}`` | ``180°`` |
| ``{0, ``-1}`` | ``-90°`` |