# Routing, Signals, and Train Movement — Concept

## Foundation: the node-arc graph

The track system models the railway as a directed graph:

- **TrackNode** — a connection point. Endpoint nodes have one arc; junction nodes have three or more.
- **TrackArc** — a Catmull-Rom curve between two nodes, parameterised by `t ∈ [0, 1]`. Stores anchors and a precomputed arc length.

This graph is the single source of truth for all higher-level systems.

---

## Train position

A train is a cursor on the graph:

```
struct TrainState {
    int   arc_id;   // which arc the train is on
    float t;        // position within that arc, 0 = node_a end, 1 = node_b end
    bool  forward;  // traversal direction (node_a → node_b when true)
};
```

Each frame, advance `t` by `speed * dt / arc_length`. When `t` reaches 0 or 1, look up the node at that boundary, find the next arc specified by the route, and enter it at the matching end.

---

## Routes

A route is a pre-defined, ordered list of arc traversals built by the user:

```
struct RouteStep {
    int  arc_id;
    bool forward; // true = node_a → node_b
};

struct Route {
    std::vector<RouteStep> steps;
};
```

Consecutive steps must share a node (the end node of step N equals the start node of step N+1). The train is a cursor walking this list — no pathfinding is required at runtime.

At a junction node the train simply follows whichever arc the route specifies next.

---

## Signals

A signal is a point on an arc that carries a state:

```
enum SignalState { SIGNAL_CLEAR, SIGNAL_CAUTION, SIGNAL_STOP };

struct Signal {
    int         arc_id;
    float       t;      // position within the arc
    SignalState state;
};
```

Signals divide arcs into **blocks** — the track segment between two consecutive signals. Block occupancy is determined by checking whether any train's `(arc_id, t)` falls within the block's `t` bounds. This is arithmetic, not a spatial query.

---

## Stations

A station is a trigger point with the same structure as a signal:

```
struct Station {
    int   arc_id;
    float t;
    float dwell_time; // seconds the train waits on arrival
};
```

---

## Detection: 1D interval crossing

Each frame the train moves from `t_prev` to `t_new` on `arc_id`. A signal or station at `t_trigger` on the same arc is crossed when:

```
forward:  t_prev < t_trigger <= t_new
backward: t_new <= t_trigger <  t_prev
```

Keep signals and stations in a list sorted by `t` per arc so iteration can break early.

### Overshoot guard

At high speed or large frame deltas the train could step past a signal in one frame. When a `SIGNAL_STOP` crossing is detected, clamp `t_new = t_trigger` that frame so the train lands exactly at the signal rather than past it.

### On crossing

| Trigger  | Action |
|----------|--------|
| Signal — CLEAR / CAUTION | Adjust target speed accordingly |
| Signal — STOP | Clamp `t_new`, set train speed to 0 |
| Station | Fire dwell event; hold train for `dwell_time`, then resume |

---

## Arc transitions

When the train steps onto a new arc it resets `t` to 0 or 1 (depending on entry direction). Only signals and stations belonging to the current arc are scanned each frame — the per-arc lists stay small.

---

## Required additions to the current codebase

| What | Where | Notes |
|------|-------|-------|
| Persistent arc length cache | `TrackArc` | Add `float length` field; populate in `CommitPlacement` |
| `Signal` list | new `src/systems/signal.cpp` | One system, same Init/Update/Draw pattern |
| `Station` list | new `src/systems/station.cpp` | Same pattern |
| `TrainState` + route cursor | new `src/systems/train.cpp` | Advances `t` each frame, fires crossing events |
| Route definition | `src/state/game_state.h` or dedicated file | Serialisable list of `RouteStep` |

No changes to the node-arc graph are needed. The crossing detection is approximately 30 lines of arithmetic.
