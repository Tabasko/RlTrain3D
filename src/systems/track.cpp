#include "track.h"
#include "../state/game_state.h"
#include "../events/event_bus.h"
#include "../types.h"
#include "../colors.h"
#include "raylib.h"
#include "raymath.h"
#include <vector>
#include <cmath>

static constexpr float SNAP_DEFAULT   = 1.0f;
static constexpr float ANCHOR_HIT_PX  = 14.0f;  // screen-pixel radius for drag detection
static constexpr int   ARC_INT_STEPS  = 128;     // arc-length integration steps per segment
static constexpr float TILE_GAP_TRIM  = 0.1f;   // close inter-tile gaps; increase until tiles touch
static constexpr float ALIGN_EPSILON  = 0.05f;  // world-space tolerance for axis-alignment guides
static constexpr float SNAP_EP_RADIUS = 1.2f;   // endpoint snap radius in world units
static constexpr float GUIDE_HALF_LEN = 80.0f;  // half-length of axis guide lines

// ---------------------------------------------------------------------------
// Asset state — one model loaded once, tiled along every committed spline
// ---------------------------------------------------------------------------
static Model    s_model;
static Material s_ghost_material; // flat COL_TRACK_GHOST material for ghost preview
static float    s_model_length;   // extent of the model along its forward axis
static Vector3  s_model_forward;  // local forward axis of the loaded model (+Z or +X)
static bool     s_model_loaded = false;

// ---------------------------------------------------------------------------
// Internal types
// ---------------------------------------------------------------------------

// A committed track line — raw Catmull-Rom anchors and per-segment instance matrices.
// instances[i] holds one transform per tiled model copy for segment (anchors[i], anchors[i+1]).
struct TrackLine {
    std::vector<Vector3>             anchors;
    std::vector<std::vector<Matrix>> instances;
};

// Live placement state — lives entirely within the system, not in gs.
struct Placement {
    std::vector<Vector3> anchors;
    int   drag_idx  = -1;
    float snap_size = SNAP_DEFAULT;
};

// ---------------------------------------------------------------------------
// Static state
// ---------------------------------------------------------------------------
static std::vector<TrackLine> s_lines;
static Placement              s_place;
static Vector3                s_cursor;
static bool                   s_has_cursor;
static bool                   s_has_guide_x;   // axis-alignment guide active on X
static float                  s_guide_x;
static bool                   s_has_guide_z;   // axis-alignment guide active on Z
static float                  s_guide_z;
static bool                   s_has_snap_ep;   // cursor snapped to a committed track endpoint
static float                  s_ghost_length;  // total arc length of the in-progress ghost curve

// ---------------------------------------------------------------------------
// Catmull-Rom sampling
//
// Phantom boundary points duplicate the first/last anchor so the curve
// passes through both endpoints with a zero-tangent extrapolation.
// ---------------------------------------------------------------------------

static Vector3 AnchorAt(const std::vector<Vector3>& pts, int i) {
    if (i < 0)                return pts[0];
    if (i >= (int)pts.size()) return pts.back();
    return pts[i];
}

// Sample position on the Catmull-Rom curve at t ∈ [0,1] for segment (seg, seg+1)
static Vector3 SampleCR(const std::vector<Vector3>& pts, int seg, float t) {
    Vector3 p0 = AnchorAt(pts, seg - 1);
    Vector3 p1 = pts[seg];
    Vector3 p2 = pts[seg + 1];
    Vector3 p3 = AnchorAt(pts, seg + 2);
    float t2 = t * t, t3 = t2 * t;
    return {
        0.5f * ((2*p1.x) + (-p0.x+p2.x)*t + (2*p0.x-5*p1.x+4*p2.x-p3.x)*t2 + (-p0.x+3*p1.x-3*p2.x+p3.x)*t3),
        0.0f,
        0.5f * ((2*p1.z) + (-p0.z+p2.z)*t + (2*p0.z-5*p1.z+4*p2.z-p3.z)*t2 + (-p0.z+3*p1.z-3*p2.z+p3.z)*t3),
    };
}

// Normalised tangent of the curve at t
static Vector3 TangentCR(const std::vector<Vector3>& pts, int seg, float t) {
    Vector3 p0 = AnchorAt(pts, seg - 1);
    Vector3 p1 = pts[seg];
    Vector3 p2 = pts[seg + 1];
    Vector3 p3 = AnchorAt(pts, seg + 2);
    float t2 = t * t;
    Vector3 d = {
        0.5f * ((-p0.x+p2.x) + (4*p0.x-10*p1.x+8*p2.x-2*p3.x)*t + (-3*p0.x+9*p1.x-9*p2.x+3*p3.x)*t2),
        0.0f,
        0.5f * ((-p0.z+p2.z) + (4*p0.z-10*p1.z+8*p2.z-2*p3.z)*t + (-3*p0.z+9*p1.z-9*p2.z+3*p3.z)*t2),
    };
    float len = Vector3Length(d);
    return (len > 0.0001f) ? Vector3Scale(d, 1.0f / len) : Vector3{1, 0, 0};
}

// ---------------------------------------------------------------------------
// Arc-length reparameterization
//
// Catmull-Rom curves aren't arc-length parameterized: equal t-steps produce
// unequal world-space distances, which gets worse on tight curves. To place
// tiles at equal world-space intervals we build a (t → cumulative arc) LUT,
// then invert it to find the t that corresponds to any desired arc distance.
// ---------------------------------------------------------------------------

struct ArcLUTEntry { float t, arc; };

// Build the arc-length LUT for segment (seg, seg+1) using ARC_INT_STEPS samples.
static std::vector<ArcLUTEntry> BuildArcLUT(const std::vector<Vector3>& anchors, int seg) {
    std::vector<ArcLUTEntry> lut;
    lut.reserve(ARC_INT_STEPS + 1);
    float    arc  = 0.0f;
    Vector3  prev = SampleCR(anchors, seg, 0.0f);
    lut.push_back({0.0f, 0.0f});
    for (int i = 1; i <= ARC_INT_STEPS; i++) {
        float   t   = (float)i / ARC_INT_STEPS;
        Vector3 cur = SampleCR(anchors, seg, t);
        arc += Vector3Length(Vector3Subtract(cur, prev));
        lut.push_back({t, arc});
        prev = cur;
    }
    return lut;
}

// Given a desired arc distance d, return the t that corresponds to it via
// binary search + linear interpolation in the LUT.
static float ArcToT(const std::vector<ArcLUTEntry>& lut, float d) {
    if (d <= 0.0f)             return 0.0f;
    if (d >= lut.back().arc)   return 1.0f;

    int lo = 0, hi = (int)lut.size() - 1;
    while (hi - lo > 1) {
        int mid = (lo + hi) / 2;
        if (lut[mid].arc < d) lo = mid; else hi = mid;
    }
    float frac = (d - lut[lo].arc) / (lut[hi].arc - lut[lo].arc);
    return lut[lo].t + frac * (lut[hi].t - lut[lo].t);
}

// ---------------------------------------------------------------------------
// Instance matrix
// ---------------------------------------------------------------------------

// Build a TRS matrix that places the loaded model at `pos` oriented along `tangent`.
// The model's local forward axis is rotated to match the spline tangent.
static Matrix InstanceMatrix(Vector3 pos, Vector3 tangent) {
    Quaternion rot = QuaternionFromVector3ToVector3(s_model_forward, tangent);
    Matrix matR    = QuaternionToMatrix(rot);
    Matrix matT    = MatrixTranslate(pos.x, pos.y, pos.z);
    return MatrixMultiply(matR, matT); // rotate then translate into world space
}

// ---------------------------------------------------------------------------
// Segment instance building
// ---------------------------------------------------------------------------

// Compute transform matrices for all tiled model instances along one inter-anchor segment.
// Tiles are placed at equal world-space intervals using arc-length reparameterization,
// so spacing stays consistent on both straight sections and curves.
static std::vector<Matrix> BuildSegmentInstances(const std::vector<Vector3>& anchors, int seg) {
    std::vector<Matrix> out;
    if (!s_model_loaded) return out;

    auto  lut      = BuildArcLUT(anchors, seg);
    float total    = lut.back().arc;
    float tile_len = s_model_length - TILE_GAP_TRIM;
    int   n        = (int)fmaxf(1.0f, roundf(total / tile_len));
    float spacing  = total / (float)n; // actual world-space interval between tile centres

    out.reserve(n);
    for (int i = 0; i < n; i++) {
        float   d  = (i + 0.5f) * spacing; // arc distance to this tile's centre
        float   t  = ArcToT(lut, d);
        Vector3 p  = SampleCR(anchors, seg, t);
        Vector3 tn = TangentCR(anchors, seg, t);
        out.push_back(InstanceMatrix(p, tn));
    }
    return out;
}

// ---------------------------------------------------------------------------
// Placement helpers
// ---------------------------------------------------------------------------

// Project mouse ray onto the Y=0 ground plane, snap to grid, write to *out.
// Returns false when the ray is parallel to the plane or behind the camera.
static bool GroundPos(Vector3 *out, float snap) {
    Ray ray = GetMouseRay(GetMousePosition(), gs.camera.cam);
    if (fabsf(ray.direction.y) < 0.001f) return false;
    float t = -ray.position.y / ray.direction.y;
    if (t < 0.0f) return false;
    Vector3 p = Vector3Add(ray.position, Vector3Scale(ray.direction, t));
    out->x = roundf(p.x / snap) * snap;
    out->y = 0.0f;
    out->z = roundf(p.z / snap) * snap;
    return true;
}

// Returns the index of the first anchor within ANCHOR_HIT_PX of the mouse, or -1.
static int HitAnchor(const std::vector<Vector3>& anchors) {
    Vector2 mouse = GetMousePosition();
    for (int i = 0; i < (int)anchors.size(); i++) {
        Vector2 s = GetWorldToScreen(anchors[i], gs.camera.cam);
        if (Vector2Distance(s, mouse) < ANCHOR_HIT_PX) return i;
    }
    return -1;
}

// Commit in-progress anchors as a new TrackLine. Requires at least 2 anchors.
static void CommitPlacement() {
    if ((int)s_place.anchors.size() < 2) return;

    TrackLine line;
    line.anchors = s_place.anchors;
    int n_segs   = (int)s_place.anchors.size() - 1;
    line.instances.resize(n_segs);
    for (int i = 0; i < n_segs; i++)
        line.instances[i] = BuildSegmentInstances(s_place.anchors, i);

    s_lines.push_back(std::move(line));
}

// ---------------------------------------------------------------------------
// Ghost drawing (must be called inside BeginMode3D)
// ---------------------------------------------------------------------------

// Draw the full ghost preview through all placed anchors + cursor using the actual
// track mesh tiled along the spline, tinted with the ghost material.
static void DrawGhostCurve(const std::vector<Vector3>& anchors, Vector3 cursor) {
    if (!s_model_loaded) return;

    std::vector<Vector3> pts = anchors;
    pts.push_back(cursor);

    int n = (int)pts.size();
    s_ghost_length = 0.0f;
    for (int seg = 0; seg < n - 1; seg++) {
        // Accumulate arc length before building instances (BuildSegmentInstances
        // rebuilds the LUT internally — minor redundancy accepted for ghost-only path).
        auto lut = BuildArcLUT(pts, seg);
        s_ghost_length += lut.back().arc;

        for (const Matrix& m : BuildSegmentInstances(pts, seg))
            for (int mi = 0; mi < s_model.meshCount; mi++)
                DrawMesh(s_model.meshes[mi], s_ghost_material, m);
    }

    for (const Vector3& a : anchors)
        DrawSphere(a, 0.25f, COL_ACCENT);

    DrawSphere(cursor, 0.3f, YELLOW);
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

void TrackSystemInit() {
    s_model = LoadModel("resources/kenny/kenney_train-kit/Models/GLB format/track-single.glb");

    if (s_model.meshCount == 0) {
        TraceLog(LOG_WARNING, "TRACK: failed to load spline-segment.glb");
        return;
    }
    s_model_loaded = true;

    // Replace every material with a flat-colour default so palette colours render
    // without being modulated by the GLB's embedded textures.
    // Material 0 is assumed to be the ballast/base; anything else is the rail.
    for (int i = 0; i < s_model.materialCount; i++) {
        Material mat = LoadMaterialDefault();
        mat.maps[MATERIAL_MAP_DIFFUSE].color = (i == 0) ? COL_TRACK: COL_TRACK_BALLAST;
        s_model.materials[i] = mat;
    }

    s_ghost_material = LoadMaterialDefault();
    s_ghost_material.maps[MATERIAL_MAP_DIFFUSE].color = COL_TRACK_GHOST;

    // Determine the model's forward axis and tile length from its bounding box.
    // The longest horizontal extent (X or Z) is treated as the track direction.
    BoundingBox bb = GetModelBoundingBox(s_model);
    Vector3     sz = Vector3Subtract(bb.max, bb.min);
    if (sz.z >= sz.x) {
        s_model_forward = {0, 0, 1};
        s_model_length  = sz.z;
    } else {
        s_model_forward = {1, 0, 0};
        s_model_length  = sz.x;
    }

    TraceLog(LOG_INFO, "TRACK: model loaded — forward %s, length %.3f",
             (s_model_forward.z > 0.5f ? "+Z" : "+X"), s_model_length);
}

void TrackSystemUpdate() {
    // Activate placement when the Track button event arrives from the previous frame
    if (gs.events.has(EVENT_START_TRACK_EDIT)) {
        gs.app.track_editing = true;
        s_place.anchors.clear();
        s_place.drag_idx = -1;
    }

    if (!gs.app.track_editing) return;

    s_has_cursor = GroundPos(&s_cursor, s_place.snap_size);
    if (!s_has_cursor) return;

    // Anchor snap: if cursor is near any anchor on any committed track, lock to it.
    s_has_snap_ep = false;
    for (TrackLine& line : s_lines) {
        for (Vector3 a : line.anchors) {
            if (Vector3Distance(s_cursor, a) < SNAP_EP_RADIUS) {
                s_cursor      = a;
                s_has_snap_ep = true;
                break;
            }
        }
        if (s_has_snap_ep) break;
    }

    // Axis-alignment guides: flag X/Z when cursor shares a coordinate with any anchor.
    s_has_guide_x = false;
    s_has_guide_z = false;
    auto check_align = [&](Vector3 a) {
        if (fabsf(a.x - s_cursor.x) < ALIGN_EPSILON) { s_has_guide_x = true; s_guide_x = s_cursor.x; }
        if (fabsf(a.z - s_cursor.z) < ALIGN_EPSILON) { s_has_guide_z = true; s_guide_z = s_cursor.z; }
    };
    for (TrackLine& line : s_lines)
        for (Vector3 a : line.anchors) check_align(a);
    for (Vector3 a : s_place.anchors) check_align(a);

    if (IsMouseButtonPressed(MOUSE_LEFT_BUTTON)) {
        int hit = HitAnchor(s_place.anchors);
        if (hit >= 0)
            s_place.drag_idx = hit;
        else
            s_place.anchors.push_back(s_cursor);
    }

    if (s_place.drag_idx >= 0 && IsMouseButtonDown(MOUSE_LEFT_BUTTON))
        s_place.anchors[s_place.drag_idx] = s_cursor;

    if (IsMouseButtonReleased(MOUSE_LEFT_BUTTON))
        s_place.drag_idx = -1;

    // Right-click: commit and exit placement mode
    if (IsMouseButtonPressed(MOUSE_RIGHT_BUTTON)) {
        CommitPlacement();
        s_place.anchors.clear();
        s_place.drag_idx    = -1;
        gs.app.track_editing = false;
    }

    if (IsKeyPressed(KEY_BACKSPACE) && !s_place.anchors.empty())
        s_place.anchors.pop_back();

    // ESC: cancel without committing; UiUpdate skips its ESC handler while track_editing is true
    if (IsKeyPressed(KEY_ESCAPE)) {
        s_place.anchors.clear();
        s_place.drag_idx    = -1;
        gs.app.track_editing = false;
    }
}

void TrackSystemDraw3D() {
    if (s_model_loaded) {
        for (TrackLine& line : s_lines) {
            for (std::vector<Matrix>& seg_inst : line.instances) {
                for (const Matrix& m : seg_inst) {
                    for (int mi = 0; mi < s_model.meshCount; mi++)
                        DrawMesh(s_model.meshes[mi],
                                 s_model.materials[s_model.meshMaterial[mi]],
                                 m);
                }
            }
        }
    }

    if (gs.app.track_editing) {
        // Draw all committed anchors so the user can see snap targets.
        for (TrackLine& line : s_lines)
            for (Vector3 a : line.anchors)
                DrawSphere(a, 0.2f, COL_SNAP_ENDPOINT);
    }

    if (gs.app.track_editing && s_has_cursor) {
        // Axis-alignment guide lines — raised 1 cm to avoid Z-fighting with the ground.
        if (s_has_guide_x)
            DrawLine3D({s_guide_x, 0.01f, -GUIDE_HALF_LEN},
                       {s_guide_x, 0.01f,  GUIDE_HALF_LEN}, COL_SNAP_DOT);
        if (s_has_guide_z)
            DrawLine3D({-GUIDE_HALF_LEN, 0.01f, s_guide_z},
                       { GUIDE_HALF_LEN, 0.01f, s_guide_z}, COL_SNAP_DOT);

        // Endpoint snap ring drawn around the locked position.
        if (s_has_snap_ep)
            DrawCircle3D(s_cursor, 0.5f, {1, 0, 0}, 90.0f, COL_SNAP_ENDPOINT);

        DrawGhostCurve(s_place.anchors, s_cursor);
    }
}

void TrackSystemDraw2D() {
    if (!gs.app.track_editing || !s_has_cursor || s_place.anchors.empty()) return;

    // Project cursor to screen and draw total ghost arc length next to it.
    Vector2 screen = GetWorldToScreen(s_cursor, gs.camera.cam);
    DrawText(TextFormat("%.1f m", s_ghost_length),
             (int)screen.x + 14, (int)screen.y - 10, 24, COL_ACCENT);
}

void TrackSystemDestroy() {
    if (s_model_loaded) UnloadModel(s_model);
    s_lines.clear();
}

const Model* TrackGetModel()        { return s_model_loaded ? &s_model : nullptr; }
Vector3      TrackGetModelForward() { return s_model_forward; }
float        TrackGetModelLength()  { return s_model_length; }
