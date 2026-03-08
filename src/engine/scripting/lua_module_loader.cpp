#include "engine/scripting/lua_module_loader.h"
#include "engine/core/diag.h"

extern "C" {
#include <lua.h>
#include <lauxlib.h>
#include <lualib.h>
}

#include <cstring>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>

namespace uz {

// ---------------------------------------------------------------------------
// Registry keys for module loader state
// ---------------------------------------------------------------------------

static const char* const kProjectRootKey = "uz_module_project_root";
static const char* const kLoadingSetKey  = "uz_module_loading_set";

// ---------------------------------------------------------------------------
// Path resolution
// ---------------------------------------------------------------------------

bool lua_module_resolve_path(const char* module_name,
                             const char* project_root,
                             char* out_path,
                             int out_size) {
    if (!module_name || !project_root || !out_path || out_size <= 0) {
        return false;
    }

    // Empty module name is invalid
    if (module_name[0] == '\0') {
        return false;
    }

    // Check for path traversal patterns in the raw module name
    // Reject if it contains ".." as a segment
    const char* p = module_name;
    while (*p) {
        if (p[0] == '.' && p[1] == '.') {
            return false;  // path traversal
        }
        ++p;
    }

    // Convert dots to slashes: "game.utils" -> "game/utils"
    char relative[512] = {};
    int ri = 0;
    for (int i = 0; module_name[i] && ri < 500; ++i) {
        if (module_name[i] == '.') {
            relative[ri++] = '/';
        } else {
            relative[ri++] = module_name[i];
        }
    }
    relative[ri] = '\0';

    // Check for slash traversal in the converted path (e.g., someone passed
    // a name with embedded slashes or other tricks)
    for (int i = 0; relative[i]; ++i) {
        if (relative[i] == '/' && relative[i + 1] == '.' &&
            relative[i + 2] == '.' && (relative[i + 3] == '/' || relative[i + 3] == '\0')) {
            return false;
        }
    }
    // Also check at the start
    if (relative[0] == '.' && relative[1] == '.' &&
        (relative[2] == '/' || relative[2] == '\0')) {
        return false;
    }

    // Reject absolute paths embedded in module name
    if (relative[0] == '/' || relative[0] == '\\') {
        return false;
    }
    // Windows drive letter
    if (ri >= 2 && relative[1] == ':') {
        return false;
    }

    // Build full path: project_root + "/" + relative + ".lua"
    int root_len = static_cast<int>(std::strlen(project_root));

    // Strip trailing slash from project_root
    while (root_len > 0 && (project_root[root_len - 1] == '/' ||
                            project_root[root_len - 1] == '\\')) {
        --root_len;
    }

    int needed = root_len + 1 + ri + 4 + 1;  // root + "/" + relative + ".lua" + '\0'
    if (needed > out_size) {
        return false;
    }

    std::memcpy(out_path, project_root, root_len);
    out_path[root_len] = '/';
    std::memcpy(out_path + root_len + 1, relative, ri);
    std::memcpy(out_path + root_len + 1 + ri, ".lua", 4);
    out_path[root_len + 1 + ri + 4] = '\0';

    return true;
}

// ---------------------------------------------------------------------------
// C function: uz_require(module_name) — the sandboxed require replacement
// ---------------------------------------------------------------------------

static int uz_require(lua_State* L) {
    const char* module_name = luaL_checkstring(L, 1);

    // 1. Check package.loaded cache first
    lua_getfield(L, LUA_REGISTRYINDEX, "_LOADED");  // LuaJIT stores loaded here
    if (lua_isnil(L, -1)) {
        lua_pop(L, 1);
        // Try package.loaded via global package table
        lua_getglobal(L, "package");
        if (lua_istable(L, -1)) {
            lua_getfield(L, -1, "loaded");
            lua_remove(L, -2);
        }
    }

    int loaded_idx = lua_gettop(L);
    if (lua_istable(L, loaded_idx)) {
        lua_getfield(L, loaded_idx, module_name);
        if (!lua_isnil(L, -1)) {
            // Module already loaded — return cached value
            lua_remove(L, loaded_idx);
            return 1;
        }
        lua_pop(L, 1);  // pop nil
    }

    // 2. Get project root from registry
    lua_getfield(L, LUA_REGISTRYINDEX, kProjectRootKey);
    const char* project_root = lua_tostring(L, -1);
    if (!project_root) {
        lua_pop(L, 1);
        if (lua_istable(L, loaded_idx)) lua_remove(L, loaded_idx);
        return luaL_error(L, "require: module system not initialized (no project root)");
    }
    lua_pop(L, 1);  // pop project_root string

    // 3. Resolve path
    char resolved[1024];
    if (!lua_module_resolve_path(module_name, project_root, resolved, sizeof(resolved))) {
        if (lua_istable(L, loaded_idx)) lua_remove(L, loaded_idx);
        return luaL_error(L, "require: path traversal denied: %s", module_name);
    }

    // 4. Check circular dependency
    lua_getfield(L, LUA_REGISTRYINDEX, kLoadingSetKey);
    int loading_idx = lua_gettop(L);
    lua_getfield(L, loading_idx, module_name);
    if (lua_toboolean(L, -1)) {
        lua_pop(L, 1);
        lua_pop(L, 1);  // loading set
        if (lua_istable(L, loaded_idx)) lua_remove(L, loaded_idx);
        return luaL_error(L, "require: circular dependency detected: %s", module_name);
    }
    lua_pop(L, 1);  // pop nil/false

    // Mark as loading
    lua_pushboolean(L, 1);
    lua_setfield(L, loading_idx, module_name);

    // 5. Read the file
    std::ifstream file(resolved);
    if (!file.is_open()) {
        // Unmark loading
        lua_pushnil(L);
        lua_setfield(L, loading_idx, module_name);
        lua_pop(L, 1);  // loading set
        if (lua_istable(L, loaded_idx)) lua_remove(L, loaded_idx);
        return luaL_error(L, "require: module not found: %s", resolved);
    }

    std::ostringstream ss;
    ss << file.rdbuf();
    std::string content = ss.str();
    file.close();

    // 6. Compile
    // Use @path as chunk name so errors show the file path
    char chunk_name[1040];
    std::snprintf(chunk_name, sizeof(chunk_name), "@%s", resolved);

    int err = luaL_loadbuffer(L, content.c_str(),
                              content.size(), chunk_name);
    if (err != 0) {
        // Compile error — get message before cleanup
        const char* errmsg = lua_tostring(L, -1);
        // Unmark loading
        lua_pushnil(L);
        lua_setfield(L, loading_idx, module_name);

        // Propagate compile error
        return lua_error(L);
    }

    // 7. Execute the chunk
    err = lua_pcall(L, 0, 1, 0);
    if (err != 0) {
        // Runtime error during module init
        const char* errmsg = lua_tostring(L, -1);
        // Unmark loading
        lua_pushnil(L);
        lua_setfield(L, loading_idx, module_name);

        return lua_error(L);
    }

    // 8. If the module returned nil, use true as the cached value
    //    (standard Lua require behavior)
    if (lua_isnil(L, -1)) {
        lua_pop(L, 1);
        lua_pushboolean(L, 1);
    }

    // 9. Cache in package.loaded
    if (lua_istable(L, loaded_idx)) {
        lua_pushvalue(L, -1);  // dup module value
        lua_setfield(L, loaded_idx, module_name);
    }

    // 10. Unmark loading
    lua_pushnil(L);
    lua_setfield(L, loading_idx, module_name);

    // Clean up: remove loading_set and loaded table from under the result
    // Stack: [loaded_tbl, loading_set, result]
    // We need to return just the result
    lua_remove(L, loading_idx);  // remove loading_set
    lua_remove(L, loaded_idx);   // remove loaded table

    return 1;
}

// ---------------------------------------------------------------------------
// Install / Invalidate
// ---------------------------------------------------------------------------

void lua_module_loader_install(lua_State* L, const char* project_root) {
    if (!L || !project_root) return;

    // Store project root in registry
    lua_pushstring(L, project_root);
    lua_setfield(L, LUA_REGISTRYINDEX, kProjectRootKey);

    // Create the loading-set table for circular dependency detection
    lua_newtable(L);
    lua_setfield(L, LUA_REGISTRYINDEX, kLoadingSetKey);

    // Ensure package.loaded exists (sandbox may have nilled package)
    // We store loaded modules in the registry's _LOADED table (standard LuaJIT location)
    lua_getfield(L, LUA_REGISTRYINDEX, "_LOADED");
    if (lua_isnil(L, -1)) {
        lua_pop(L, 1);
        lua_newtable(L);
        lua_pushvalue(L, -1);
        lua_setfield(L, LUA_REGISTRYINDEX, "_LOADED");
    }
    lua_pop(L, 1);

    // Install our safe require as the global 'require'.
    // Use rawset to bypass the sandbox global guard (__newindex on _G).
    lua_pushvalue(L, LUA_GLOBALSINDEX);   // push _G
    lua_pushstring(L, "require");
    lua_pushcfunction(L, uz_require);
    lua_rawset(L, -3);                    // _G["require"] = uz_require
    lua_pop(L, 1);                        // pop _G

    DIAG_FMT("[LuaModule] Installed module loader (root: %s)", project_root);
}

void lua_module_loader_invalidate(lua_State* L, const char* module_name) {
    if (!L || !module_name) return;

    lua_getfield(L, LUA_REGISTRYINDEX, "_LOADED");
    if (lua_istable(L, -1)) {
        lua_pushnil(L);
        lua_setfield(L, -2, module_name);
    }
    lua_pop(L, 1);

    DIAG_FMT("[LuaModule] Invalidated: %s", module_name);
}

} // namespace uz
