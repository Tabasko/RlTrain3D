#pragma once
#include <algorithm>
#include <functional>
#include <vector>
#include <queue>
#include <unordered_map>
#include <string>
#include <variant>

/**
Example Usage:
--------------

EventBus bus;

void setup() {
    bus.subscribe(_EventType::TRAIN_ARRIVED_STATION, [](const _Event& e){
        std::string station = std::get<std::string>(e.params.at("stationName"));
        int track = std::get<int>(e.params.at("track"));
        std::cout << "Train arrived at " << station << " on track " << track << "\n";
    });
}

void update() {
    // Example: emit event when key pressed
    if (IsKeyPressed(KEY_SPACE)) {
        Event e;
        e.type = EventType::TRAIN_ARRIVED_STATION;
        e.params["stationName"] = std::string("Düsseldorf Hbf");
        e.params["track"] = 5;

        bus.emit(e);
    }
}

 *
 */

enum class _EventType {
  TRAIN_ARRIVED_STATION,
  TRAIN_PASSES_SIGNAL,
  TRAIN_DEPARTED,
  CUSTOM
};

using _EventValue = std::variant<int, float, bool, std::string>;

struct _Event {
    _EventType type;
    std::unordered_map<std::string, _EventValue> params;
};

class EventBusSystem {
public:
    using Callback = std::function<void(const _Event&)>;
    
    struct Listener {
        int id;
        Callback cb;
    };

    int subscribe(_EventType type, Callback cb) {
        int id = nextId++;
        listeners[type].push_back({id, cb});
        return id;
    }

    void unsubscribe(int id) {
        for (auto& [type, vec] : listeners) {
            vec.erase(
                std::remove_if(vec.begin(), vec.end(),
                    [&](const Listener& l){ return l.id == id; }),
                vec.end()
            );
        }
    }

    void emit(const _Event& e) {
        eventQueue.push(e);  // queued for next frame
    }

    void dispatch() {
        while (!eventQueue.empty()) {
            _Event e = eventQueue.front();
            eventQueue.pop();

            auto it = listeners.find(e.type);
            if (it != listeners.end()) {
                for (auto& listener : it->second) {
                    listener.cb(e);
                }
            }
        }
    }

private:
    int nextId = 1;
    std::unordered_map<_EventType, std::vector<Listener>> listeners;
    std::queue<_Event> eventQueue;
};
