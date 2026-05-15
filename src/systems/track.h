#pragma once
#include "raylib.h"
#include <stdio.h>
#include <vector>

// A sampled point on a committed arc — returned by TrackFindNearestArc.
// Identifies the arc, the Catmull-Rom segment, the parameter within it, and
// the corresponding world-space position and tangent.
struct ArcPoint {
    int     arc_id;
    int     seg;     // segment index within the arc
    float   t;       // parameter ∈ (0, 1) within that segment
    Vector3 pos;
    Vector3 tangent; // normalised forward direction
};

// Initialise GPU materials. Call once after InitWindow.
void TrackSystemInit();

// Process placement input. Call once per frame before Draw.
void TrackSystemUpdate();

// Draw committed tracks and the in-progress ghost. Must be called inside BeginMode3D.
void TrackSystemDraw3D();

// Draw 2D placement overlays (arc-length label). Must be called outside BeginMode3D.
void TrackSystemDraw2D();

// Release all GPU resources.
void TrackSystemDestroy();

// Serialize all committed track lines (anchors only) to an open file.
void TrackSystemSave(FILE *f);

// Replace all committed track lines from an open file; rebuilds instance matrices.
void TrackSystemLoad(FILE *f);

