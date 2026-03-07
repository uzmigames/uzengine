#pragma once

// script_runner.h -- Manages all active script instances and ticks them each frame.
//
// Usage from main loop:
//   ScriptRunner runner;
//   runner.init();
//   runner.set_scene(&scene);
//   runner.set_scene_params(&params);
//   // each frame:
//   runner.tick(delta_time, total_time);

#include "engine/scripting/lua_vm.h"
#include "engine/scripting/script_instance.h"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace uz {
struct Scene;
struct SceneParams;

class ScriptRunner {
public:
    ScriptRunner() = default;
    ~ScriptRunner();

    /// Initialize the Lua VM. Call once at startup.
    bool init();

    /// Shut down all scripts and the Lua VM.
    void shutdown();

    /// Set the active scene (for scanning ScriptData components).
    void set_scene(Scene* scene);

    /// Set scene params (for camera control from scripts).
    void set_scene_params(SceneParams* params);

    /// Tick all active scripts. Call once per frame before renderer.update().
    void tick(float delta_time, float total_time);

    /// Load a script from a file and attach it to a node.
    /// Returns true if the script was loaded successfully.
    bool attach_script(const char* path, uint32_t node_id);

    /// Remove all scripts attached to a node.
    void detach_scripts(uint32_t node_id);

    /// Remove all scripts.
    void clear();

    /// Scan scene for ScriptData components and auto-attach scripts.
    void scan_scene();

    /// Number of active script instances.
    uint32_t script_count() const { return static_cast<uint32_t>(scripts_.size()); }

private:
    LuaVM vm_;
    std::vector<std::unique_ptr<ScriptInstance>> scripts_;
    bool initialized_ = false;
};

} // namespace uz
