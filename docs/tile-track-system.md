# Tile-Based Track System

## Motivation

The previous free-form Catmull-Rom spline approach is expressive but incompatible with
the goals of a realistic railway simulation:

- **Parallel tracks** — offsetting a spline by a fixed distance does not yield another
  valid spline, making passing loops and double-track mainlines unreliable.
- **Junctions** — real turnouts have a fixed diverge geometry; bolting them onto a
  free-form system produces seam and alignment problems.
- **Signals and routing** — block signals sit at predictable points (tile endpoints /
  junction throats). Arbitrary anchors provide no such guarantees.

A tile-based system where every track piece has fixed, baked-in geometry solves all
three problems at the cost of some placement flexibility.

---

## Tile Types

Each tile type maps to one pre-loaded mesh and a constant endpoint definition.
Mirrored variants (left/right curves) share the same mesh with an X-negated transform
— they are not separate mesh files.

| Enum constant       | Description                        | Mesh file              |
|---------------------|------------------------------------|------------------------|
| `TILE_STRAIGHT_S`   | Short straight (~4 m)              | `track-straight-s.glb` |
| `TILE_STRAIGHT_L`   | Long straight (~8 m)               | `track-straight-l.glb` |
| `TILE_CURVE_R1_15`  | Small radius (R=20 m), 15° arc     | `track-curve-r1-15.glb`|
| `TILE_CURVE_R2_15`  | Medium radius (R=40 m), 15° arc    | `track-curve-r2-15.glb`|
| `TILE_CURVE_R2_30`  | Medium radius (R=40 m), 30° arc    | `track-curve-r2-30.glb`|
| `TILE_JUNCTION_L`   | Left-hand turnout (~1:9 diverge)   | `track-junction-l.glb` |
| `TILE_JUNCTION_R`   | Right-hand turnout (~1:9 diverge)  | `track-junction-r.glb` |

Left/right curve mirrors are expressed as `TileType` enum variants but share the same
mesh — the world transform negates X to produce the mirror.

---

## Endpoint Geometry (Local Space)

Every tile's entry endpoint is fixed at the origin with heading 0 (pointing along +Z).
Exit endpoints are derived from the tile's geometry:

```
Straight, length L:
    exit.pos     = (0, 0, L)
    exit.heading = 0

Curve, radius R, angle θ (right-hand turn):
    exit.pos     = (R·sin θ,  0,  R·(1 − cos θ))
    exit.heading = θ

Curve (left-hand mirror):
    exit.pos     = (−R·sin θ, 0,  R·(1 − cos θ))
    exit.heading = −θ

Junction:
    exit_main   = straight exit
    exit_branch = curve exit at the diverge angle
```

These values are constants stored in a lookup table indexed by `TileType`.

---

## Data Structures

```c
// Traversal direction constraint on a placed tile.
// Reuses the existing ArcDirection enum from types.h.
//   ARC_DIR_BOTH   = 0   bidirectional (default)
//   ARC_DIR_A_TO_B = 1   entry → exit only
//   ARC_DIR_B_TO_A = -1  exit → entry only

// One endpoint of a placed tile.
typedef struct {
    Vector3 pos;          // world-space position (derived from world transform)
    float   heading;      // world-space heading in radians
    int     linked_tile;  // index into placed-tile array, -1 = open
    int     linked_ep;    // endpoint index on the linked tile
} TileEndpoint;

// One placed tile instance.
typedef struct {
    TileType     type;
    ArcDirection direction;   // traversal constraint
    Matrix       world;       // world transform, computed once at placement
    TileEndpoint eps[3];      // [0]=entry  [1]=exit  [2]=branch (junctions only)
    int          ep_count;    // 2 for straight/curve, 3 for junction
} PlacedTile;
```

---

## Rendering — GPU Instancing

All tiles of the same type are rendered in a single `DrawMeshInstanced` call.
The instance transform buffer is **rebuilt only when tiles are added or removed**,
not every frame.

```
Per frame:
    for each TileType t:
        if instance_count[t] > 0:
            DrawMeshInstanced(meshes[t], material, instance_buf[t], instance_count[t])
```

With a typical network this produces approximately `TILE_TYPE_COUNT` draw calls total
(one per type), independent of network size.

A separate ghost material is used for the placement preview. The ghost path is a
sequence of `DrawMesh` calls (one per ghost tile) emitted each frame during edit mode;
they are not added to the instance buffer until the user confirms placement.

---

## Placement UX

The user does not select individual tiles from a catalog. Instead they pick a start
point and a target point; the system automatically assembles the best-fitting tile
sequence between them. This is inspired by **Dubins path** geometry — the shortest
route between two oriented points using only straights and constant-radius arcs.

### Path structure

Every generated path follows the form:

```
[departure curve]  →  [straight(s)]  →  [arrival curve]
```

Curves are composed of stacked catalog tiles (e.g. a 45° turn = three 15° curve
tiles). Straights are filled with the optimal combination of long and short straight
tiles. The discrete tile angle increment (15°) means the path snaps to the nearest
achievable heading; a snap indicator distinguishes exact hits from approximations.

### Interaction flow

1. **Click a start point** — either an existing open endpoint (heading inherited) or
   a free ground point (heading defaults to the camera forward).
2. **Move the cursor** — the system computes the tile sequence in real-time and
   renders it as ghost tiles.
   - If the cursor is near an existing open endpoint the arrival curve is constrained
     to match that endpoint's heading.
   - Otherwise the path arrives free-ended, pointing toward the target.
3. **`D`** — cycles the direction constraint for the entire generated sequence:
   `BOTH` → `A_TO_B` → `B_TO_A` → `BOTH`.
4. Ghost tiles turn **red** when the path is geometrically impossible (target too
   close for the minimum curve radius) with a *"too tight"* label near the cursor.
5. **Left-click** — confirms the whole sequence: all tiles placed and linked at once;
   instance buffer rebuilt.
6. **Left-click on a ground point mid-route** — drops a **waypoint anchor**. The path
   is now computed in two legs (start → waypoint, waypoint → cursor), letting the user
   force the route through a specific point or heading before confirming.
7. **Backspace** — removes the last waypoint anchor.
8. **Right-click / ESC** — cancel without placing.

### Path algorithm (per cursor move)

```
1. Compute bearing: direction from start position to cursor.

2. Departure turn:
   - Angle to rotate from start heading to bearing.
   - Round to nearest multiple of the smallest curve tile angle (15°).
   - Choose left or right turn whichever is smaller.
   - Walk the curve tiles forward to get post-curve position + heading.

3. Straight fill:
   - Remaining distance from post-curve position to target.
   - Fill greedily with long tiles, remainder with short tiles.

4. Arrival curve (only when connecting to an existing open endpoint):
   - Angle to rotate from current heading to target endpoint heading.
   - Same rounding logic as departure.
   - If total curve angle exceeds what fits in the remaining distance → red ghost.

5. Emit the tile sequence as ghost instances for rendering.
```

### Erase mode

Click any placed tile to remove it. Its endpoints are unlinked and any previously
connected tiles gain open endpoints again. Alternatively drag a marquee rectangle to
remove all tiles whose entry endpoint falls inside it.

---

## Direction Constraints

Each `PlacedTile` carries an `ArcDirection` that restricts which way trains may
traverse it.

| Value          | Meaning                        | Visual indicator  |
|----------------|--------------------------------|-------------------|
| `ARC_DIR_BOTH` | Bidirectional (default)        | None              |
| `ARC_DIR_A_TO_B` | Entry → exit only            | Arrow toward exit |
| `ARC_DIR_B_TO_A` | Exit → entry only            | Arrow toward entry|

Direction arrows are drawn above the tile only when the track editing tool is active.
They are colour-coded (e.g. `COL_ACCENT` for one-way, distinct colour for reverse).

The routing system (A*) skips a tile edge when the direction constraint forbids the
traversal direction of the approaching train.

---

## Graph for Signals and Routing

Tile connectivity directly forms the routing graph — no separate graph-build step:

- **Edges** — placed tiles (each tile = one traversable edge, junctions = two edges
  sharing the entry endpoint).
- **Nodes** — tile endpoints (`TileEndpoint`). A junction throat is a 3-way node.
- **Signals** — attached to a specific `(tile_idx, ep_idx)` pair.
- **Junction state** — a `bool active_branch` on junction tiles selects which exit
  is open; routing respects this.

Route planning is A* over the tile adjacency list, identical in structure to the
existing graph system.

---

## Save / Load Format

One tile per line under a `TILES n` header:

```
TILES 14
STRAIGHT_S  0  1.000 0.000 0.000 0.000  ...  (full 4×4 matrix, row-major)
CURVE_R2_15 1  ...
JUNCTION_L  0  ...
```

Column 2 is the `ArcDirection` integer value (0, 1, or −1).
Columns 3–18 are the 16 floats of the world transform matrix.

On load the world matrix is read directly; endpoint world positions are recomputed
by transforming the local endpoint definitions through the matrix.

---

## Implementation Order

1. Tile geometry constant table (local endpoint positions and arc lengths per type).
2. Mesh loading — one `Model` per tile type; instanced material setup.
3. Instance buffer management — rebuild on add/remove.
4. Dubins path algorithm — given start pos+heading and target pos, emit a tile
   sequence (departure curve + straights + optional arrival curve).
5. Ghost rendering — run the path algorithm each frame in edit mode, draw the result
   with a ghost material without touching the instance buffer.
6. Click-to-confirm — commit the ghost sequence, auto-link endpoints, rebuild buffer.
7. Waypoint anchors — accumulate intermediate points, chain path legs between them.
8. Direction constraint cycling (`D` key) and direction arrow rendering.
9. Junction toggle (switch active branch).
10. Save/load integration (replace current `TrackSystemSave/Load`).
11. Graph/routing integration (signals, A* traversal respecting direction).
