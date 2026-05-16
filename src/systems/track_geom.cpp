#include "track_geom.h"
#include "raymath.h"
#include <cmath>

TileGeom s_geom[TILE_TYPE_COUNT];

bool IsLeftCurve(TileType t) {
    return t == TILE_CURVE_R1_15_L || t == TILE_CURVE_R2_15_L || t == TILE_CURVE_R2_30_L;
}

float NormAngle(float a) {
    while (a >  (float)M_PI) a -= 2.0f * (float)M_PI;
    while (a <= -(float)M_PI) a += 2.0f * (float)M_PI;
    return a;
}

Vector3 RotateByHeading(Vector3 local, float heading) {
    float s = sinf(heading), c = cosf(heading);
    return { local.x * c + local.z * s, 0.0f, -local.x * s + local.z * c };
}

// Ry(heading) * Scale(TILE_MESH_SCALE) then translate.
// Scaling the rotation columns keeps the translation in world units.
Matrix TileMatrix(Vector3 pos, float heading) {
    float s = sinf(heading) * TILE_MESH_SCALE;
    float c = cosf(heading) * TILE_MESH_SCALE;
    Matrix m = {
         c, 0,  s, pos.x,
         0, TILE_MESH_SCALE, 0, pos.y,
        -s, 0,  c, pos.z,
        0,  0,  0, 1
    };
    return m;
}

Matrix MirrorX(Matrix m) {
    m.m0 = -m.m0;
    m.m1 = -m.m1;
    m.m2 = -m.m2;
    return m;
}

void WalkTile(TileType type, Vector3 pos, float heading,
              Vector3 *next_pos, float *next_heading) {
    const TileGeom& g = s_geom[type];
    *next_pos     = Vector3Add(pos, RotateByHeading(g.exit_pos, heading));
    *next_heading = NormAngle(heading + g.exit_heading);
}

void TrackGeomInit() {
    // Straight lengths in world units. Curves: R1=20 m, R2=40 m.
    // exit_pos for a right curve: (R·sinθ, 0, R·(1−cosθ)), exit_heading = θ.
    // Left variants negate x and heading; arc length = R·θ for both.
    float s2 = 2.0f * DEG15; // 30°

    s_geom[TILE_STRAIGHT_S]   = {{ 0, 0, -8.0f*TILE_MESH_SCALE }, 0.0f, 8.0f*TILE_MESH_SCALE };
    s_geom[TILE_STRAIGHT_L]   = {{ 0, 0, -8.0f*TILE_MESH_SCALE }, 0.0f, 8.0f*TILE_MESH_SCALE };
    s_geom[TILE_STRAIGHT_XL]   = {{ 0, 0, -64.0f*TILE_MESH_SCALE }, 0.0f, 64.0f*TILE_MESH_SCALE };

    s_geom[TILE_CURVE_R1_15]   = {{ 20.0f * sinf(DEG15),  0, 20.0f * (1.0f - cosf(DEG15)) },  DEG15, 20.0f * DEG15 };
    s_geom[TILE_CURVE_R1_15_L] = {{ -20.0f * sinf(DEG15), 0, 20.0f * (1.0f - cosf(DEG15)) }, -DEG15, 20.0f * DEG15 };
    s_geom[TILE_CURVE_R2_15]   = {{ 40.0f * sinf(DEG15),  0, 40.0f * (1.0f - cosf(DEG15)) },  DEG15, 40.0f * DEG15 };
    s_geom[TILE_CURVE_R2_15_L] = {{ -40.0f * sinf(DEG15), 0, 40.0f * (1.0f - cosf(DEG15)) }, -DEG15, 40.0f * DEG15 };
    s_geom[TILE_CURVE_R2_30]   = {{ 80.0f * sinf(s2),     0, 80.0f * (1.0f - cosf(s2)) },      s2,   80.0f * s2    };
    s_geom[TILE_CURVE_R2_30_L] = {{ -80.0f * sinf(s2),    0, 80.0f * (1.0f - cosf(s2)) },     -s2,   80.0f * s2    };
}
