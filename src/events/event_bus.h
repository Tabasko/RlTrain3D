#pragma once
#include "../types.h"
#include <vector>

// All application-wide event types.
// Add new entries here before extending EventData if a payload is needed.
typedef enum {
    EVENT_NONE = 0,
    EVENT_TOGGLE_LEFT_PANEL,
    EVENT_START_TRACK_EDIT,
    EVENT_START_JUNCTION_EDIT,
} EventType;

// Payload carrier — add field types here as new events require them.
// Only one field is active per event; which one is implied by EventType.
union EventData {
    int     i;
    float   f;
    bool    b;
    Vector2 v2;
};

struct Event {
    EventType type;
    EventData data = {};
};

// Double-buffered event bus.
//
// Events emitted during frame N land in `write` and become readable in `read`
// during frame N+1. Call swap() once at the top of the frame loop before any
// system runs, then use emit() and has() freely within the frame.
struct EventBus {
    std::vector<Event> read;  // events from last frame — consumed this frame
    std::vector<Event> write; // events queued this frame — readable next frame

    // Reserve backing storage; call once at startup.
    void init();

    // Promote write queue to read queue and clear write. Call once per frame.
    void swap();

    // Queue an event for the next frame.
    void emit(EventType type, EventData data = {});

    // Returns true if at least one event of `type` is in the read queue.
    bool has(EventType type) const;
};
