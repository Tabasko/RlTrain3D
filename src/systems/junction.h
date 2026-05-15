#pragma once

// Initialise the junction system. Call once after TrackSystemInit.
void JunctionSystemInit();

// Process junction placement input. Call once per frame before Draw.
void JunctionSystemUpdate();

// Draw committed junctions and the ghost preview. Must be called inside BeginMode3D.
void JunctionSystemDraw3D();

// Draw 2D placement overlays (frog info, snap hint). Must be called outside BeginMode3D.
void JunctionSystemDraw2D();

// Release all resources.
void JunctionSystemDestroy();
