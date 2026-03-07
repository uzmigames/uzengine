#pragma once

// script_instance.h -- Abstract interface for script lifecycle.
//
// Both LuaScriptInstance and (future) CppScriptInstance implement this.
// The ScriptRunner calls these methods each frame.

#include <cstdint>

namespace uz {

class ScriptInstance {
public:
    virtual ~ScriptInstance() = default;

    /// Called once when the script is first attached to a node.
    virtual void on_start() = 0;

    /// Called every frame with delta time in seconds.
    virtual void on_update(float dt) = 0;

    /// Called when the script is detached or the node is destroyed.
    virtual void on_stop() = 0;

    /// The node ID this script is attached to.
    uint32_t node_id = 0;

    /// Path to the script file (for hot-reload identification).
    const char* script_path() const { return script_path_; }

protected:
    char script_path_[512] = "";
};

} // namespace uz
