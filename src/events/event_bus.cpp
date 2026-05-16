#include "event_bus.h"

static constexpr int EVENT_BUS_CAPACITY = 128;

void EventBus::init() {
    read.reserve(EVENT_BUS_CAPACITY);
    write.reserve(EVENT_BUS_CAPACITY);
}

void EventBus::swap() {
    read = write;
    write.clear();
}

void EventBus::emit(EventType type, EventData data) {
    write.push_back({ type, data });
}

bool EventBus::has(EventType type) const {
    for (const Event &e : read)
        if (e.type == type) return true;
    return false;
}
