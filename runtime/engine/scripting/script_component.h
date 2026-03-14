#pragma once

/// CScript — Lua script component for attaching a .lua file to an entity.
///
/// Stores the VFS path to the script, the compiled chunk reference, a per-entity
/// environment table reference, and lifecycle state. The `ScriptSystem` manages
/// loading from VFS, executing the chunk to define lifecycle hooks, and calling
/// `on_start`, `on_update`, and `on_stop` at the appropriate times.
///
/// # Lifecycle
/// 1. User adds `CScript` with a `scriptPath` via `World::AddComponent<CScript>`.
/// 2. On the next `ScriptSystem::Update()`, the system reads the script bytes from
///    VFS, compiles them, creates an isolated environment table, and calls `on_start`.
/// 3. Every subsequent `ScriptSystem::Update()` calls `on_update(entity, dt)`.
/// 4. `ScriptSystem::Shutdown()` calls `on_stop(entity)` and releases Lua refs.
///
/// # Error Policy
/// If loading or any lifecycle call fails, the error is logged and the entity is
/// skipped — it will NOT be retried on the next frame (checked via `luaRef`).
///
/// # Isolation
/// Each `CScript` instance owns its own Lua environment table (`envRef`).
/// Global variables set inside one script do not affect any other script.

#include <string>

extern "C" {
#include <lauxlib.h>
}

namespace uz {

// ─────────────────────────────────────────────────────────────────────────────
// CScript — ECS component for attaching a Lua script to an entity
// ─────────────────────────────────────────────────────────────────────────────

/// Lua script component — attaches a .lua script to an entity.
///
/// Stores the script path, the Lua registry ref to the compiled chunk,
/// and lifecycle state. The ScriptSystem manages loading and calling
/// `on_start` / `on_update` / `on_stop`.
///
/// # Fields
/// - `scriptPath`: VFS path to the .lua file (e.g., `"scripts/rotate.lua"`).
/// - `luaRef`:     Registry ref for the compiled Lua chunk. `LUA_REFNIL` until loaded.
/// - `envRef`:     Registry ref for this entity's private environment table.
///                 Inherits `_G` via metatable `__index`.
/// - `started`:    Set to `true` after `on_start` has been called successfully.
///
/// # Load-error sentinel
/// If loading fails, `luaRef` stays `LUA_REFNIL` and `started` stays `false`.
/// The system will NOT retry loading — the entity is silently skipped.
/// This prevents log-spam from a missing/broken script on every frame.
struct CScript {
    std::string scriptPath;     ///< VFS path to the .lua file
    int luaRef   = LUA_REFNIL; ///< Lua registry ref to compiled chunk (set by ScriptSystem)
    int envRef   = LUA_REFNIL; ///< Lua registry ref to per-entity environment table
    bool started = false;      ///< True after on_start has been called
};

} // namespace uz
