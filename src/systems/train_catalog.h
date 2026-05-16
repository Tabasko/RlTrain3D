#pragma once
#include "../types.h"

// World-unit gap between car couplings within a consist.
#define CAR_COUPLING_GAP 0.1f

// One car in a consist (locomotive or wagon).
typedef struct {
    const char *model_path; // path relative to working directory
    float       length;     // car length in world units; drives consist spacing
    float       scale;      // uniform mesh scale
} CarDef;

// Static definition of a train type. All fields are constant after catalog init.
typedef struct {
    const char *name;
    float       max_speed;                // world units/sec
    float       accel;                    // world units/sec²
    CarDef      cars[MAX_CARS_PER_TRAIN]; // [0] = locomotive / lead unit
    int         car_count;
} TrainDef;

// Load all models referenced by the catalog. Call once after InitWindow.
void            TrainCatalogInit();

// Unload all cached models.
void            TrainCatalogDestroy();

// Return a catalog entry by index, or NULL if idx is out of range.
const TrainDef *TrainCatalogGet(int idx);

// Number of entries in the catalog.
int             TrainCatalogCount();

// Draw the full consist for def_idx.
// front_tile / front_arc locate the front coupling of car[0].
// ep_fwd: 1 if moving toward eps[1], 0 if toward eps[0].
void TrainCatalogDrawConsist(int def_idx, int front_tile, float front_arc, int ep_fwd);
