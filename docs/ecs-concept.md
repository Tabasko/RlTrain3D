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
  `constexpr` for compile-time limits; virtual dispatch for the system interface only.
  No RTTI, no exceptions.
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

A system is a class that owns its GPU resources and other private state, and iterates
specific component pools each frame. All simulation state lives in components; systems
only hold resources that are not meaningful to save (loaded models, GPU buffers, etc.).

Every system implements `ISystem`, a thin abstract interface:

```cpp
struct ISystem {
    virtual void Init()    = 0;  // load GPU resources; called once after InitWindow
    virtual void Update()  = 0;  // advance simulation; called once per frame
    virtual void Draw3D()  = 0;  // draw world geometry; called inside BeginMode3D
    virtual void Draw2D()  = 0;  // draw screen-space overlays; called outside BeginMode3D
    virtual void Destroy() = 0;  // release GPU resources; called before CloseWindow
    virtual ~ISystem()     = default;
};
```

`Draw2D` defaults to a no-op for systems that have no screen-space output.

`main.cpp` holds an ordered array of registered systems and drives them through each
phase in sequence — it never calls a concrete system type directly:

```cpp
ISystem* systems[] = {
    &environment_system,
    &track_system,
    &train_system,
    &props_system,
};

// In the game loop:
for (auto* s : systems) s->Update();
BeginMode3D(...);
for (auto* s : systems) s->Draw3D();
EndMode3D();
for (auto* s : systems) s->Draw2D();
```

**Why not component-signature registration?**
A signature system (where each system declares which component types it reacts to and
the registry routes matching entities automatically) adds a component bitmask per entity
and an archetype query engine. That complexity pays off when you have dozens of systems
with overlapping interests or systems added at runtime. With 6–8 fixed domain systems,
the same result is achieved more clearly by each system iterating its pools directly.
The registration here is just the `systems[]` array — ordered, explicit, no dispatch magic.

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

### Concrete system example

```cpp
class TrainSystem : public ISystem {
public:
    void Init()    override;
    void Update()  override;
    void Draw3D()  override;
    void Draw2D()  override {}   // no screen-space output
    void Destroy() override;
private:
    // GPU resources owned by this system — not saved, not shared
    Model car_models[MAX_CARS_PER_TRAIN];
};
```

Systems iterate their pools directly — no query API, no hidden dispatch:

```cpp
void TrainSystem::Update() {
    auto& phys = gs.ecs.train_physics;
    auto& occ  = gs.ecs.tile_occupants;

    for (int i = 0; i < phys.count; i++) {
        EntityID       id = phys.ids[i];
        CTrainPhysics&  p = phys.data[i];
        CTileOccupant*  o = occ.get(id);  // nullptr = not on a tile (waiting)
        if (!o) continue;
        // … advance physics …
    }
}
```

This is intentionally explicit — no archetype shuffling, straightforward to step through
in a debugger.

---

## Event System

Two event implementations exist in the codebase. They have different roles and only one
belongs in the ECS architecture.

### `EventBus` (`event_bus.h`) — keep, fits ECS naturally

`EventBus` is a double-buffered, frame-delayed bus. Events emitted during frame N become
readable in frame N+1. This maps directly onto the `ISystem` update model: systems never
call into each other — they leave messages that peers read on the next pass.

```
Frame N:   TrainSystem::Update()  → gs.events.emit(EVENT_TRAIN_ARRIVED_STATION)
Frame N+1: SignalSystem::Update() → if (gs.events.has(EVENT_TRAIN_ARRIVED_STATION)) ...
```

New domain events (`TRAIN_ARRIVED_STATION`, `TRAIN_PASSES_SIGNAL`, `TRAIN_DEPARTED`)
should be added as entries in the `EventType` enum in `event_bus.h` as they are needed.
The `EventData` union is extended alongside them.

### `EventBusSystem` (`event_system.h/.cpp`) — remove

`EventBusSystem` is a subscribe/callback model. When `dispatch()` fires it invokes
listener callbacks synchronously, meaning system B's code runs *inside* system A's
`Update()`. That breaks the clean per-phase execution order the `ISystem` interface
enforces.

Its payload type (`std::unordered_map<std::string, std::variant<...>>`) is heap-allocated
and string-keyed — the wrong shape for code on the per-frame hot path.

The `_` prefixes on all its types (`_EventType`, `_Event`, `_EventValue`) and the empty
`.cpp` indicate it was an unfinished draft. These files can be deleted; the domain events
it sketched are covered by extending `event_bus.h`.

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
2. Add `src/ecs/isystem.h` with the `ISystem` interface.
3. Add `Registry ecs` to `GameState`; wire up the `systems[]` array in `main.cpp`
   using the existing system singletons — behaviour is unchanged.
4. Define component structs in `src/ecs/components.h`; keep them matching the
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
    isystem.h       — ISystem interface
```

Systems continue to live in `src/systems/`. They `#include "../ecs/registry.h"` and
`#include "../ecs/isystem.h"`, then inherit from `ISystem` and access `gs.ecs` directly.
No new inter-system includes are needed.

---

## C++ Features Used

| Feature              | Where                            | Why                                          |
|----------------------|----------------------------------|----------------------------------------------|
| `template<T, Cap>`   | `ComponentPool`                  | One pool implementation, many component types|
| `using EntityID`     | throughout                       | Readable type alias for a plain integer       |
| `static constexpr`   | `ENTITY_NULL`, capacity defaults | Zero-cost compile-time constants              |
| `nullptr` return     | `ComponentPool::get`             | Idiomatic optional-presence check             |
| `= default` / `= {}` | Pool zero-init                  | Avoids manual memset in constructors          |
| `virtual` / `override` | `ISystem`                      | Uniform lifecycle interface for `main.cpp`    |
| `= default` destructor | `ISystem`                      | Safe base-class destruction without overhead  |

No STL containers, no heap allocation, no exceptions, no RTTI.
Virtual dispatch is used only for the `ISystem` interface — component pools and the
registry use none.
