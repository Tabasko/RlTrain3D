#include "registry.h"

EntityID Registry::create() {
    return next_id++;
}

// Removes the entity from every component pool.
// Each pool's remove() is a no-op if the entity has no component of that type.
void Registry::destroy(EntityID id) {
    transforms.remove(id);
    tile_occupants.remove(id);
    train_physics.remove(id);
    train_paths.remove(id);
    train_defs.remove(id);
    tile_tracks.remove(id);
    junctions.remove(id);
    props.remove(id);
}
