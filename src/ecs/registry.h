#pragma once
#include <assert.h>
#include <stdint.h>
#include "components.h"

// EntityID is typedef'd in types.h so C-style structs can hold handles without
// pulling in this header. The constant lives here since it is registry-specific.
static constexpr EntityID ENTITY_NULL = 0; // reserved; valid IDs start at 1

// ---------------------------------------------------------------------------
// ComponentPool<T, MaxCount>
//
// Fixed-capacity parallel arrays: ids[i] identifies the entity that owns
// data[i]. Lookup is a linear scan — acceptable for the entity counts in this
// project (< 256 for any pool).
//
// add()    — writes to the next free slot; asserts if full.
// get()    — returns a pointer to the component, or nullptr if the entity has none.
// remove() — swap-remove: moves the last slot into the freed slot so the live
//            range stays contiguous; O(count).
// ---------------------------------------------------------------------------
template<typename T, int MaxCount>
struct ComponentPool {
    EntityID ids[MaxCount]  = {};
    T        data[MaxCount] = {};
    int      count          = 0;

    T& add(EntityID id, T init = {}) {
        assert(count < MaxCount);
        ids[count]  = id;
        data[count] = init;
        return data[count++];
    }

    T* get(EntityID id) {
        for (int i = 0; i < count; i++)
            if (ids[i] == id) return &data[i];
        return nullptr;
    }

    void remove(EntityID id) {
        for (int i = 0; i < count; i++) {
            if (ids[i] == id) {
                ids[i]  = ids[count - 1];
                data[i] = data[count - 1];
                count--;
                return;
            }
        }
    }
};

// ---------------------------------------------------------------------------
// Registry
//
// Owns all component pools and the entity ID counter.
// create() — allocates a new ID; the entity has no components until add() is called.
// destroy() — removes the entity from every pool.
// ---------------------------------------------------------------------------
struct Registry {
    EntityID next_id = 1;

    ComponentPool<CTransform,    128 + 256>  transforms;    // trains + props
    ComponentPool<CTileOccupant, 128>        tile_occupants;
    ComponentPool<CTrainPhysics, 128>        train_physics;
    ComponentPool<CTrainPath,    128>        train_paths;
    ComponentPool<CTrainDef,     128>        train_defs;
    ComponentPool<CTileTrack,    1024>       tile_tracks;
    ComponentPool<CTileBounds,   1024>       tile_bounds;   // AABB per tile for ray-pick
    ComponentPool<CJunction,     64>         junctions;
    ComponentPool<CProp,         256>        props;
    ComponentPool<CSignal,       256>        signals;

    EntityID create();
    void     destroy(EntityID id); // removes the entity from all pools
};
