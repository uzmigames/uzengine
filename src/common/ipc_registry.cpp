// ---------------------------------------------------------------------------
// ipc_registry.cpp -- IpcRegistry implementation.
// ---------------------------------------------------------------------------

#include "ipc_registry.h"

namespace uz {

void IpcRegistry::add(const std::string& name, Handler handler) {
    handlers_[name] = std::move(handler);
}

nlohmann::json IpcRegistry::call(const std::string& name,
                                  const nlohmann::json& args) const {
    const auto it = handlers_.find(name);
    if (it == handlers_.end()) {
        return {{"ok", false}, {"error", "function not found: " + name}};
    }

    return it->second(args);
}

bool IpcRegistry::has(const std::string& name) const {
    return handlers_.find(name) != handlers_.end();
}

} // namespace uz
