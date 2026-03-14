#pragma once

/// LuaJIT virtual machine wrapper — manages Lua state lifecycle.
///
/// Provides sandboxed Lua execution with engine API bindings.
/// Uses LuaJIT 2.1 for near-native performance.
///
/// # Thread Safety
/// NOT thread-safe. Must be called from the main thread only.

struct lua_State;

namespace uz {

/// Singleton LuaJIT VM wrapper.
///
/// Owns the global lua_State, applies sandbox restrictions,
/// and registers engine API tables (uz.*, uz.scene.*, etc.).
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

private:
    static lua_State* s_state;
};

} // namespace uz
