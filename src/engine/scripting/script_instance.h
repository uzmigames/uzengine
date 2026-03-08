#pragma once

// script_instance.h -- Abstract interface for script lifecycle.
//
// Both LuaScriptInstance and (future) CppScriptInstance implement this.
// The ScriptRunner calls these methods each frame.

#include <cstdint>
#include <string>

namespace uz {

/// Script execution state.
enum class ScriptState : uint8_t {
    OK      = 0,    // Running normally
    ERRORED = 1,    // Script hit an error; skipped until hot-reload
};

/// Per-script error information.
struct ScriptErrorInfo {
    std::string message;      // Error message from lua_tostring
    std::string file;         // Source file (extracted from error message)
    int         line = 0;     // Line number (extracted from error message)
    bool        is_timeout = false;
};

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

    /// Current execution state.
    ScriptState state() const { return state_; }

    /// Error information (valid when state == ERRORED).
    const ScriptErrorInfo& error_info() const { return error_info_; }

    /// Mark as errored with the given info.
    void set_error(const ScriptErrorInfo& info) {
        state_ = ScriptState::ERRORED;
        error_info_ = info;
    }

    /// Clear error state (e.g., after hot-reload).
    void clear_error() {
        state_ = ScriptState::OK;
        error_info_ = {};
    }

protected:
    char            script_path_[512] = "";
    ScriptState     state_ = ScriptState::OK;
    ScriptErrorInfo error_info_;
};

} // namespace uz
