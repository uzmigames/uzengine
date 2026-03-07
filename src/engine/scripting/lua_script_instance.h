#pragma once

// lua_script_instance.h -- Lua implementation of ScriptInstance.
//
// Loads a .lua file into a per-script Lua environment (isolated table),
// then calls on_start/on_update/on_stop functions if they exist.

#include "engine/scripting/script_instance.h"

struct lua_State;

namespace uz {

class LuaVM;

class LuaScriptInstance : public ScriptInstance {
public:
    explicit LuaScriptInstance(LuaVM* vm);
    ~LuaScriptInstance() override;

    /// Load the script file. Returns false if file not found or syntax error.
    bool load(const char* path, uint32_t node_id);

    // ScriptInstance interface
    void on_start() override;
    void on_update(float dt) override;
    void on_stop() override;

private:
    /// Call a function from the script environment. Returns false on error.
    bool call_func(const char* name, int nargs = 0);

    LuaVM*   vm_    = nullptr;
    int      env_ref_ = -1;  // Lua registry reference to this script's env table
    bool     has_start_  = false;
    bool     has_update_ = false;
    bool     has_stop_   = false;
    bool     started_    = false;
};

} // namespace uz
