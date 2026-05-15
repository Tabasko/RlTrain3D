#pragma once
#include "track_geom.h"
#include "track.h"
#include <vector>

static constexpr float OVERLAP_R     = 0.5f;
static constexpr int   TILE_SAMPLE_N = 9; // sample points along each tile centerline

// The live tile and junction arrays. Exposed for draw, save/load, and routing.
extern std::vector<PlacedTile>  s_tiles;
extern std::vector<JunctionNode> s_junctions;

// Rebuild per-type instance matrices from s_tiles. Call after any add/remove.
void RebuildInstanceBuffers();

// Link the endpoints of the new tile at new_idx to any matching open endpoints.
void AutoLink(int new_idx);

// Unlink all references to tile at idx, erase it, and fix up remaining indices.
void RemoveTile(int idx);

// Append a tile to s_tiles, compute its endpoints, and call AutoLink.
void PlaceTile(TileType type, Vector3 pos, float heading, ArcDirection dir);

// Fill out[TILE_SAMPLE_N] with evenly-spaced points on the chord from a to b.
// For straights this is exact; for curves it is a chord approximation (arc bulge
// is smaller than OVERLAP_R for all current tiles).
void SampleTileLine(Vector3 a, Vector3 b, Vector3 out[TILE_SAMPLE_N]);

// True if pos is within OVERLAP_R of any endpoint the ghost may validly connect to:
// open endpoints, directly-linked endpoints eligible for junction upgrade, and
// existing junction node positions.
bool IsEndpointAt(Vector3 pos);

// If a junction exists within snap range of pos, toggle its thrown state and return true.
bool TryThrowJunction(Vector3 pos);

// True if the ghost tile [entry→exit] body overlaps any placed tile.
// Ghost entry/exit that land on an open endpoint are exempt (valid connections).
bool GhostCollides(Vector3 entry, Vector3 exit);
