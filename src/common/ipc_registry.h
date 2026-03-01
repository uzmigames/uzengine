#pragma once

// ---------------------------------------------------------------------------
// ipc_registry.h -- Registry of named IPC handler functions.
//
// Each handler receives a JSON "args" object and returns a JSON result.
// The registry is populated at startup and queried by the IPC poller when
// an external client writes a command to command.json.
// ---------------------------------------------------------------------------

#include <functional>
#include <string>
#include <unordered_map>

#include <nlohmann/json.hpp>

namespace uz {

class IpcRegistry {
public:
    using Handler = std::function<nlohmann::json(const nlohmann::json&)>;

    // Register a handler under the given function name (e.g. "scene.setRotation").
    void add(const std::string& name, Handler handler);

    // Invoke the handler for |name| with |args|.  Returns the handler's result
    // JSON, or an error object if the function is not registered.
    [[nodiscard]] nlohmann::json call(const std::string& name,
                                       const nlohmann::json& args) const;

    // Returns true if a handler is registered for |name|.
    [[nodiscard]] bool has(const std::string& name) const;

private:
    std::unordered_map<std::string, Handler> handlers_;
};

} // namespace uz
