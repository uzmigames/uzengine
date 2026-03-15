#include "lua_input_api.h"

#include "log.h"
/// Platform input.h is added to the include path by CMake per-platform
/// (platform/windows/ on WIN32, platform/linux/ on Linux, etc.),
/// so we include it by filename only.
#include "input.h"

extern "C" {
#include <lua.h>
#include <lualib.h>
#include <lauxlib.h>
}

#include <cstring>

// Scripting log category is defined in lua_vm.cpp — only declare here.
UZ_DECLARE_LOG_CATEGORY(Scripting);

// ─────────────────────────────────────────────────────────────────────────────
// Registry key for InputSystem pointer
// ─────────────────────────────────────────────────────────────────────────────

namespace {
    constexpr const char* kInputSystemKey = "uz_input_system";
} // anonymous namespace

namespace uz {

// ─────────────────────────────────────────────────────────────────────────────
// Internal helpers
// ─────────────────────────────────────────────────────────────────────────────

/// Retrieves the InputSystem* from the Lua registry. Returns null if not set.
[[nodiscard]] static InputSystem* GetInputSystem(lua_State* L) {
    lua_getfield(L, LUA_REGISTRYINDEX, kInputSystemKey);
    void* p = lua_touserdata(L, -1);
    lua_pop(L, 1);
    return static_cast<InputSystem*>(p);
}

/// Maps a Lua string key name to a `Key` enum value.
///
/// Returns `Key::Unknown` for unrecognized names.
/// Logs a Debug message on unrecognized input so script authors get feedback.
[[nodiscard]] static Key KeyFromString(const char* name) noexcept {
    if (!name || name[0] == '\0') return Key::Unknown;

    // Single uppercase letter.
    if (name[1] == '\0' && name[0] >= 'A' && name[0] <= 'Z') {
        return static_cast<Key>(name[0]);
    }

    // Single digit.
    if (name[1] == '\0' && name[0] >= '0' && name[0] <= '9') {
        return static_cast<Key>(name[0]);
    }

    // Special keys (case-sensitive matching to avoid locale issues).
    if (std::strcmp(name, "Space")    == 0) return Key::Space;
    if (std::strcmp(name, "Escape")   == 0) return Key::Escape;
    if (std::strcmp(name, "Enter")    == 0) return Key::Enter;
    if (std::strcmp(name, "Tab")      == 0) return Key::Tab;
    if (std::strcmp(name, "Backspace")== 0) return Key::Backspace;
    if (std::strcmp(name, "Delete")   == 0) return Key::Delete;
    if (std::strcmp(name, "Insert")   == 0) return Key::Insert;
    if (std::strcmp(name, "Home")     == 0) return Key::Home;
    if (std::strcmp(name, "End")      == 0) return Key::End;
    if (std::strcmp(name, "PageUp")   == 0) return Key::PageUp;
    if (std::strcmp(name, "PageDown") == 0) return Key::PageDown;

    // Arrow keys.
    if (std::strcmp(name, "Left")  == 0) return Key::Left;
    if (std::strcmp(name, "Right") == 0) return Key::Right;
    if (std::strcmp(name, "Up")    == 0) return Key::Up;
    if (std::strcmp(name, "Down")  == 0) return Key::Down;

    // Modifiers — map to left-hand variant for Lua API consistency.
    if (std::strcmp(name, "Shift") == 0) return Key::LeftShift;
    if (std::strcmp(name, "Ctrl")  == 0) return Key::LeftCtrl;
    if (std::strcmp(name, "Alt")   == 0) return Key::LeftAlt;

    // Function keys F1..F12.
    if (name[0] == 'F' && name[1] != '\0' && name[2] == '\0') {
        const char digit = name[1];
        if (digit >= '1' && digit <= '9') {
            return static_cast<Key>(static_cast<uint16_t>(Key::F1) + (digit - '1'));
        }
    }
    if (std::strcmp(name, "F10") == 0) return Key::F10;
    if (std::strcmp(name, "F11") == 0) return Key::F11;
    if (std::strcmp(name, "F12") == 0) return Key::F12;

    return Key::Unknown;
}

/// Maps a Lua mouse button name to a `MouseButton` enum value.
///
/// Returns `MouseButton::Count` for unrecognized names (used as "invalid" sentinel).
[[nodiscard]] static MouseButton MouseButtonFromString(const char* name) noexcept {
    if (!name) return MouseButton::Count;
    if (std::strcmp(name, "Left")   == 0) return MouseButton::Left;
    if (std::strcmp(name, "Right")  == 0) return MouseButton::Right;
    if (std::strcmp(name, "Middle") == 0) return MouseButton::Middle;
    return MouseButton::Count;
}

// ─────────────────────────────────────────────────────────────────────────────
// Lua C functions — uz.input.*
// ─────────────────────────────────────────────────────────────────────────────

/// uz.input.is_key_down(key) -> bool
///
/// Returns true while the named key is held. Returns false if the InputSystem
/// is not set or the key name is unrecognized.
static int InputIsKeyDown(lua_State* L) {
    InputSystem* input = GetInputSystem(L);
    if (!input) {
        lua_pushboolean(L, 0);
        return 1;
    }

    const char* name = luaL_checkstring(L, 1);
    const Key key = KeyFromString(name);
    if (key == Key::Unknown) {
        UZ_LOG(Scripting, Debug,
            "uz.input.is_key_down: unrecognized key name '{}'", name);
        lua_pushboolean(L, 0);
        return 1;
    }

    lua_pushboolean(L, input->IsKeyDown(key) ? 1 : 0);
    return 1;
}

/// uz.input.is_key_just_pressed(key) -> bool
///
/// Returns true only on the single frame the key transitions from up to down.
static int InputIsKeyJustPressed(lua_State* L) {
    InputSystem* input = GetInputSystem(L);
    if (!input) {
        lua_pushboolean(L, 0);
        return 1;
    }

    const char* name = luaL_checkstring(L, 1);
    const Key key = KeyFromString(name);
    if (key == Key::Unknown) {
        UZ_LOG(Scripting, Debug,
            "uz.input.is_key_just_pressed: unrecognized key name '{}'", name);
        lua_pushboolean(L, 0);
        return 1;
    }

    lua_pushboolean(L, input->IsKeyJustPressed(key) ? 1 : 0);
    return 1;
}

/// uz.input.get_mouse_delta() -> dx, dy
///
/// Returns the mouse movement in pixels since the last frame.
/// Returns 0, 0 if the InputSystem is not set.
static int InputGetMouseDelta(lua_State* L) {
    InputSystem* input = GetInputSystem(L);
    if (!input) {
        lua_pushnumber(L, 0.0);
        lua_pushnumber(L, 0.0);
        return 2;
    }
    lua_pushnumber(L, static_cast<double>(input->GetMouseDeltaX()));
    lua_pushnumber(L, static_cast<double>(input->GetMouseDeltaY()));
    return 2;
}

/// uz.input.is_mouse_button_down(button) -> bool
///
/// Returns true while the named mouse button is held.
/// Valid button names: `"Left"`, `"Right"`, `"Middle"`.
static int InputIsMouseButtonDown(lua_State* L) {
    InputSystem* input = GetInputSystem(L);
    if (!input) {
        lua_pushboolean(L, 0);
        return 1;
    }

    const char* name = luaL_checkstring(L, 1);
    const MouseButton btn = MouseButtonFromString(name);
    if (btn == MouseButton::Count) {
        UZ_LOG(Scripting, Debug,
            "uz.input.is_mouse_button_down: unrecognized button name '{}' "
            "(valid: \"Left\", \"Right\", \"Middle\")", name);
        lua_pushboolean(L, 0);
        return 1;
    }

    lua_pushboolean(L, input->IsMouseDown(btn) ? 1 : 0);
    return 1;
}

/// uz.input.is_key_just_released(key) -> bool
///
/// Returns true only on the single frame the key transitions from down to up.
/// Returns false if the InputSystem is not set or the key name is unrecognized.
static int InputIsKeyJustReleased(lua_State* L) {
    InputSystem* input = GetInputSystem(L);
    if (!input) {
        lua_pushboolean(L, 0);
        return 1;
    }

    const char* name = luaL_checkstring(L, 1);
    const Key key = KeyFromString(name);
    if (key == Key::Unknown) {
        UZ_LOG(Scripting, Debug,
            "uz.input.is_key_just_released: unrecognized key name '{}'", name);
        lua_pushboolean(L, 0);
        return 1;
    }

    lua_pushboolean(L, input->IsKeyJustReleased(key) ? 1 : 0);
    return 1;
}

/// uz.input.get_mouse_position() -> x, y
///
/// Returns the mouse cursor position in window-relative pixels.
/// Returns 0, 0 if the InputSystem is not set.
static int InputGetMousePosition(lua_State* L) {
    InputSystem* input = GetInputSystem(L);
    if (!input) {
        lua_pushnumber(L, 0.0);
        lua_pushnumber(L, 0.0);
        return 2;
    }
    lua_pushnumber(L, static_cast<double>(input->GetMouseX()));
    lua_pushnumber(L, static_cast<double>(input->GetMouseY()));
    return 2;
}

/// uz.input.get_scroll_delta() -> delta, 0
///
/// Returns the accumulated scroll wheel delta since the last frame.
/// Positive values indicate scroll up; negative indicate scroll down.
/// The second return value is always 0 (horizontal scroll is not supported).
/// Returns 0, 0 if the InputSystem is not set.
static int InputGetScrollDelta(lua_State* L) {
    InputSystem* input = GetInputSystem(L);
    if (!input) {
        lua_pushnumber(L, 0.0);
        lua_pushnumber(L, 0.0);
        return 2;
    }
    lua_pushnumber(L, static_cast<double>(input->GetScrollDelta()));
    lua_pushnumber(L, 0.0); // Horizontal scroll not supported by InputSystem
    return 2;
}

/// uz.input.set_mouse_capture(enabled) — no-op from Lua
///
/// Mouse capture requires a platform window handle (HWND on Windows) which is
/// not available in the Lua sandbox. This function logs a warning and returns
/// immediately. Use the C++ layer (`InputSystem::SetMouseCapture`) directly from
/// engine code when capture state needs to change.
static int InputSetMouseCapture(lua_State* L) {
    // Intentionally unused parameter — document it exists but cannot be acted on.
    (void)lua_toboolean(L, 1);
    UZ_LOG(Scripting, Warning,
        "uz.input.set_mouse_capture: cannot set capture from Lua "
        "(requires platform HWND — call from C++ engine code instead)");
    return 0;
}

/// uz.input.is_mouse_captured() -> bool
///
/// Returns true if mouse capture is currently active (cursor confined to window).
/// Returns false if the InputSystem is not set.
static int InputIsMouseCaptured(lua_State* L) {
    InputSystem* input = GetInputSystem(L);
    if (!input) {
        lua_pushboolean(L, 0);
        return 1;
    }
    lua_pushboolean(L, input->IsMouseCaptured() ? 1 : 0);
    return 1;
}

/// uz.input.set_cursor_visible(enabled) — sets cursor visibility
///
/// Shows or hides the OS mouse cursor. The visibility state is applied the
/// next time the WM_SETCURSOR message fires (handled by the platform layer).
///
/// # Arguments
/// * `enabled` — `true` to show the cursor, `false` to hide it.
static int InputSetCursorVisible(lua_State* L) {
    InputSystem* input = GetInputSystem(L);
    if (!input) {
        return 0;
    }
    const bool visible = lua_toboolean(L, 1) != 0;
    input->SetCursorVisible(visible);
    return 0;
}

/// uz.input.is_cursor_visible() -> bool
///
/// Returns true if the cursor is configured to be visible.
/// Returns false if the InputSystem is not set.
static int InputIsCursorVisible(lua_State* L) {
    InputSystem* input = GetInputSystem(L);
    if (!input) {
        lua_pushboolean(L, 1); // Default: visible
        return 1;
    }
    lua_pushboolean(L, input->IsCursorVisible() ? 1 : 0);
    return 1;
}

/// uz.input.is_gamepad_connected(index) -> bool
///
/// Returns true if the gamepad at the given player index (0-based) is connected.
/// Returns false if the InputSystem is not set or the index is out of range [0, 3].
static int InputIsGamepadConnected(lua_State* L) {
    InputSystem* input = GetInputSystem(L);
    if (!input) {
        lua_pushboolean(L, 0);
        return 1;
    }
    const int index = static_cast<int>(luaL_checkinteger(L, 1));
    lua_pushboolean(L, input->IsGamepadConnected(index) ? 1 : 0);
    return 1;
}

/// Maps a Lua axis name string to a `GamepadAxis` enum value.
///
/// Returns `GamepadAxis::Count` for unrecognized names (used as "invalid" sentinel).
/// Valid names: `"LeftX"`, `"LeftY"`, `"RightX"`, `"RightY"`,
///              `"LeftTrigger"`, `"RightTrigger"`.
[[nodiscard]] static GamepadAxis GamepadAxisFromString(const char* name) noexcept {
    if (!name) return GamepadAxis::Count;
    if (std::strcmp(name, "LeftX")         == 0) return GamepadAxis::LeftX;
    if (std::strcmp(name, "LeftY")         == 0) return GamepadAxis::LeftY;
    if (std::strcmp(name, "RightX")        == 0) return GamepadAxis::RightX;
    if (std::strcmp(name, "RightY")        == 0) return GamepadAxis::RightY;
    if (std::strcmp(name, "LeftTrigger")   == 0) return GamepadAxis::LeftTrigger;
    if (std::strcmp(name, "RightTrigger")  == 0) return GamepadAxis::RightTrigger;
    return GamepadAxis::Count;
}

/// Maps a Lua button name string to a `GamepadButton` enum value.
///
/// Returns `GamepadButton::Count` for unrecognized names (used as "invalid" sentinel).
/// Valid names: `"A"`, `"B"`, `"X"`, `"Y"`, `"LB"`, `"RB"`, `"Start"`, `"Back"`,
///              `"LeftStick"`, `"RightStick"`, `"DPadUp"`, `"DPadDown"`,
///              `"DPadLeft"`, `"DPadRight"`.
[[nodiscard]] static GamepadButton GamepadButtonFromString(const char* name) noexcept {
    if (!name) return GamepadButton::Count;
    if (std::strcmp(name, "A")          == 0) return GamepadButton::A;
    if (std::strcmp(name, "B")          == 0) return GamepadButton::B;
    if (std::strcmp(name, "X")          == 0) return GamepadButton::X;
    if (std::strcmp(name, "Y")          == 0) return GamepadButton::Y;
    if (std::strcmp(name, "LB")         == 0) return GamepadButton::LB;
    if (std::strcmp(name, "RB")         == 0) return GamepadButton::RB;
    if (std::strcmp(name, "Start")      == 0) return GamepadButton::Start;
    if (std::strcmp(name, "Back")       == 0) return GamepadButton::Back;
    if (std::strcmp(name, "LeftStick")  == 0) return GamepadButton::LThumb;
    if (std::strcmp(name, "RightStick") == 0) return GamepadButton::RThumb;
    if (std::strcmp(name, "DPadUp")     == 0) return GamepadButton::DPadUp;
    if (std::strcmp(name, "DPadDown")   == 0) return GamepadButton::DPadDown;
    if (std::strcmp(name, "DPadLeft")   == 0) return GamepadButton::DPadLeft;
    if (std::strcmp(name, "DPadRight")  == 0) return GamepadButton::DPadRight;
    return GamepadButton::Count;
}

/// uz.input.get_gamepad_axis(index, axis_name) -> number
///
/// Returns the value of a gamepad axis with deadzone applied.
/// Thumbstick axes return values in the range -1.0 to 1.0.
/// Trigger axes return values in the range 0.0 to 1.0.
///
/// # Arguments
/// * `index` — Player index (0..3, 0-based).
/// * `axis_name` — One of: `"LeftX"`, `"LeftY"`, `"RightX"`, `"RightY"`,
///                          `"LeftTrigger"`, `"RightTrigger"`.
///
/// Returns 0.0 if the InputSystem is not set, the index is out of range,
/// the gamepad is disconnected, or the axis name is unrecognized.
static int InputGetGamepadAxis(lua_State* L) {
    InputSystem* input = GetInputSystem(L);
    if (!input) {
        lua_pushnumber(L, 0.0);
        return 1;
    }

    const int index = static_cast<int>(luaL_checkinteger(L, 1));
    const char* axisName = luaL_checkstring(L, 2);
    const GamepadAxis axis = GamepadAxisFromString(axisName);
    if (axis == GamepadAxis::Count) {
        UZ_LOG(Scripting, Debug,
            "uz.input.get_gamepad_axis: unrecognized axis name '{}' "
            "(valid: \"LeftX\", \"LeftY\", \"RightX\", \"RightY\", "
            "\"LeftTrigger\", \"RightTrigger\")", axisName);
        lua_pushnumber(L, 0.0);
        return 1;
    }

    lua_pushnumber(L, static_cast<double>(input->GetGamepadAxis(index, axis)));
    return 1;
}

/// uz.input.is_gamepad_button_down(index, button_name) -> bool
///
/// Returns true while the named gamepad button is held on the specified controller.
///
/// # Arguments
/// * `index` — Player index (0..3, 0-based).
/// * `button_name` — One of: `"A"`, `"B"`, `"X"`, `"Y"`, `"LB"`, `"RB"`,
///                   `"Start"`, `"Back"`, `"LeftStick"`, `"RightStick"`,
///                   `"DPadUp"`, `"DPadDown"`, `"DPadLeft"`, `"DPadRight"`.
///
/// Returns false if the InputSystem is not set, the index is out of range,
/// the gamepad is disconnected, or the button name is unrecognized.
static int InputIsGamepadButtonDown(lua_State* L) {
    InputSystem* input = GetInputSystem(L);
    if (!input) {
        lua_pushboolean(L, 0);
        return 1;
    }

    const int index = static_cast<int>(luaL_checkinteger(L, 1));
    const char* buttonName = luaL_checkstring(L, 2);
    const GamepadButton button = GamepadButtonFromString(buttonName);
    if (button == GamepadButton::Count) {
        UZ_LOG(Scripting, Debug,
            "uz.input.is_gamepad_button_down: unrecognized button name '{}' "
            "(valid: \"A\", \"B\", \"X\", \"Y\", \"LB\", \"RB\", \"Start\", "
            "\"Back\", \"LeftStick\", \"RightStick\", \"DPadUp\", \"DPadDown\", "
            "\"DPadLeft\", \"DPadRight\")", buttonName);
        lua_pushboolean(L, 0);
        return 1;
    }

    lua_pushboolean(L, input->IsGamepadButtonDown(index, button) ? 1 : 0);
    return 1;
}

// ─────────────────────────────────────────────────────────────────────────────
// Registration
// ─────────────────────────────────────────────────────────────────────────────

void LuaRegisterInputAPI(lua_State* L) {
    lua_getglobal(L, "uz");
    if (!lua_istable(L, -1)) {
        UZ_LOG(Scripting, Error,
            "LuaRegisterInputAPI: 'uz' global is not a table — call RegisterUzTable first");
        lua_pop(L, 1);
        return;
    }

    lua_newtable(L); // uz.input = {}

    lua_pushcfunction(L, InputIsKeyDown);
    lua_setfield(L, -2, "is_key_down");

    lua_pushcfunction(L, InputIsKeyJustPressed);
    lua_setfield(L, -2, "is_key_just_pressed");

    lua_pushcfunction(L, InputGetMouseDelta);
    lua_setfield(L, -2, "get_mouse_delta");

    lua_pushcfunction(L, InputIsMouseButtonDown);
    lua_setfield(L, -2, "is_mouse_button_down");

    lua_pushcfunction(L, InputIsKeyJustReleased);
    lua_setfield(L, -2, "is_key_just_released");

    lua_pushcfunction(L, InputGetMousePosition);
    lua_setfield(L, -2, "get_mouse_position");

    lua_pushcfunction(L, InputGetScrollDelta);
    lua_setfield(L, -2, "get_scroll_delta");

    lua_pushcfunction(L, InputSetMouseCapture);
    lua_setfield(L, -2, "set_mouse_capture");

    lua_pushcfunction(L, InputIsMouseCaptured);
    lua_setfield(L, -2, "is_mouse_captured");

    lua_pushcfunction(L, InputSetCursorVisible);
    lua_setfield(L, -2, "set_cursor_visible");

    lua_pushcfunction(L, InputIsCursorVisible);
    lua_setfield(L, -2, "is_cursor_visible");

    lua_pushcfunction(L, InputIsGamepadConnected);
    lua_setfield(L, -2, "is_gamepad_connected");

    lua_pushcfunction(L, InputGetGamepadAxis);
    lua_setfield(L, -2, "get_gamepad_axis");

    lua_pushcfunction(L, InputIsGamepadButtonDown);
    lua_setfield(L, -2, "is_gamepad_button_down");

    lua_setfield(L, -2, "input"); // uz.input = { ... }
    lua_pop(L, 1);                // pop uz table

    UZ_LOG(Scripting, Info, "uz.input API registered");
}

void LuaSetInputSystem(lua_State* L, InputSystem* input) {
    lua_pushlightuserdata(L, static_cast<void*>(input));
    lua_setfield(L, LUA_REGISTRYINDEX, kInputSystemKey);
    UZ_LOG(Scripting, Debug, "LuaSetInputSystem: input={}", static_cast<void*>(input));
}

} // namespace uz
