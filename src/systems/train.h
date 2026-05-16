#pragma once
#include <stdio.h>

// Initialise GPU resources and load the tram model. Call once after InitWindow.
void TrainSystemInit();

// Process placing input and advance all active trains. Call once per frame before Draw.
void TrainSystemUpdate();

// Draw all active trains. Must be called inside BeginMode3D.
void TrainSystemDraw3D();

// Release all GPU resources.
void TrainSystemDestroy();

// Serialize all active trains to an open file.
void TrainSystemSave(FILE *f);

// Replace all active trains from an open file.
void TrainSystemLoad(FILE *f);
