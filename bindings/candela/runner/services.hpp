// Engine functions of the `drift` program kept out of drift.cpp, one unit per
// area. Each `...Service` answers the functions it owns with JSON, the way
// drift.cpp's own service() does, and returns false for any other name.

#pragma once

#include <nlohmann/json.hpp>

#include <string>
#include <utility>
#include <vector>

namespace drift {
class App;
}

namespace drift::candela {

// Messages for the game's systems: {"type": struct name, "value": fields}.
// The host takes them once a frame (the engine function take_messages) and
// delivers each as a @drift::message of that struct.
inline std::vector<nlohmann::json>& outbox() {
    static std::vector<nlohmann::json> box;
    return box;
}
inline void postMessage(const char* type, nlohmann::json value) {
    outbox().push_back({{"type", type}, {"value", std::move(value)}});
}

// Data files (JSON, TOML, CSV, text) and the player's language: data.cpp.
bool dataService(App& app, const std::string& f, const nlohmann::json& a, nlohmann::json& out);

// HTTP requests and WebSockets: net.cpp. netPoll() posts what arrived since
// the last call.
bool netService(const std::string& f, const nlohmann::json& a, nlohmann::json& out);
void netPoll();

} // namespace drift::candela
