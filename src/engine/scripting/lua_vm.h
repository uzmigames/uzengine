#pragma once

// lua_vm.h -- LuaJIT state wrapper with sandboxed environment.
//
// Owns a single lua_State and provides helpers for loading/running scripts.
// The sandbox strips dangerous globals (os.execute, io, loadfile, etc.)
// while keeping safe math/string/table libraries.
//
// Execution safety features:
//   - Custom allocator with per-VM memory cap
//   - Instruction count hook for CPU budget
//   - Global guard (__newindex on _G) for sandbox escape detection

#include "engine/scripting/script_execution_safety.h"

#include <cstddef>

struct lua_State;

namespace uz {

struct Scene;
struct SceneParams;

class LuaVM {
public:
    LuaVM();
    ~LuaVM();

    LuaVM(const LuaVM&) = delete;
    LuaVM& operator=(const LuaVM&) = delete;

    /// Initialize the Lua state with sandboxed standard libraries.
    /// Returns false on allocation failure.
    bool init();

    /// Initialize with explicit configuration.
    bool init(const ScriptingConfig& config);

    /// Shut down and free the Lua state.
    void shutdown();

    /// Set the scene pointer for Lua bindings (uz.scene.*).
    void set_scene(Scene* scene) { scene_ = scene; }

    /// Set scene params for camera control (uz.camera.*).
    void set_scene_params(SceneParams* params) { scene_params_ = params; }

    /// Set frame timing values (updated each frame by ScriptRunner).
    void set_frame_time(float dt, float total) { delta_time_ = dt; total_time_ = total; }

    /// Set the project root for the module loader.
    /// Must be called before scripts that use require().
    void set_project_root(const char* root);

    /// Invalidate a cached module so require() reloads it next time.
    void invalidate_module(const char* module_name);

    /// Raw Lua state (for LuaScriptInstance to load/call functions).
    lua_State* L() const { return L_; }

    // Accessors for bindings
    Scene*       scene()        const { return scene_; }
    SceneParams* scene_params() const { return scene_params_; }
    float        delta_time()   const { return delta_time_; }
    float        total_time()   const { return total_time_; }

    /// Current scripting configuration.
    const ScriptingConfig& config() const { return config_; }

    /// Memory used by this VM's Lua allocator (bytes).
    size_t memory_used_bytes() const { return allocator_.bytes_used(); }

    /// Memory limit for this VM (bytes).
    size_t memory_limit_bytes() const { return allocator_.limit; }

    /// Reset the instruction hook with a fresh budget.
    /// Call before each on_update dispatch.
    void reset_instruction_hook();

private:
    void register_bindings();
    void apply_sandbox();

    lua_State*      L_            = nullptr;
    Scene*          scene_        = nullptr;
    SceneParams*    scene_params_ = nullptr;
    float           delta_time_   = 0.0f;
    float           total_time_   = 0.0f;
    ScriptingConfig config_;
    LuaAllocator    allocator_;
};

} // namespace uz
