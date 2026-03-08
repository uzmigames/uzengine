#include "engine/scripting/lua_script_reloader.h"
#include "engine/scripting/script_runner.h"
#include "engine/core/diag.h"

#include <cstring>

namespace uz {

uint32_t LuaScriptReloader::on_file_changed(const char* path) {
    if (!runner_ || !path || path[0] == '\0') return 0;

    // Find all nodes that have a script matching this path.
    constexpr uint32_t kMaxReloads = 64;
    uint32_t node_ids[kMaxReloads];
    const uint32_t count = runner_->find_nodes_by_script_path(path, node_ids, kMaxReloads);

    if (count == 0) return 0;

    // Detach and reattach each affected script.
    uint32_t reloaded = 0;
    for (uint32_t i = 0; i < count; ++i) {
        const uint32_t nid = node_ids[i];
        runner_->detach_scripts(nid);
        if (runner_->attach_script(path, nid)) {
            DIAG_FMT("[LuaReload] reloaded: %s (node %u)", path, nid);
            ++reloaded;
        } else {
            DIAG_FMT("[LuaReload] failed to reload: %s (node %u)", path, nid);
        }
    }

    reload_count_ += reloaded;
    return reloaded;
}

} // namespace uz
