## 1. Prerequisites

- [x] 1.1 Confirm `phase1-lua-scripting` is complete — sandbox strips `require`, `LuaVM` exists
- [x] 1.2 Read LuaJIT `package.loaders` / `package.searchers` documentation
- [x] 1.3 Read current `lua_sandbox.cpp` — understand what is currently stripped

## 2. Path Resolver

- [x] 2.1 Create `src/engine/scripting/lua_module_loader.h/cpp`
- [x] 2.2 Implement `resolve_path(const char* module_name) -> std::string`:
  - Convert `"game.utils"` → `"game/utils.lua"` (dots to slashes)
  - Prepend project `res://` root: `project_root + "/" + relative_path`
  - Normalize path: collapse `..` segments, verify result starts with project root
- [x] 2.3 Implement sandbox path check: reject paths not under `project_root`; return error string for violation
- [x] 2.4 Implement path existence check: `std::filesystem::exists(resolved)` before opening
- [x] 2.5 Register `uz_module_searcher` as sole entry in `package.searchers` (index 1)

## 3. Module Loader

- [x] 3.1 Implement `uz_module_loader(lua_State*, const char* path)` — C function called by LuaJIT when module not cached
- [x] 3.2 Load file content via `std::ifstream` — stream into string buffer
- [x] 3.3 Compile with `luaL_loadbuffer` — capture compile errors
- [x] 3.4 Execute in sandboxed environment — use `LuaEnvironment::create_restricted_env()` as upvalue
- [x] 3.5 Store result in `package.loaded[module_name]` — deduplicate on subsequent requires
- [x] 3.6 Return the module value to the caller (top of stack after execution)

## 4. Sandbox Integration

- [x] 4.1 Update `lua_sandbox.cpp` — replace `require = nil` with `package.searchers = { uz_module_searcher }`
- [x] 4.2 Remove dangerous default searchers: file, C, preload (keep only `uz_module_searcher`)
- [x] 4.3 Nil out `package.loadlib` — prevent dynamic C library loading
- [x] 4.4 Nil out `package.cpath` — C library paths not allowed
- [x] 4.5 Expose `package.loaded` as read-only mirror from sandbox — scripts can check if a module is loaded

## 5. Hot-Reload Integration

- [x] 5.1 Update `LuaScriptReloader::reload(path)` — when a `.lua` file changes, nil out its entry in `package.loaded`
- [x] 5.2 After nil-out, call `on_stop` then `on_start` for all nodes that required this module
- [x] 5.3 Ensure circular dependency detection: track in-progress loads in a `loading_set`; error on cycle
- [x] 5.4 Log hot-reload module event: `[LuaModule] reloaded: res://game/utils.lua`

## 6. Error Reporting

- [x] 6.1 Compile error in required module: `lua_error` with message including file path and line number
- [x] 6.2 Runtime error during module initialization: `lua_error` propagates to requirer's `on_start`
- [x] 6.3 Path violation: `lua_error` with message `"require: path traversal denied: ../../../evil"`
- [x] 6.4 Missing file: `lua_error` with message `"require: module not found: res://game/missing.lua"`

## 7. Testing

- [x] 7.1 Unit test: `resolve_path("game.utils")` → correct filesystem path under project root
- [x] 7.2 Unit test: `resolve_path("../../etc/passwd")` → sandbox rejection error
- [x] 7.3 Unit test: second `require("game.utils")` returns cached value (no re-execution)
- [x] 7.4 Unit test: compile error in required module surfaces in requirer's error with line info
- [x] 7.5 Unit test: circular require `A requires B requires A` → error, no infinite loop
- [x] 7.6 Integration test: `require("game.math_utils")` from an `on_start` script; verify shared function callable
- [x] 7.7 Integration test: modify `math_utils.lua`, verify hot-reload invalidates cache and re-requires on next `on_start`

## 8. Documentation

- [x] 8.1 Update `docs/specs/lua-scripting.md` — add "Module System" section with `require` usage, path format, sandbox rules
- [x] 8.2 Add module example: `local MathUtils = require("game.utils.math_utils")`
- [x] 8.3 Update `docs/guides/lua-scripting-guide.md` — "Sharing Code with Modules" section
