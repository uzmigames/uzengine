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
