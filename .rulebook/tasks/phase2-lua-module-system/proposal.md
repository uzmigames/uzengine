# Proposal: phase1-lua-module-system

## Why

The current Lua sandbox strips `require` entirely, making it impossible for scripts to share utility code. Every script duplicates helper functions (math utilities, state machines, inventory logic). This produces maintenance overhead and inconsistency. A controlled `require` implementation — restricted to `res://` paths, sandboxed, with module caching and hot-reload — gives Lua scripts the ability to share code without opening security holes.

## What Changes

- **`src/engine/scripting/lua_module_loader.h/cpp`** — custom module loader replacing stripped `require`:
  - `res://` path resolver: maps `require("game.utils")` → `res://game/utils.lua`
  - Sandbox enforcement: only `res://` paths allowed, no absolute paths, no `..` traversal
  - Module cache: `package.loaded` table integration for deduplication
  - Hot-reload: `LuaScriptReloader` triggers `package.loaded[path] = nil` on file change, re-requires on next access
- **`src/engine/scripting/lua_sandbox.cpp`** — replace `require = nil` with controlled loader registration
- **Tests** — path resolution, sandbox enforcement, cache behavior, hot-reload

## Impact

- Affected specs: `docs/specs/lua-scripting.md` (module system section)
- Dependencies: `phase1-lua-scripting` (sandbox and VM must exist)
- Breaking change: ADDITIVE — `require` was previously nil; now works for `res://` paths
- User benefit: shared Lua libraries; no code duplication across scripts; hot-reload of module files
