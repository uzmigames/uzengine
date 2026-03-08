#pragma once

// lua_module_loader.h -- Sandbox-safe module loading for Lua scripts.
//
// Provides a restricted `require()` that:
//   - Resolves dot-separated module names to .lua files under project root
//   - Rejects path traversal (no ..)
//   - Caches loaded modules in package.loaded
//   - Detects circular dependencies
//
// Usage:
//   lua_module_loader_install(L, "/path/to/project");
//   // Now Lua scripts can: local M = require("game.utils")

struct lua_State;

namespace uz {

/// Install the sandbox-safe require into a Lua state.
/// Replaces the default require with one that only loads .lua files
/// from under `project_root`. Must be called after luaL_openlibs + sandbox.
///
/// @param L            Lua state (must have package table available or we recreate it)
/// @param project_root Absolute path to the project root directory (forward slashes)
void lua_module_loader_install(lua_State* L, const char* project_root);

/// Invalidate a cached module so the next require() re-loads it.
/// @param module_name  Dot-separated module name (e.g. "game.utils")
void lua_module_loader_invalidate(lua_State* L, const char* module_name);

/// Resolve a dot-separated module name to an absolute filesystem path.
/// Returns empty string on path traversal or invalid input.
/// Exported for testing.
///
/// @param module_name  e.g. "game.utils.math_utils"
/// @param project_root e.g. "/project"
/// @param out_path     Buffer to receive the resolved path
/// @param out_size     Size of out_path buffer
/// @return true if resolution succeeded, false on error
bool lua_module_resolve_path(const char* module_name,
                             const char* project_root,
                             char* out_path,
                             int out_size);

} // namespace uz
