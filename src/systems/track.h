#pragma once
#include "../types.h"
#include <stdio.h>

// Initialise GPU resources and load tile meshes. Call once after InitWindow.
void TrackSystemInit();

// Process placement input. Call once per frame before Draw.
void TrackSystemUpdate();

// Draw committed tiles and the in-progress ghost. Must be called inside BeginMode3D.
void TrackSystemDraw3D();

// Draw 2D placement overlays. Must be called outside BeginMode3D.
void TrackSystemDraw2D();

// Release all GPU resources.
void TrackSystemDestroy();

// Serialize all placed tiles to an open file.
void TrackSystemSave(FILE *f);

// Replace all placed tiles from an open file; rebuilds instance buffers.
void TrackSystemLoad(FILE *f);
