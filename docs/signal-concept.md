  Why collision volumes are the wrong fit for tile boundary detection

  Train movement in your system is fundamentally a 1D parametric problem: a train moves along a curve, and arc_dist is the single
  scalar that describes its exact position on that curve. The tile boundary crossing check (arc_dist >= tile_length) is not "too much
  math" — it's the minimal, correct expression of the problem.

  If you replaced it with CheckCollisionSpheres at tile endpoints, you'd introduce new problems:

  1. You still need arc_dist to convert position to world space before checking collision — the math doesn't go away, you just call it
   earlier.
  2. Overflow precision is lost. When a train crosses a boundary mid-frame, you need overflow = arc_dist - tile_length to carry the
  remainder into the next tile smoothly. A sphere collision gives you only "yes/no", not how far past the endpoint the train
  travelled. Fast trains would stutter or skip tiles.
  3. Direction ambiguity. A sphere at an endpoint is hit from either side; arc_dist already tells you which direction you came from.
  4. Per-frame order matters. Sphere checks fire at the position after the move step, so you'd always be one frame late catching the
  crossing.

  Where raylib collision genuinely helps

  The pattern CheckCollisionSpheres / CheckCollisionBoxes is well-suited for spatial proximity queries between independent objects,
  not for parametric path-following. In your ECS roadmap, good fits would be:

  ┌──────────────────────────────┬────────────────────────────────────────┬───────────────────────────────────────────────────────┐
  │           Use case           │               Component                │                         Check                         │
  ├──────────────────────────────┼────────────────────────────────────────┼───────────────────────────────────────────────────────┤
  │ Train–train collision        │ CTrainPhysics (world pos)              │ CheckCollisionSpheres between two CTransform          │
  │ warning                      │                                        │ positions                                             │
  ├──────────────────────────────┼────────────────────────────────────────┼───────────────────────────────────────────────────────┤
  │ Train enters station radius  │ CStation { Vector3 pos; float radius;  │ CheckCollisionSpheres vs. train world pos             │
  │                              │ }                                      │                                                       │
  ├──────────────────────────────┼────────────────────────────────────────┼───────────────────────────────────────────────────────┤
  │ Train near a signal          │ CSignal { Vector3 pos; }               │ Same                                                  │
  ├──────────────────────────────┼────────────────────────────────────────┼───────────────────────────────────────────────────────┤
  │ Tile click-pick (Phase 2/3)  │ CTileTrack bounding box                │ CheckCollisionRayBox in PickTile                      │
  └──────────────────────────────┴────────────────────────────────────────┴───────────────────────────────────────────────────────┘

  Those replace manual distance comparisons you'd otherwise write by hand — and that's where the savings are real.

  Verdict: Keep arc_dist for movement. Replace the PickTile chord projection with CheckCollisionRayBox (that's a genuine win), and
  introduce sphere checks when you add stations or signals in Phase 5. The ECS migration makes the spatial checks easy to add later
  without touching the core movement math.
  
  
  ECS SIGNAL SYSTEM
  -----------------
  
  Good case to walk through — it's where CheckCollisionSpheres genuinely earns its keep.

  The conceptual pipeline

  1. What CSignal needs to hold
```c
  typedef struct {
      Vector3 pos;       // world position, set at placement
      float   radius;    // detection sphere radius (~3 world units)
      bool    red;       // current state: true = stop
      int     tile_idx;  // tile whose direction this signal guards
      int     ep_idx;    // endpoint on that tile (0 or 1)
  } CSignal;
```
  The pos is what drives collision. The tile_idx/ep_idx pair is what lets the signal tell a train which direction to stop in, not just
   that it should stop.

  ---
  2. What trains are missing: world-space position
  
  Right now a train only knows tile_idx + arc_dist. CheckCollisionSpheres needs a Vector3. So before the signal check can exist, every
   active train must have a CTransform with its current world position maintained by TrainSystem::Update.

  The conversion already happens implicitly inside TrainCatalogDrawConsist — you need to expose that arc→world calculation so Update
  can call it and write the result into CTransform on the train's entity. Once that's wired up, the train's world position is always
  one frame current.

  Without this step, the collision check has nothing to test against.

  ---
  3. Signal entity lifecycle

  Signal placement mirrors what we just did for CTileBounds:

  - User places a signal on a tile endpoint → create an ECS entity → gs.ecs.signals.add(eid, { pos, radius, false, tile_idx, ep_idx })

  - Signal removed → gs.ecs.destroy(eid)
  - No index-shifting problem here since signals are independent of the s_tiles[] array position

  ---
  4. Where the check lives

  The collision loop belongs in TrainSystem::Update, after all trains have advanced their arc position for the frame (so CTransform is
   already up to date for this frame):

  for each train entity (has CTransform, CTileOccupant, CTrainPhysics):
      for each signal entity (has CSignal):
          if CheckCollisionSpheres(train.pos, TRAIN_SPHERE_R, signal.pos, signal.radius):
              // react

  There is a subtlety: you only want to react when the train is approaching the signal from the guarded direction, not when it's
  already past it and retreating. The direction test is: does the train's current ep_dir (derived from sign(speed)) match the endpoint
   the signal guards? If not, skip. This prevents a train backing away from a red signal from being falsely stopped.

  ---
  5. What "react" means

  Two independent things should happen:

  a) Emit an event — so other systems can observe:
  gs.events.emit(EVENT_TRAIN_PASSES_SIGNAL);
  This is the decoupled path for future signal-logic systems.

  b) Write directly into CTrainPhysics.speed_limit — this is the physics path:
  - Signal is red → set speed_limit = 0.0f (braking to a stop)
  - Signal is green → set speed_limit = def->max_speed (restore full speed)
  
  The physics already has a speed_limit field (it's in CTrainPhysics). The train's braking logic reads it each frame. So the signal
  doesn't need to know anything about acceleration math — it just sets the ceiling, and physics handles the rest.

  ---
  6. The approach order in one sentence each
  
  1. Expose the arc→world position function from TrainCatalogDrawConsist so Update can call it.
  2. Migrate train instances to ECS entities (Phase 3 from the concept doc, or partially — just enough to give each train a
  CTransform).
  3. Add CSignal to components.h and a signals pool to Registry.
  4. Wire signal placement (wherever you handle user input for placing signals) to create/destroy ECS entities.
  5. In TrainSystem::Update, after advancing all trains, loop the pools and call CheckCollisionSpheres.
  6. On hit, check approach direction, then write speed_limit and emit the event.

  The reason this is cleaner than the tile-endpoint approach would be: signals are placed on physical positions in the world, trains
  are objects moving through that world — spatial collision is the natural model. You're not asking "did arc distance cross a
  threshold?" you're asking "are two spheres overlapping?" which is exactly what the function does.
  
  
  SIGNAL STATE
  ------------
  
 Two separate concerns — rendering resources vs. state — and the ECS concept doc already gives you the answer directly.

  Where the mesh goes: system-owned, not in a component

  The concept doc states:

  ▎ "systems only hold resources that are not meaningful to save (loaded models, GPU buffers, etc.)"

  A raylib Model is a GPU resource. It cannot be serialized, it is shared across every signal instance, and it has no meaning outside
  Init()/Destroy(). It belongs in SignalSystem as a private field — exactly like TrainSystem owns car_models[]:
```c
  // signal.h
  class SignalSystem : public ISystem {
  public:
      void Init()    override; // LoadModel("assets/KS-Sig.glb")
      void Update()  override; // placement input + collision checks
      void Draw3D()  override; // iterate gs.ecs.signals, DrawModel at each pos
      void Draw2D()  override {}
      void Destroy() override; // UnloadModel
  private:
      Model s_model;
      bool  s_model_ok = false;
  };
```
  Why not CRenderable? A generic CRenderable { Model* model; } would store a pointer into GPU memory inside a component — it couples
  the data layer to the resource layer. It also provides no benefit here: signals, trains, and props all have different draw logic
  (different transforms, animation, tinting by state). Each system renders its own pool directly. A RenderSystem that dispatches all
  draw calls would just be a wrapper around the same per-type switch statement, adding indirection for no gain. At 6–8 fixed systems,
  explicit iteration beats dispatch magic (the concept doc makes this point for the same reason with component-signature
  registration).

  Where the state goes: CSignal

  State that changes at runtime, is read by other systems, and must survive a save/load cycle belongs in a component. That is exactly
  CSignal.

  The current definition you added is close. One thing worth reconsidering: bool red is too narrow for a real railway signal. KS-Sig
  (Kombinationssignal) has multiple aspects. An enum fits better:
```c
  typedef enum {
      SIGNAL_PROCEED = 0, // Hp1 — green, full speed
      SIGNAL_CAUTION,     // Hp2 — yellow, reduced speed
      SIGNAL_STOP,        // Hp0 — red, speed = 0
  } SignalAspect;

  typedef struct {
      Vector3      pos;      // world position
      float        radius;   // detection sphere radius
      SignalAspect aspect;   // current displayed aspect
      int          tile_idx; // tile this signal guards
      int          ep_idx;   // endpoint (approach direction)
  } CSignal;
```
  SignalSystem::Draw3D() reads aspect to choose a tint color or swap the mesh animation frame. TrainSystem::Update() reads aspect to
  set CTrainPhysics.speed_limit — STOP → 0, CAUTION → half speed, PROCEED → full speed.

  The full picture

  SignalSystem::Init()
    └─ LoadModel("assets/KS-Sig.glb") → s_model (private, GPU)

  user places signal
    └─ create ECS entity → gs.ecs.signals.add(eid, { pos, radius, SIGNAL_PROCEED, tile_idx, ep_idx })

  SignalSystem::Update()
    └─ handle placement input
    └─ (later) CheckCollisionSpheres vs. train CTransforms → write aspect

  SignalSystem::Draw3D()
    └─ for each CSignal in gs.ecs.signals:
         DrawModelEx(s_model, signal.pos, ..., tint_by_aspect(signal.aspect))

  TrainSystem::Update()
    └─ after advancing arc_dist:
         CheckCollisionSpheres vs. each CSignal → write physics.speed_limit

  save/load
    └─ serialize gs.ecs.signals pool (pos, aspect, tile_idx, ep_idx)
       — model path is a constant, not saved

  The mesh is never in a component. The state is never in the system. Each owns exactly what it's responsible for.