#include <algorithm>
#include "event_system.h"
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
    bus.subscribe(EventType::TRAIN_ARRIVED_STATION, [](const Event& e){
        std::string station =
std::any_cast<std::string>(e.params.at("stationName")); int track =
std::any_cast<int>(e.params.at("track")); std::cout << "Train arrived at " <<
station << " on track " << track << "\n";
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

