#pragma once

/// LuaJIT virtual machine wrapper — manages Lua state lifecycle.
///
/// Provides sandboxed Lua execution with engine API bindings.
/// Uses LuaJIT 2.1 for near-native performance.
///
/// # Thread Safety
/// NOT thread-safe. Must be called from the main thread only.

#include <string>
#include <vector>
#include <cstdint>

struct lua_State;

namespace uz {

/// Singleton LuaJIT VM wrapper.
///
/// Owns the global lua_State, applies sandbox restrictions,
/// and registers engine API tables (uz.*, uz.scene.*, etc.).
///
/// # Script Lifecycle
/// 1. `Init()` — creates Lua state, applies sandbox, registers `uz.*` API
/// 2. `LoadScript(path)` — compiles a .lua file, returns a Lua registry ref
/// 3. `CallRef(ref, ...)` — calls a loaded function with error handling
/// 4. `UnloadScript(ref)` — releases the registry ref
/// 5. `Shutdown()` — destroys the Lua state
class LuaVM {
public:
    /// Initialize the Lua VM and register engine APIs.
    /// Must be called once during engine startup.
    /// Returns false if LuaJIT initialization fails.
    static bool Init();

    /// Shutdown the Lua VM and free all Lua resources.
    /// Safe to call even if Init() was never called.
    static void Shutdown();

    /// Get the raw lua_State pointer.
    /// Returns nullptr if not initialized.
    static lua_State* GetState();

    /// Returns true if the VM has been initialized.
    static bool IsInitialized();

    /// Update per-frame globals (uz.delta_time, uz.total_time, uz.frame_index).
    ///
    /// Must be called once per frame before any script callbacks.
    ///
    /// # Parameters
    /// - `deltaTime`: Time in seconds since last frame.
    /// - `totalTime`: Total elapsed time in seconds since engine start.
    /// - `frameIndex`: Current frame number (monotonically increasing).
    static void UpdateFrameGlobals(float deltaTime, float totalTime, uint64_t frameIndex);

    /// Load a Lua script file and return a registry reference.
    ///
    /// Reads the file as bytes, compiles with `luaL_loadbuffer`, and stores
    /// the compiled chunk in the Lua registry via `luaL_ref`.
    ///
    /// # Parameters
    /// - `scriptBytes`: Raw file content (UTF-8 Lua source).
    /// - `scriptName`: Display name for error messages (e.g., "scripts/foo.lua").
    ///
    /// # Returns
    /// - Positive registry ref on success.
    /// - `LUA_REFNIL` (-1) on compile error.
    ///
    /// # Errors
    /// Logs `Scripting::Error` with file name and Lua error message on failure.
    static int LoadScript(const std::vector<uint8_t>& scriptBytes, const std::string& scriptName);

    /// Release a previously loaded script reference.
    ///
    /// Frees the Lua registry entry. The ref becomes invalid after this call.
    ///
    /// # Parameters
    /// - `ref`: Registry reference returned by `LoadScript()`. No-op if `LUA_REFNIL`.
    static void UnloadScript(int ref);

    /// Call a Lua function with protected error handling.
    ///
    /// Pushes the function from registry ref onto the stack, calls it via
    /// `lua_pcall`, and logs any errors with file:line info.
    ///
    /// # Parameters
    /// - `ref`: Registry reference to a Lua function.
    /// - `nargs`: Number of arguments already pushed onto the stack.
    /// - `nresults`: Expected number of return values.
    /// - `source`: Human-readable source name for error messages.
    ///
    /// # Returns
    /// - `true` if the call succeeded.
    /// - `false` if a Lua error occurred (error is logged and popped from stack).
    static bool PCall(int ref, int nargs, int nresults, const char* source);

private:
    static lua_State* s_state;
};

} // namespace uz
