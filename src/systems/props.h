#pragma once

#include "raylib.h"
#include <stdio.h>

// Prop types supported by the props system.
// Add a new entry here and handle it in props.cpp to introduce a new prop kind.
typedef enum {
    PROP_WIND_TURBINE,
    PROP_TYPE_COUNT
} PropType;

// One placed instance of any prop.
typedef struct {
    PropType type;
    Vector3  position;
    float    rotation;        // current rotor angle in radians
    float    rotation_speed;  // radians per second (randomised per instance)
} PropInstance;

// Scatter props and load shared models/materials.
void PropsInit(void);

// Advance all per-instance animation state.
void PropsUpdate(void);

// Draw all prop instances in world space.
void PropsDraw3D(void);

// Unload all models and materials.
void PropsDestroy(void);

// Serialize all prop instances to an open file.
void PropsSave(FILE *f);

// Replace all prop instances from an open file.
void PropsLoad(FILE *f);
