#pragma once

/// lua_input_api — Lua bindings for keyboard, mouse, and input state.
///
/// Registers the `uz.input` sub-table in the Lua global `uz` with functions
/// for querying keyboard key state (down/just-pressed) and mouse state
/// (delta, button down). Bridges Lua scripts to the platform `InputSystem`.
///
/// # Input System Integration
/// The `InputSystem*` pointer is injected at runtime via `LuaSetInputSystem()`
/// and stored in the Lua registry under `"uz_input_system"`. This keeps the
/// Lua C functions decoupled from any global singleton.
///
/// # Key Name Mapping
/// Keys are addressed by short string names matching the `Key` enum:
/// - Letters: `"A"` .. `"Z"` (case-sensitive, uppercase)
/// - Numbers: `"0"` .. `"9"`
/// - Special: `"Space"`, `"Escape"`, `"Enter"`, `"Tab"`, `"Backspace"`
/// - Arrows: `"Left"`, `"Right"`, `"Up"`, `"Down"`
/// - Modifiers: `"Shift"`, `"Ctrl"`, `"Alt"` (maps to left variant)
/// - Function: `"F1"` .. `"F12"`
/// - Misc: `"Delete"`, `"Insert"`, `"Home"`, `"End"`, `"PageUp"`, `"PageDown"`
///
/// # Mouse Button Names
/// - `"Left"`, `"Right"`, `"Middle"`
///
/// # Thread Safety
/// Not thread-safe. All functions must be called from the main thread.

struct lua_State;

namespace uz {

class InputSystem;

/// Registers the `uz.input` sub-table into the existing `uz` global table.
///
/// Must be called after `RegisterUzTable()` and while the Lua VM is initializing.
///
/// # Arguments
/// * `L` - Active Lua state. Must not be null.
void LuaRegisterInputAPI(lua_State* L);

/// Stores an InputSystem pointer in the Lua registry for use by input callbacks.
///
/// Must be called before any Lua script invokes `uz.input.*` functions.
/// Passing `nullptr` disables input access (all functions return false/0).
///
/// # Arguments
/// * `L` - Active Lua state. Must not be null.
/// * `input` - InputSystem instance to expose. May be null to revoke access.
void LuaSetInputSystem(lua_State* L, InputSystem* input);

} // namespace uz
