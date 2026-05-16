# ECS Concept — RlTrain3D

## Motivation

The current architecture is a set of monolithic systems that own both their data and their
logic in parallel flat arrays (e.g. `s_tile_trains[MAX_TILE_TRAINS]`, `s_props[MAX_PROPS]`).
This works well but has recurring friction points:

- Adding a new behaviour to trains (e.g. damage, cargo state, a signal receiver) requires
  widening the `TileTrain` struct and touching unrelated code in `train.cpp`.
- Prop animation, train physics, and junction logic share no infrastructure — every new
  object type duplicates the same slot-management bookkeeping.
- There is no way to compose behaviours at runtime (e.g. a tile that is _both_ a junction
  _and_ a signal) without ad-hoc flag fields.

A lightweight ECS lets us separate _identity_ (entity ID), _data_ (components), and
_logic_ (systems) so that each concern is extended independently.

---

## Design Goals

- **Minimal and readable.** No macro magic, no external library. The entire core fits in
  one header + one translation unit.
- **Fixed capacity.** Entity counts are bounded by existing `MAX_*` constants; no dynamic
  allocation inside the registry.
- **C-compatible data.** Components are plain `typedef struct` types from `types.h`.
  The ECS layer is C++ but its component payloads remain C-style structs so the rest of
  the codebase does not have to change.
- **C++ only where it earns its keep.** Templates for type-safe component pools;
  `constexpr` for compile-time limits. No virtual dispatch, no RTTI, no exceptions.
- **Coexist with `GameState`.** `AppState`, `UiState`, camera, and the event bus are not
  entities — they stay in `gs`. The registry is an additional field of `GameState`.

---

## Core Concepts

### Entity

An entity is a lightweight handle — nothing more than a typed integer:

```cpp
using EntityID = uint32_t;
static constexpr EntityID ENTITY_NULL = 0;
```

The value `0` is reserved as "no entity". Valid IDs start at `1`. An entity on its own
carries no data; it only becomes meaningful through the components attached to it.

### Component

A component is a plain data struct. It holds _state_, never logic.

Examples drawn from the existing codebase:

| Component struct       | Data it holds                                              |
|------------------------|------------------------------------------------------------|
| `CTransform`           | `Vector3 pos`, `float heading`                             |
| `CTileOccupant`        | `int tile_idx`, `float arc_dist`                           |
| `CTrainPhysics`        | `float speed`, `float target_speed`, `float speed_limit`   |
| `CTrainPath`           | `path_arcs[]`, `path_arc_count`, `target_node`, …          |
| `CTrainDef`            | `int def_idx` (index into `TRAIN_CATALOG`)                 |
| `CTileTrack`           | `TileType type`, `ArcDirection direction`, `Matrix world`  |
| `CJunction`            | `JunctionLeg legs[3]`, `int thrown`, `int leg_count`       |
| `CProp`                | `PropType type`, `float rotation`, `float rotation_speed`  |

Each component type maps one-to-one to a pool inside the registry.

### System

A system is a free function (or a small group of functions) that queries the registry for
entities that own a specific set of components and acts on them. Systems carry _no state_
of their own — all persistent state lives in components.

The existing naming convention `SystemVerbNoun` is kept. Systems still expose the
`Init / Update / Draw / Destroy` interface used in `main.cpp`.

### Registry

The registry is a thin container that maps `(EntityID, ComponentType)` pairs to component
data. It owns the component pools and the entity lifecycle (create / destroy).

---

## Implementation Approach

### Component Pool

Each component type gets a fixed-size array of slots paired with a parallel array of
entity IDs. Lookup is a linear scan over the occupied slots — acceptable for the entity
counts in this project (`MAX_TILE_TRAINS 128`, `MAX_TRAINS 64`, a few hundred tiles).

```cpp
template<typename T, int Cap>
struct ComponentPool {
    EntityID ids[Cap]  = {};
    T        data[Cap] = {};
    int      count     = 0;

    T*  get(EntityID id);           // O(count), returns nullptr if absent
    T&  add(EntityID id, T init);   // asserts count < Cap
    void remove(EntityID id);       // swap-remove; O(count)
};
```

`get` returning `nullptr` is the natural way for a system to skip entities that lack an
optional component (e.g. not all trains have an active route).

### Registry

```cpp
struct Registry {
    EntityID next_id = 1;

    ComponentPool<CTransform,    MAX_TILE_TRAINS + MAX_PROPS> transforms;
    ComponentPool<CTileOccupant, MAX_TILE_TRAINS>             tile_occupants;
    ComponentPool<CTrainPhysics, MAX_TILE_TRAINS>             train_physics;
    ComponentPool<CTrainPath,    MAX_TILE_TRAINS>             train_paths;
    ComponentPool<CTrainDef,     MAX_TILE_TRAINS>             train_defs;
    ComponentPool<CTileTrack,    MAX_SEGMENTS>                tile_tracks;
    ComponentPool<CJunction,     MAX_JUNC_ENDS>               junctions;
    ComponentPool<CProp,         256>                         props;

    EntityID create();
    void     destroy(EntityID id);   // removes from all pools
};
```

`Registry` becomes a field of `GameState`:

```cpp
typedef struct {
    RPGCamera  camera;
    UiState    ui;
    AppState   app;
    EventBus   events;
    EventBusSystem bus;
    Registry   ecs;          // ← new
} GameState;
```

### Iteration pattern

Systems iterate a pool directly rather than through a query API:

```cpp
void TrainSystemUpdate() {
    auto& phys  = gs.ecs.train_physics;
    auto& occ   = gs.ecs.tile_occupants;

    for (int i = 0; i < phys.count; i++) {
        EntityID id  = phys.ids[i];
        CTrainPhysics& p = phys.data[i];
        CTileOccupant* o = occ.get(id);   // nullptr = not on a tile (waiting)
        if (!o) continue;
        // … advance physics …
    }
}
```

This is intentionally explicit — no hidden query building, no archetype shuffling.

---

## What is NOT migrated

Some state belongs in `GameState` flat fields, not in the ECS:

| Current thing          | Reason to keep it out of ECS                          |
|------------------------|-------------------------------------------------------|
| `AppState` / `UiState` | Singleton mode flags, not per-entity data             |
| `RPGCamera`            | Single camera, no components to compose               |
| `EventBus`             | Cross-cutting infrastructure, not an entity           |
| `TrainDef` catalog     | Static read-only data, not runtime state              |
| `ArcLUT` table         | Geometry cache keyed by tile type, not by entity      |

---

## Migration Plan

The migration is incremental. Each step leaves the build passing and the game playable.

### Phase 1 — Core infrastructure (no behaviour change)

1. Add `src/ecs/registry.h` with `ComponentPool<T,Cap>` and `Registry`.
2. Add `Registry ecs` to `GameState`; call nothing new in `main.cpp` yet.
3. Define component structs in `src/ecs/components.h`; keep them matching the
   existing struct fields one-to-one so migration is a mechanical copy.

### Phase 2 — Migrate props

Props are the simplest object type (no routing, no physics graph).

1. Replace `s_props[]` in `props.cpp` with ECS entities carrying `CProp` + `CTransform`.
2. `PropsInit` creates entities; `PropsUpdate` / `PropsDraw3D` iterate `CProp` pool.
3. `PropsSave` / `PropsLoad` iterate the pool and serialize component data.

### Phase 3 — Migrate tile trains

1. Replace `s_tile_trains[]` with entities carrying `CTrainDef + CTileOccupant +
   CTrainPhysics`.
2. Routing and path state go into `CTrainPath`.
3. `TrainSystemUpdate` iterates `CTrainPhysics`; drawing iterates `CTrainDef`.

### Phase 4 — Migrate placed tiles

1. Replace `s_placed_tiles[]` with entities carrying `CTileTrack`.
2. Junction nodes become entities with `CJunction`.
3. Instance buffers are rebuilt by scanning the `CTileTrack` pool, same as before.

### Phase 5 — Extend with new components (post-migration payoff)

Once the foundation is in place, new behaviours are additive:

- `CSignal { bool red; int tile_idx; int ep_idx; }` — attach to junction entities.
- `CCargo { CargoType type; int amount; }` — attach to train entities selectively.
- `CDamage { float health; }` — attach to any entity; a damage system iterates it.

---

## File Layout

```
src/
  ecs/
    registry.h      — ComponentPool<T,Cap>, Registry, EntityID
    registry.cpp    — Registry::destroy (removes from all pools)
    components.h    — all component struct definitions
```

Systems continue to live in `src/systems/`. They `#include "../ecs/registry.h"` and
access `gs.ecs` directly — no new inter-system includes are needed.

---

## C++ Features Used

| Feature             | Where                            | Why                                      |
|---------------------|----------------------------------|------------------------------------------|
| `template<T, Cap>`  | `ComponentPool`                  | One pool implementation, many types      |
| `using EntityID`    | throughout                       | Readable type alias for a plain integer  |
| `static constexpr`  | `ENTITY_NULL`, capacity defaults | Zero-cost compile-time constants         |
| `nullptr` return    | `ComponentPool::get`             | Idiomatic optional-presence check        |
| `= default` / `= {}` | Pool zero-init                  | Avoids manual memset in constructors     |

No STL containers, no heap allocation, no exceptions, no virtual functions.
