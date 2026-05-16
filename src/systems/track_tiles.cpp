#include "track_tiles.h"
#include "raymath.h"

std::vector<PlacedTile>   s_tiles;
std::vector<JunctionNode> s_junctions;

static std::vector<Matrix> s_inst_buf[TILE_TYPE_COUNT];

void RebuildInstanceBuffers() {
    for (int t = 0; t < TILE_TYPE_COUNT; t++)
        s_inst_buf[t].clear();
    for (const PlacedTile& tile : s_tiles) {
        Matrix m = tile.world;
        if (IsLeftCurve(tile.type))
            m = MirrorX(m);
        s_inst_buf[tile.type].push_back(m);
    }
}

void AutoLink(int new_idx) {
    PlacedTile& nt = s_tiles[new_idx];
    for (int ep = 0; ep < nt.ep_count; ep++) {
        if (nt.eps[ep].linked_tile     != TILE_NO_LINK) continue;
        if (nt.eps[ep].linked_junction != TILE_NO_LINK) continue;

        Vector3 np      = nt.eps[ep].pos;
        bool    handled = false;

        // Phase 1: attach to an existing junction node at this position.
        for (int ji = 0; ji < (int)s_junctions.size() && !handled; ji++) {
            if (Vector3Distance(np, s_junctions[ji].pos) >= SNAP_EP_R) continue;
            JunctionNode& jn = s_junctions[ji];
            if (jn.leg_count >= 3) continue; // full
            jn.legs[jn.leg_count++] = { new_idx, ep };
            nt.eps[ep].linked_junction = ji;
            handled = true;
        }
        if (handled) continue;

        // Phase 2: collect all nearby endpoints not yet part of a junction.
        // Scans both open AND directly-linked endpoints so that placing a third
        // track at an existing 2-track link position upgrades it to a junction.
        struct Cand { int ti, ei; };
        Cand near[4];
        int  cnt = 0;
        for (int ti = 0; ti < (int)s_tiles.size() && cnt < 4; ti++) {
            if (ti == new_idx) continue;
            for (int oep = 0; oep < s_tiles[ti].ep_count && cnt < 4; oep++) {
                if (s_tiles[ti].eps[oep].linked_junction != TILE_NO_LINK) continue;
                if (Vector3Distance(np, s_tiles[ti].eps[oep].pos) < SNAP_EP_R)
                    near[cnt++] = { ti, oep };
            }
        }

        if (cnt == 0) continue;

        if (cnt == 1 && s_tiles[near[0].ti].eps[near[0].ei].linked_tile == TILE_NO_LINK) {
            // Simple 2-way link.
            nt.eps[ep].linked_tile                       = near[0].ti;
            nt.eps[ep].linked_ep                         = near[0].ei;
            s_tiles[near[0].ti].eps[near[0].ei].linked_tile = new_idx;
            s_tiles[near[0].ti].eps[near[0].ei].linked_ep   = ep;
            continue;
        }

        // 2+ nearby endpoints, or 1 that is already directly linked: create junction.
        // The newly placed tile becomes the stem (legs[0]).
        JunctionNode jn = {};
        jn.pos                  = np;
        jn.legs[jn.leg_count++] = { new_idx, ep };
        for (int n = 0; n < cnt && jn.leg_count < 3; n++)
            jn.legs[jn.leg_count++] = { near[n].ti, near[n].ei };
        jn.thrown = 0;

        int ji = (int)s_junctions.size();
        s_junctions.push_back(jn);

        // Mark all participating endpoints and clear any stale direct link.
        for (int l = 0; l < jn.leg_count; l++) {
            int lt = jn.legs[l].tile_idx, lep = jn.legs[l].ep_idx;
            s_tiles[lt].eps[lep].linked_tile     = TILE_NO_LINK;
            s_tiles[lt].eps[lep].linked_ep       = TILE_NO_LINK;
            s_tiles[lt].eps[lep].linked_junction = ji;
        }
    }
}

// Erase junction ji from s_junctions and fix all linked_junction references.
static void EraseJunction(int ji) {
    s_junctions.erase(s_junctions.begin() + ji);
    for (PlacedTile& t : s_tiles)
        for (int ep = 0; ep < t.ep_count; ep++)
            if (t.eps[ep].linked_junction > ji)
                t.eps[ep].linked_junction--;
}

void RemoveTile(int idx) {
    // Step 1: detach this tile from any junctions, downgrading as needed.
    for (int ji = (int)s_junctions.size() - 1; ji >= 0; ji--) {
        JunctionNode& jn = s_junctions[ji];

        // Remove legs that belong to the dying tile.
        for (int l = jn.leg_count - 1; l >= 0; l--) {
            if (jn.legs[l].tile_idx != idx) continue;
            for (int k = l; k < jn.leg_count - 1; k++) jn.legs[k] = jn.legs[k + 1];
            jn.leg_count--;
        }

        if (jn.leg_count == 3) continue; // still a valid junction

        if (jn.leg_count == 2) {
            // Downgrade to a direct 2-way link.
            int t0 = jn.legs[0].tile_idx, e0 = jn.legs[0].ep_idx;
            int t1 = jn.legs[1].tile_idx, e1 = jn.legs[1].ep_idx;
            s_tiles[t0].eps[e0].linked_junction = TILE_NO_LINK;
            s_tiles[t1].eps[e1].linked_junction = TILE_NO_LINK;
            s_tiles[t0].eps[e0].linked_tile = t1;
            s_tiles[t0].eps[e0].linked_ep   = e1;
            s_tiles[t1].eps[e1].linked_tile = t0;
            s_tiles[t1].eps[e1].linked_ep   = e0;
            EraseJunction(ji);
        } else if (jn.leg_count == 1) {
            // Release the remaining leg as an open endpoint.
            int lt = jn.legs[0].tile_idx, lep = jn.legs[0].ep_idx;
            s_tiles[lt].eps[lep].linked_junction = TILE_NO_LINK;
            EraseJunction(ji);
        } else {
            EraseJunction(ji); // leg_count == 0
        }
    }

    // Step 2: unlink direct tile-to-tile connections.
    PlacedTile& dying = s_tiles[idx];
    for (int ep = 0; ep < dying.ep_count; ep++) {
        int lt = dying.eps[ep].linked_tile;
        int le = dying.eps[ep].linked_ep;
        if (lt != TILE_NO_LINK && lt < (int)s_tiles.size()) {
            s_tiles[lt].eps[le].linked_tile = TILE_NO_LINK;
            s_tiles[lt].eps[le].linked_ep   = TILE_NO_LINK;
        }
    }

    s_tiles.erase(s_tiles.begin() + idx);

    // Step 3: fix indices that shifted after the erase.
    for (PlacedTile& t : s_tiles)
        for (int ep = 0; ep < t.ep_count; ep++)
            if (t.eps[ep].linked_tile > idx)
                t.eps[ep].linked_tile--;

    for (JunctionNode& jn : s_junctions)
        for (int l = 0; l < jn.leg_count; l++)
            if (jn.legs[l].tile_idx > idx)
                jn.legs[l].tile_idx--;
}

void PlaceTile(TileType type, Vector3 pos, float heading, ArcDirection dir) {
    // TODO check the placement here
    PlacedTile t;
    t.type      = type;
    t.direction = dir;
    t.ep_count  = 2;
    t.world     = TileMatrix(pos, heading);

    t.eps[0].pos             = pos;
    t.eps[0].heading         = heading;
    t.eps[0].linked_tile     = TILE_NO_LINK;
    t.eps[0].linked_ep       = TILE_NO_LINK;
    t.eps[0].linked_junction = TILE_NO_LINK;

    Vector3 next_pos; float next_h;
    WalkTile(type, pos, heading, &next_pos, &next_h);
    t.eps[1].pos             = next_pos;
    t.eps[1].heading         = next_h;
    t.eps[1].linked_tile     = TILE_NO_LINK;
    t.eps[1].linked_ep       = TILE_NO_LINK;
    t.eps[1].linked_junction = TILE_NO_LINK;

    s_tiles.push_back(t);
    AutoLink((int)s_tiles.size() - 1);
}

void SampleTileLine(Vector3 a, Vector3 b, Vector3 out[TILE_SAMPLE_N]) {
    for (int i = 0; i < TILE_SAMPLE_N; i++) {
        float t = (float)i / (TILE_SAMPLE_N - 1);
        out[i] = { a.x + t*(b.x - a.x), 0.0f, a.z + t*(b.z - a.z) };
    }
}

bool IsEndpointAt(Vector3 pos) {
    for (const PlacedTile& t : s_tiles)
        for (int ep = 0; ep < t.ep_count; ep++)
            if (t.eps[ep].linked_junction == TILE_NO_LINK &&
                Vector3Distance(pos, t.eps[ep].pos) < OVERLAP_R)
                return true;
    for (const JunctionNode& jn : s_junctions)
        if (Vector3Distance(pos, jn.pos) < OVERLAP_R)
            return true;
    return false;
}

bool TryThrowJunction(Vector3 pos) {
    for (JunctionNode& jn : s_junctions) {
        if (Vector3Distance(pos, jn.pos) < SNAP_EP_R * 2.0f) {
            jn.thrown = 1 - jn.thrown;
            return true;
        }
    }
    return false;
}

bool GhostCollides(Vector3 entry, Vector3 exit) {
    bool entry_exempt = IsEndpointAt(entry);
    bool exit_exempt  = IsEndpointAt(exit);

    Vector3 ghost[TILE_SAMPLE_N];
    SampleTileLine(entry, exit, ghost);

    for (const PlacedTile& t : s_tiles) {
        // Does this tile share an endpoint with the ghost's entry or exit?
        // If so, samples on both sides near that shared point must be exempted,
        // because sample spacing (0.25) is smaller than OVERLAP_R (0.5).
        bool t_near_entry = Vector3Distance(t.eps[0].pos, entry) < OVERLAP_R
                         || Vector3Distance(t.eps[1].pos, entry) < OVERLAP_R;
        bool t_near_exit  = Vector3Distance(t.eps[0].pos, exit)  < OVERLAP_R
                         || Vector3Distance(t.eps[1].pos, exit)  < OVERLAP_R;

        Vector3 tile[TILE_SAMPLE_N];
        SampleTileLine(t.eps[0].pos, t.eps[1].pos, tile);

        for (int i = 0; i < TILE_SAMPLE_N; i++) {
            if (entry_exempt && Vector3Distance(ghost[i], entry) < OVERLAP_R) continue;
            if (exit_exempt  && Vector3Distance(ghost[i], exit)  < OVERLAP_R) continue;
            for (int j = 0; j < TILE_SAMPLE_N; j++) {
                if (t_near_entry && Vector3Distance(tile[j], entry) < OVERLAP_R) continue;
                if (t_near_exit  && Vector3Distance(tile[j], exit)  < OVERLAP_R) continue;
                if (Vector3Distance(ghost[i], tile[j]) < OVERLAP_R)
                    return true;
            }
        }
    }
    return false;
}
