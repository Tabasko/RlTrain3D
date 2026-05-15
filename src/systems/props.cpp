#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "raylib.h"
#include "raymath.h"
#include "props.h"
#include "environment.h" // MAP_SIZE
#include "../colors.h"

#define MAX_PROPS         64
#define WIND_TURBINE_COUNT 12
#define WIND_TURBINE_SCALE 1.0f
// Y offset to position the rotor hub at the top of the mast
#define WIND_TURBINE_HUB_Y (6.55f * WIND_TURBINE_SCALE)

// Keep props away from the map centre where the track starts
#define SCATTER_MARGIN     20.0f

static PropInstance props[MAX_PROPS];
static int          prop_count = 0;

// Shared models — one copy in GPU memory, drawn at each instance's transform
static Model    s_mast;
static Model    s_rotor;
static Color    s_tint;
static Texture  s_texture;

// ---------------------------------------------------------------------------
// Internal helpers
// ---------------------------------------------------------------------------

static float rand_range(float lo, float hi) {
    return lo + ((float)rand() / (float)RAND_MAX) * (hi - lo);
}

static void place_wind_turbines(int count) {
    float half = (float)MAP_SIZE * 0.5f - SCATTER_MARGIN;
    for (int i = 0; i < count && prop_count < MAX_PROPS; i++) {
        PropInstance *p = &props[prop_count++];
        p->type           = PROP_WIND_TURBINE;
        p->position       = { rand_range(-half, half), 0.0f, rand_range(-half, half) };
        p->rotation       = rand_range(0.0f, 2.0f * PI);
        p->rotation_speed = rand_range(0.1f, 0.4f); // radians per second
    }
}

// ---------------------------------------------------------------------------
// Public interface
// ---------------------------------------------------------------------------

void PropsInit(void) {
    s_mast  = LoadModel("resources/windTurbineMast.glb");
    s_rotor = LoadModel("resources/windTurbineRotor.glb");
    s_texture = LoadTexture("resources/windTurbineBasic3Base.png");
    s_mast.materials[0].maps[MATERIAL_MAP_DIFFUSE].texture = s_texture;
    s_rotor.materials[0].maps[MATERIAL_MAP_DIFFUSE].texture = s_texture;


    s_tint = ColorLerp(WHITE, COL_ACCENT, 0.85f);
    place_wind_turbines(WIND_TURBINE_COUNT);
}

void PropsUpdate(void) {
    float dt = GetFrameTime();
    for (int i = 0; i < prop_count; i++) {
        PropInstance *p = &props[i];
        if (p->type == PROP_WIND_TURBINE)
            p->rotation = fmodf(p->rotation + p->rotation_speed * dt, 2.0f * PI);
    }
}

void PropsDraw3D(void) {
    for (int i = 0; i < prop_count; i++) {
        PropInstance *p = &props[i];

        if (p->type == PROP_WIND_TURBINE) {
            DrawModel(s_mast, p->position, WIND_TURBINE_SCALE, s_tint);

            // Rotor hub sits above the mast top; apply per-instance rotation
            Vector3 hub = { p->position.x, p->position.y + WIND_TURBINE_HUB_Y, p->position.z };
            s_rotor.transform = MatrixRotateZ(p->rotation);
            DrawModel(s_rotor, hub, WIND_TURBINE_SCALE, s_tint);
        }
    }
}

void PropsDestroy(void) {
    UnloadTexture(s_texture);
    UnloadModel(s_mast);
    UnloadModel(s_rotor);
}

static const char *prop_type_name(PropType t) {
    switch (t) {
        case PROP_WIND_TURBINE: return "WIND_TURBINE";
        default:                return "UNKNOWN";
    }
}

static PropType prop_type_from_name(const char *name) {
    if (strcmp(name, "WIND_TURBINE") == 0) return PROP_WIND_TURBINE;
    return (PropType)-1; // unrecognised
}

void PropsSave(FILE *f) {
    fprintf(f, "PROPS %d\n", prop_count);
    for (int i = 0; i < prop_count; i++) {
        const PropInstance *p = &props[i];
        fprintf(f, "%s %.6f %.6f %.6f %.6f %.6f\n",
                prop_type_name(p->type),
                p->position.x, p->position.y, p->position.z,
                p->rotation, p->rotation_speed);
    }
}

void PropsLoad(FILE *f) {
    prop_count = 0;
    int n = 0;
    if (fscanf(f, " PROPS %d", &n) != 1) return;
    for (int i = 0; i < n && prop_count < MAX_PROPS; i++) {
        char     type_str[32] = {};
        PropInstance p        = {};
        if (fscanf(f, " %31s %f %f %f %f %f",
                   type_str,
                   &p.position.x, &p.position.y, &p.position.z,
                   &p.rotation, &p.rotation_speed) != 6) return;
        PropType t = prop_type_from_name(type_str);
        if ((int)t < 0) continue; // skip unrecognised types
        p.type = t;
        props[prop_count++] = p;
    }
}
