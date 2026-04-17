#pragma once

/// ScriptSystem — per-frame driver for all CScript components.
///
/// Iterates the World's CScript view each frame and manages the full script
/// lifecycle: VFS loading, Lua chunk compilation, per-entity environment
/// isolation, and calling `on_start` / `on_update` / `on_stop`.
///
/// # Usage
/// ```cpp
/// ScriptSystem::Initialize(&world, &vfs);
///
/// // game loop:
/// while (running) {
///     ScriptSystem::Update(deltaTime);
/// }
///
/// ScriptSystem::Shutdown();
/// ```
///
/// # Thread Safety
/// NOT thread-safe. Must be called from the main thread only.
///
/// # Error Policy
/// Lua errors in any lifecycle hook are caught via `lua_pcall`, logged with
/// `Scripting::Error`, and the entity is left in its current state — it will
/// NOT be retried or crash the engine.

namespace uz {

class World;
class IVFS;

// ─────────────────────────────────────────────────────────────────────────────
// ScriptSystem — drives all CScript components in the World
// ─────────────────────────────────────────────────────────────────────────────

/// Processes all CScript components in a World each frame.
///
/// Call sequence:
/// 1. `ScriptSystem::Initialize(world, vfs)` — one-time setup. Must be called
///    before the first `Update()`. Both pointers must remain valid for the
///    lifetime of the system.
/// 2. `ScriptSystem::Update(dt)` — called every frame. Loads any pending
///    scripts, calls `on_start` on newly loaded scripts, then calls `on_update`
///    on all running scripts.
/// 3. `ScriptSystem::Shutdown()` — calls `on_stop` on every running script,
///    releases all Lua registry refs, and clears the stored pointers.
class ScriptSystem {
public:
    /// Store the World and VFS for use during `Update()` and `Shutdown()`.
    ///
    /// Must be called once before the first `Update()`. The `LuaVM` must already
    /// be initialized when this is called.
    ///
    /// # Parameters
    /// - `world`: Non-owning pointer to the active World. Must not be null.
    /// - `vfs`:   Non-owning pointer to the VFS used to read script files.
    ///            Must not be null.
    ///
    /// # Side Effects
    /// Logs `Scripting::Info` confirming initialization.
    static void Initialize(World* world, IVFS* vfs);

    /// Per-frame update. Loads pending scripts and calls lifecycle hooks.
    ///
    /// For each `CScript` in the World:
    /// - If not yet loaded (`luaRef == LUA_REFNIL && !started`):
    ///   reads from VFS, compiles, creates a sandboxed environment, and calls
    ///   `on_start(entity)` if the function is defined.
    /// - If loaded and started (`started == true`):
    ///   calls `on_update(entity, dt)` if the function is defined.
    ///
    /// All Lua calls are protected. Errors are logged and the script continues
    /// to receive `on_update` calls (only the failing call is skipped).
    ///
    /// # Parameters
    /// - `deltaTime`: Seconds elapsed since the previous frame.
    ///
    /// # Side Effects
    /// Modifies `CScript::luaRef`, `CScript::envRef`, and `CScript::started`
    /// fields on newly loaded entities.
    static void Update(float deltaTime);

    /// Shutdown. Calls `on_stop` on all running scripts and releases Lua refs.
    ///
    /// For each `CScript` whose `luaRef != LUA_REFNIL`:
    /// - Calls `on_stop(entity)` if defined (protected — errors are logged).
    /// - Calls `LuaVM::UnloadScript(chunkRef)` and `luaL_unref` for envRef.
    /// - Resets `luaRef`, `envRef`, and `started` back to initial values.
    ///
    /// # Side Effects
    /// Logs `Scripting::Info` confirming shutdown. Clears stored `s_world` and
    /// `s_vfs` pointers to guard against use-after-free.
    static void Shutdown();

    /// Enables or disables script execution. When disabled (the default in
    /// editor mode), `Update()` is a no-op — no `on_start` or `on_update`
    /// calls fire, keeping gameplay scripts dormant while the user edits the
    /// scene. Set to true when entering Play Mode; set to false when leaving
    /// it.
    ///
    /// This mirrors the Unreal / Unity / Godot editor convention: BeginPlay
    /// / Start / _ready only runs inside PIE / Play / the game runtime,
    /// never while the author is arranging the scene.
    static void SetPlayMode(bool enabled);

    /// Returns whether scripts are currently active.
    [[nodiscard]] static bool IsPlayMode();

private:
    ScriptSystem() = delete;

    static World* s_world; ///< Non-owning pointer to the active World
    static IVFS*  s_vfs;   ///< Non-owning pointer to the VFS for script loading
    static bool   s_playMode; ///< When false, Update() is a no-op (editor default).
};

} // namespace uz
