#pragma once

// lua_script_reloader.h -- Hot-reload support for Lua scripts.
//
// Watches for file changes and triggers detach/reattach of affected scripts.
// Does NOT own a FileWatcher — the caller supplies change notifications via
// on_file_changed(). This keeps the reloader decoupled and testable.

#include <cstdint>

namespace uz {

class ScriptRunner;

class LuaScriptReloader {
public:
    LuaScriptReloader() = default;
    ~LuaScriptReloader() = default;

    LuaScriptReloader(const LuaScriptReloader&) = delete;
    LuaScriptReloader& operator=(const LuaScriptReloader&) = delete;

    /// Connect to a ScriptRunner. Must be called before on_file_changed().
    void set_script_runner(ScriptRunner* runner) { runner_ = runner; }

    /// Notify that a .lua file has been modified on disk.
    /// If any active script matches the path, it is detached and reattached.
    /// Returns the number of scripts that were reloaded.
    uint32_t on_file_changed(const char* path);

    /// Total number of successful reloads since construction.
    uint32_t reload_count() const { return reload_count_; }

private:
    ScriptRunner* runner_       = nullptr;
    uint32_t      reload_count_ = 0;
};

} // namespace uz
