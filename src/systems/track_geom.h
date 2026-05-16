#pragma once
#include "../types.h"
#include "raylib.h"

// ---------------------------------------------------------------------------
// Shared placement constants used across all track sub-modules.
// ---------------------------------------------------------------------------
static constexpr float DEG15           = 0.261799f;    // π/12 (15°)
static constexpr float DEG45           = 3.0f * DEG15; // π/4  (45°)
static constexpr float TILE_MESH_SCALE = 0.25f;        // uniform scale applied to loaded meshes
static constexpr int   TILE_NO_LINK    = -1;
static constexpr float SNAP_EP_R       = 0.5f;         // world-space endpoint snap radius

// Per-type geometry: local-space exit endpoint (entry is always origin, heading 0).
// Right-curve: exit.pos = (R·sinθ, 0, R·(1−cosθ)), exit.heading = θ.
// Left variants: pos.x negated, heading negated.
struct TileGeom {
    Vector3 exit_pos;
    float   exit_heading; // radians; positive = right turn
    float   length;       // arc or chord length, used for UI display
};

// Geometry table — filled once by TrackGeomInit().
extern TileGeom s_geom[TILE_TYPE_COUNT];

// Returns true when t is a left-curve mirror type.
bool    IsLeftCurve(TileType t);

// Normalize angle to (−π, π].
float   NormAngle(float a);

// Rotate a local XZ vector by a world heading (heading=0 → +Z, positive = CW from above).
Vector3 RotateByHeading(Vector3 local, float heading);

// Build a scaled world transform: translate to pos, rotate by heading around Y.
// Left-curve X-mirror is applied separately via MirrorX after calling this.
Matrix  TileMatrix(Vector3 pos, float heading);

// Negate the X column of m — post-multiplies by diag(−1,1,1,1) for left-curve mirroring.
Matrix  MirrorX(Matrix m);

// Compute the exit position and heading of a tile placed at (pos, heading).
void    WalkTile(TileType type, Vector3 pos, float heading,
                 Vector3 *next_pos, float *next_heading);

// Populate s_geom with per-type geometry data. Must be called before any WalkTile use.
void    TrackGeomInit();
