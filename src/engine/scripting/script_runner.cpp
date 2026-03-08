#include "engine/scripting/script_runner.h"
#include "engine/scripting/script_instance.h"
#include "engine/scripting/lua_script_instance.h"
#include "engine/scripting/lua_coroutine_scheduler.h"
#include "engine/scene/scene.h"
#include "engine/scene/scene_node.h"
#include "engine/scene/component_data.h"
#include "engine/scene/component_store.h"
#include "engine/core/diag.h"

extern "C" {
#include <lua.h>
#include <lauxlib.h>
}

#include <algorithm>
#include <cstring>
#include <string>

namespace uz {

ScriptRunner::~ScriptRunner() {
    shutdown();
}

bool ScriptRunner::init() {
    if (initialized_) return true;
    if (!vm_.init()) return false;

    // Initialize coroutine scheduler and register uz.coroutine.* API
    scheduler_.init(vm_.L());
    lua_getglobal(vm_.L(), "uz");
    scheduler_.register_lua_api(vm_.L());
    lua_pop(vm_.L(), 1);  // pop uz table

    initialized_ = true;
    DIAG_FMT("[ScriptRunner] Initialized");
    return true;
}

void ScriptRunner::shutdown() {
    scheduler_.cancel_all();
    clear();
    scheduler_.shutdown();
    vm_.shutdown();
    initialized_ = false;
}

void ScriptRunner::set_scene(Scene* scene) {
    vm_.set_scene(scene);
}

void ScriptRunner::set_scene_params(SceneParams* params) {
    vm_.set_scene_params(params);
}

void ScriptRunner::tick(float delta_time, float total_time) {
    if (!initialized_) return;

    vm_.set_frame_time(delta_time, total_time);

    for (auto& script : scripts_) {
        // Set current node_id so uz.coroutine.spawn knows which node is active
        lua_pushinteger(vm_.L(), static_cast<lua_Integer>(script->node_id));
        lua_setfield(vm_.L(), LUA_REGISTRYINDEX, "uz_current_node_id");

        script->on_start();   // no-op after first call
        script->on_update(delta_time);
    }

    // Reset node_id
    lua_pushnil(vm_.L());
    lua_setfield(vm_.L(), LUA_REGISTRYINDEX, "uz_current_node_id");

    // Drain coroutines after all on_update calls
    scheduler_.drain(delta_time);
}

bool ScriptRunner::attach_script(const char* path, uint32_t node_id) {
    if (!initialized_) return false;

    auto instance = std::make_unique<LuaScriptInstance>(&vm_);
    if (!instance->load(path, node_id)) {
        return false;
    }

    scripts_.push_back(std::move(instance));
    DIAG_FMT("[ScriptRunner] Attached '%s' to node %u (total: %u)",
             path, node_id, static_cast<uint32_t>(scripts_.size()));
    return true;
}

void ScriptRunner::detach_scripts(uint32_t node_id) {
    scripts_.erase(
        std::remove_if(scripts_.begin(), scripts_.end(),
            [node_id](const std::unique_ptr<ScriptInstance>& s) {
                return s->node_id == node_id;
            }),
        scripts_.end()
    );
}

void ScriptRunner::clear() {
    scripts_.clear();
}

uint32_t ScriptRunner::find_nodes_by_script_path(const char* path,
                                                  uint32_t* out_node_ids,
                                                  uint32_t max_out) const {
    if (!path || !out_node_ids || max_out == 0) return 0;
    uint32_t count = 0;
    for (const auto& s : scripts_) {
        if (std::strcmp(s->script_path(), path) == 0) {
            out_node_ids[count++] = s->node_id;
            if (count >= max_out) break;
        }
    }
    return count;
}

void ScriptRunner::scan_scene() {
    Scene* scene = vm_.scene();
    if (!scene) return;

    // Derive project base directory from scene source path (same as renderer).
    std::string base_dir;
    if (scene->source_path[0] != '\0') {
        std::string sp(scene->source_path);
        for (auto& c : sp) if (c == '\\') c = '/';
        auto s1 = sp.rfind('/');
        if (s1 != std::string::npos) {
            std::string scene_dir = sp.substr(0, s1);
            auto s2 = scene_dir.rfind('/');
            if (s2 != std::string::npos) {
                std::string parent = scene_dir.substr(0, s2);
                auto s3 = parent.rfind('/');
                if (s3 != std::string::npos) {
                    base_dir = parent.substr(0, s3 + 1);
                }
            }
        }
    }

    for (uint32_t i = 1; i < scene->next_id; ++i) {
        const SceneNode* node = scene_get_node(scene, i);
        if (!node || !node->alive) continue;

        const auto* sd = component_get<ScriptData>(&scene->components, i);
        if (!sd || sd->script_path[0] == '\0') continue;

        // Check if already attached
        bool already = false;
        for (const auto& s : scripts_) {
            if (s->node_id == i && std::strcmp(s->script_path(), sd->script_path) == 0) {
                already = true;
                break;
            }
        }
        if (already) continue;

        // Resolve relative paths against the project base directory.
        std::string path(sd->script_path);
        if (!path.empty() && path[1] != ':' && path[0] != '/') {
            path = base_dir + path;
        }
        attach_script(path.c_str(), i);
    }
}

} // namespace uz
