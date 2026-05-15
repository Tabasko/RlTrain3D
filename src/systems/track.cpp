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
static constexpr float MAX_TURN_COS      = 0.7071f; // cos(45°) — minimum dot product of consecutive segment directions
static constexpr float ARC_SUBDIVIDE_COS = 0.9659f; // cos(15°) — insert arc midpoint anchor when turn exceeds this

// ---------------------------------------------------------------------------
// Asset state — one model loaded once, tiled along every committed spline
// ---------------------------------------------------------------------------
static Model    s_model;
static Material s_ghost_material;     // flat COL_TRACK_GHOST material for ghost preview
static Material s_ghost_material_bad; // red COL_TRACK_GHOST_BAD material for invalid curve preview
static float    s_model_length;       // extent of the model along its forward axis
static Vector3  s_model_forward;      // local forward axis of the loaded model (+Z or +X)
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
static bool                   s_has_snap_ep;      // cursor snapped to a committed track endpoint
static float                  s_ghost_length;     // total arc length of the in-progress ghost curve
static bool                   s_curve_too_sharp;  // proposed new anchor would exceed MAX_TURN_COS (45°)
static int                    s_edit_line_idx   = -1; // committed line whose anchor is being dragged
static int                    s_edit_anchor_idx = -1; // index within that line's anchor array
static bool                   s_erase_dragging;       // marquee drag in progress
static Vector2                s_erase_start;          // screen position where the drag began
static Vector2                s_erase_end;            // current drag end (updated each frame)

// ---------------------------------------------------------------------------
// Arc midpoint helper
// ---------------------------------------------------------------------------

// Given anchor b with incoming direction from a, return the geometric midpoint of the
// circular arc from b to c that is tangent to (b-a) at b.
// When the three points are nearly collinear the linear midpoint is returned instead.
static Vector3 ArcMidpoint(Vector3 a, Vector3 b, Vector3 c) {
    Vector3 v1 = Vector3Normalize(Vector3Subtract(b, a));
    // Left-hand perpendicular to v1 in the XZ plane.
    Vector3 n  = { -v1.z, 0.0f, v1.x };
    Vector3 d  = Vector3Subtract(c, b);
    float   nd = n.x * d.x + n.z * d.z; // signed lateral offset of c from B along n

    // Collinear / near-straight: the midpoint is just the linear midpoint.
    if (fabsf(nd) < 0.0001f)
        return { (b.x + c.x) * 0.5f, 0.0f, (b.z + c.z) * 0.5f };

    // Signed radius of the circle tangent to v1 at b that passes through c.
    float   R  = (d.x*d.x + d.z*d.z) / (2.0f * nd);
    Vector3 O  = { b.x + R * n.x, 0.0f, b.z + R * n.z }; // circle centre
    float   r  = fabsf(R);

    // Angles of b and c viewed from the centre, then average for the arc midpoint.
    float ab   = atan2f(b.z - O.z, b.x - O.x);
    float ac   = atan2f(c.z - O.z, c.x - O.x);
    float diff = ac - ab;
    // Normalise to (−π, π] so we always take the shorter arc.
    while (diff >  (float)M_PI) diff -= 2.0f * (float)M_PI;
    while (diff < -(float)M_PI) diff += 2.0f * (float)M_PI;
    float am   = ab + diff * 0.5f;

    return { O.x + r * cosf(am), 0.0f, O.z + r * sinf(am) };
}

// ---------------------------------------------------------------------------
// Centripetal Catmull-Rom sampling
//
// Phantom boundary points duplicate the first/last anchor so the curve
// passes through both endpoints with a zero-tangent extrapolation.
//
// Centripetal parameterization spaces knots by sqrt(chord_length) rather than
// uniformly. This prevents the outward bulge that standard CR produces on curves:
// the curve energy is proportional to distance, so the spline always follows the
// inside of a turn rather than briefly overshooting outward.
// ---------------------------------------------------------------------------

static Vector3 AnchorAt(const std::vector<Vector3>& pts, int i) {
    if (i < 0)                return pts[0];
    if (i >= (int)pts.size()) return pts.back();
    return pts[i];
}

// Centripetal knot spacing: sqrt of the XZ chord length.
static float KnotDist(Vector3 a, Vector3 b) {
    return sqrtf(Vector3Distance(a, b));
}

// Linearly interpolate a→b over knot interval [ta, tb] at tc.
// Returns a when the interval is degenerate (duplicate boundary anchors).
static Vector3 KnotLerp(Vector3 a, Vector3 b, float ta, float tb, float tc) {
    if (fabsf(tb - ta) < 0.0001f) return a;
    float f = (tc - ta) / (tb - ta);
    return { a.x + f*(b.x - a.x), 0.0f, a.z + f*(b.z - a.z) };
}

// Sample position at t ∈ [0,1] for segment (seg, seg+1) via the Barry-Goldman algorithm.
static Vector3 SampleCR(const std::vector<Vector3>& pts, int seg, float t) {
    Vector3 p0 = AnchorAt(pts, seg - 1);
    Vector3 p1 = pts[seg];
    Vector3 p2 = pts[seg + 1];
    Vector3 p3 = AnchorAt(pts, seg + 2);

    float k0 = 0.0f;
    float k1 = k0 + KnotDist(p0, p1);
    float k2 = k1 + KnotDist(p1, p2);
    float k3 = k2 + KnotDist(p2, p3);
    float tc = k1 + t * (k2 - k1); // remap t∈[0,1] → knot interval [k1, k2]

    Vector3 A1 = KnotLerp(p0, p1, k0, k1, tc);
    Vector3 A2 = KnotLerp(p1, p2, k1, k2, tc);
    Vector3 A3 = KnotLerp(p2, p3, k2, k3, tc);
    Vector3 B1 = KnotLerp(A1, A2, k0, k2, tc);
    Vector3 B2 = KnotLerp(A2, A3, k1, k3, tc);
    return KnotLerp(B1, B2, k1, k2, tc);
}

// Normalised tangent at t via central finite difference — sufficient for tile orientation.
static Vector3 TangentCR(const std::vector<Vector3>& pts, int seg, float t) {
    constexpr float eps = 0.001f;
    Vector3 a = SampleCR(pts, seg, fmaxf(0.0f, t - eps));
    Vector3 b = SampleCR(pts, seg, fminf(1.0f, t + eps));
    Vector3 d = Vector3Subtract(b, a);
    float   len = Vector3Length(d);
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

// Finds the first committed anchor within ANCHOR_HIT_PX of the mouse.
// Writes the line and anchor indices to *line_out/*anchor_out; returns false if none found.
static bool HitCommittedAnchor(int *line_out, int *anchor_out) {
    Vector2 mouse = GetMousePosition();
    for (int li = 0; li < (int)s_lines.size(); li++) {
        for (int ai = 0; ai < (int)s_lines[li].anchors.size(); ai++) {
            Vector2 s = GetWorldToScreen(s_lines[li].anchors[ai], gs.camera.cam);
            if (Vector2Distance(s, mouse) < ANCHOR_HIT_PX) {
                *line_out   = li;
                *anchor_out = ai;
                return true;
            }
        }
    }
    return false;
}

// Rebuilds all segment instance matrices for a line after one of its anchors was moved.
static void RebuildLineInstances(TrackLine& line) {
    int n_segs = (int)line.anchors.size() - 1;
    line.instances.resize(n_segs);
    for (int i = 0; i < n_segs; i++)
        line.instances[i] = BuildSegmentInstances(line.anchors, i);
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

    // Mirror the auto-arc-midpoint logic so the ghost shows the intermediate anchor
    // in real-time as the cursor moves, matching what will be committed on click.
    if (pts.size() >= 2 && !s_curve_too_sharp) {
        Vector3 prev = pts[pts.size() - 2];
        Vector3 last = pts.back();
        Vector3 v1   = Vector3Normalize(Vector3Subtract(last, prev));
        Vector3 v2   = Vector3Normalize(Vector3Subtract(cursor, last));
        if (Vector3DotProduct(v1, v2) < ARC_SUBDIVIDE_COS)
            pts.push_back(ArcMidpoint(prev, last, cursor));
    }

    pts.push_back(cursor);

    int n = (int)pts.size();
    s_ghost_length = 0.0f;
    for (int seg = 0; seg < n - 1; seg++) {
        // Accumulate arc length before building instances (BuildSegmentInstances
        // rebuilds the LUT internally — minor redundancy accepted for ghost-only path).
        auto lut = BuildArcLUT(pts, seg);
        s_ghost_length += lut.back().arc;

        // Use the bad (red) material for the last segment when the curve is too sharp.
        Material& mat = (s_curve_too_sharp && seg == n - 2) ? s_ghost_material_bad : s_ghost_material;
        for (const Matrix& m : BuildSegmentInstances(pts, seg))
            for (int mi = 0; mi < s_model.meshCount; mi++)
                DrawMesh(s_model.meshes[mi], mat, m);
    }

    for (const Vector3& a : anchors)
        DrawSphere(a, 0.25f, COL_ACCENT);

    // Draw auto-inserted arc anchors (pts entries beyond the user-placed anchors, excluding cursor).
    for (int i = (int)anchors.size(); i < (int)pts.size() - 1; i++)
        DrawSphere(pts[i], 0.2f, COL_JUNCTION);

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

    s_ghost_material_bad = LoadMaterialDefault();
    s_ghost_material_bad.maps[MATERIAL_MAP_DIFFUSE].color = COL_TRACK_GHOST_BAD;

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
        gs.app.track_editing  = true;
        gs.app.erase_editing  = false;
        s_place.anchors.clear();
        s_place.drag_idx      = -1;
        s_edit_line_idx       = -1;
        s_edit_anchor_idx     = -1;
    }

    if (gs.events.has(EVENT_START_ERASE_EDIT)) {
        gs.app.erase_editing  = true;
        gs.app.track_editing  = false;
        s_erase_dragging      = false;
    }

    if (!gs.app.track_editing && !gs.app.erase_editing) return;

    // ---------------------------------------------------------------------------
    // Erase mode — marquee selection and deletion
    // ---------------------------------------------------------------------------
    if (gs.app.erase_editing) {
        if (IsMouseButtonPressed(MOUSE_LEFT_BUTTON)) {
            s_erase_start    = GetMousePosition();
            s_erase_end      = s_erase_start;
            s_erase_dragging = true;
        }
        if (s_erase_dragging && IsMouseButtonDown(MOUSE_LEFT_BUTTON))
            s_erase_end = GetMousePosition();

        if (s_erase_dragging && IsMouseButtonReleased(MOUSE_LEFT_BUTTON)) {
            s_erase_dragging = false;
            // Build a normalised screen-space rectangle from the drag corners.
            Rectangle marquee = {
                fminf(s_erase_start.x, s_erase_end.x),
                fminf(s_erase_start.y, s_erase_end.y),
                fabsf(s_erase_end.x - s_erase_start.x),
                fabsf(s_erase_end.y - s_erase_start.y),
            };
            // Remove every anchor that projects inside the marquee; erase lines
            // that fall below two anchors as they can no longer form a segment.
            for (int li = (int)s_lines.size() - 1; li >= 0; li--) {
                auto& anchors = s_lines[li].anchors;
                for (int ai = (int)anchors.size() - 1; ai >= 0; ai--) {
                    Vector2 sp = GetWorldToScreen(anchors[ai], gs.camera.cam);
                    if (CheckCollisionPointRec(sp, marquee))
                        anchors.erase(anchors.begin() + ai);
                }
                if ((int)anchors.size() < 2)
                    s_lines.erase(s_lines.begin() + li);
                else
                    RebuildLineInstances(s_lines[li]);
            }
        }

        if (IsMouseButtonPressed(MOUSE_RIGHT_BUTTON) || IsKeyPressed(KEY_ESCAPE)) {
            s_erase_dragging     = false;
            gs.app.erase_editing = false;
        }
        return; // erase mode handled; skip track-placement logic below
    }

    s_has_cursor = GroundPos(&s_cursor, s_place.snap_size);
    if (!s_has_cursor) return;

    // Turn-angle check: block placements that deflect more than 45° at the last anchor.
    // Uses the dot product of the incoming and outgoing direction vectors; independent of spacing.
    s_curve_too_sharp = false;
    if (s_place.anchors.size() >= 2) {
        Vector3 prev = s_place.anchors[s_place.anchors.size() - 2];
        Vector3 last = s_place.anchors.back();
        Vector3 v1 = Vector3Normalize(Vector3Subtract(last, prev));
        Vector3 v2 = Vector3Normalize(Vector3Subtract(s_cursor, last));
        s_curve_too_sharp = (Vector3DotProduct(v1, v2) < MAX_TURN_COS);
    }

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
        if (hit >= 0) {
            s_place.drag_idx = hit;
        } else {
            int li, ai;
            if (HitCommittedAnchor(&li, &ai)) {
                s_edit_line_idx   = li;
                s_edit_anchor_idx = ai;
            } else if (!s_curve_too_sharp) {
                // When the turn at the last anchor exceeds 15°, insert the geometric
                // midpoint of the circular arc as an intermediate anchor so the spline
                // has enough shape to follow the inside of the curve.
                if (s_place.anchors.size() >= 2) {
                    Vector3 prev = s_place.anchors[s_place.anchors.size() - 2];
                    Vector3 last = s_place.anchors.back();
                    Vector3 v1   = Vector3Normalize(Vector3Subtract(last, prev));
                    Vector3 v2   = Vector3Normalize(Vector3Subtract(s_cursor, last));
                    if (Vector3DotProduct(v1, v2) < ARC_SUBDIVIDE_COS)
                        s_place.anchors.push_back(ArcMidpoint(prev, last, s_cursor));
                }
                s_place.anchors.push_back(s_cursor);
            }
        }
    }

    if (s_place.drag_idx >= 0 && IsMouseButtonDown(MOUSE_LEFT_BUTTON))
        s_place.anchors[s_place.drag_idx] = s_cursor;

    if (s_edit_line_idx >= 0 && IsMouseButtonDown(MOUSE_LEFT_BUTTON)) {
        s_lines[s_edit_line_idx].anchors[s_edit_anchor_idx] = s_cursor;
        RebuildLineInstances(s_lines[s_edit_line_idx]);
    }

    if (IsMouseButtonReleased(MOUSE_LEFT_BUTTON)) {
        s_place.drag_idx  = -1;
        s_edit_line_idx   = -1;
        s_edit_anchor_idx = -1;
    }

    if (IsMouseButtonPressed(MOUSE_RIGHT_BUTTON)) {
        int li, ai;
        if (HitCommittedAnchor(&li, &ai)) {
            // Right-click on a committed anchor: remove it.
            // If the line drops below 2 anchors it can no longer form a segment, so remove it entirely.
            TrackLine& line = s_lines[li];
            line.anchors.erase(line.anchors.begin() + ai);
            if ((int)line.anchors.size() < 2) {
                s_lines.erase(s_lines.begin() + li);
            } else {
                RebuildLineInstances(line);
            }
            // Clear edit selection in case the deleted anchor was selected.
            s_edit_line_idx   = -1;
            s_edit_anchor_idx = -1;
        } else {
            // Right-click on empty space: commit in-progress anchors and exit placement mode.
            CommitPlacement();
            s_place.anchors.clear();
            s_place.drag_idx      = -1;
            s_edit_line_idx       = -1;
            s_edit_anchor_idx     = -1;
            gs.app.track_editing  = false;
        }
    }

    if (IsKeyPressed(KEY_BACKSPACE) && !s_place.anchors.empty())
        s_place.anchors.pop_back();

    // ESC: cancel without committing; UiUpdate skips its ESC handler while track_editing is true
    if (IsKeyPressed(KEY_ESCAPE)) {
        s_place.anchors.clear();
        s_place.drag_idx      = -1;
        s_edit_line_idx       = -1;
        s_edit_anchor_idx     = -1;
        gs.app.track_editing  = false;
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

    if (gs.app.track_editing || gs.app.erase_editing) {
        // Draw all committed anchors; in track mode highlight the one being dragged.
        for (int li = 0; li < (int)s_lines.size(); li++) {
            for (int ai = 0; ai < (int)s_lines[li].anchors.size(); ai++) {
                bool selected = (li == s_edit_line_idx && ai == s_edit_anchor_idx);
                DrawSphere(s_lines[li].anchors[ai],
                           selected ? 0.35f : 0.2f,
                           selected ? COL_ACCENT : COL_SNAP_ENDPOINT);
            }
        }
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
    if (gs.app.erase_editing && s_erase_dragging) {
        // Draw the live marquee rectangle: translucent fill + solid border.
        Rectangle marquee = {
            fminf(s_erase_start.x, s_erase_end.x),
            fminf(s_erase_start.y, s_erase_end.y),
            fabsf(s_erase_end.x - s_erase_start.x),
            fabsf(s_erase_end.y - s_erase_start.y),
        };
        DrawRectangleRec(marquee, Color{220, 60, 60, 40});
        DrawRectangleLinesEx(marquee, 1.5f, Color{220, 60, 60, 200});
    }

    if (!gs.app.track_editing || !s_has_cursor || s_place.anchors.empty()) return;

    // Project cursor to screen and draw total ghost arc length (or a curve warning) next to it.
    Vector2 screen = GetWorldToScreen(s_cursor, gs.camera.cam);
    if (s_curve_too_sharp)
        DrawText("Too sharp!", (int)screen.x + 14, (int)screen.y - 10, 24, RED);
    else
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
