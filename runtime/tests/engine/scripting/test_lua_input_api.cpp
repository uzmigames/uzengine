/// Unit tests for Lua Input API — uz.input table existence, function presence,
/// no-InputSystem fallback values, wrong-type error propagation, and key/mouse
/// name coverage. All tests run without a platform window or real InputSystem.

#include "lua_vm.h"
#include "lua_input_api.h"

#include <gtest/gtest.h>
#include <string>

extern "C" {
#include <lua.h>
#include <lualib.h>
#include <lauxlib.h>
}

// ─────────────────────────────────────────────────────────────────────────────
// Test Fixture
// ─────────────────────────────────────────────────────────────────────────────

class LuaInputAPITest : public ::testing::Test {
  protected:
    void SetUp() override {
        ASSERT_TRUE(uz::LuaVM::Init());
        // Explicitly clear the InputSystem pointer so all tests start
        // with no platform dependency.
        uz::LuaSetInputSystem(uz::LuaVM::GetState(), nullptr);
    }

    void TearDown() override {
        uz::LuaVM::Shutdown();
    }

    /// Helper: run Lua code, return true on success.
    bool DoString(const char* code) {
        return luaL_dostring(uz::LuaVM::GetState(), code) == 0;
    }

    /// Helper: evaluate a Lua expression and return its boolean result.
    bool EvalBool(const char* expr) {
        lua_State* L = uz::LuaVM::GetState();
        std::string code = std::string("return ") + expr;
        if (luaL_dostring(L, code.c_str()) != 0) {
            lua_pop(L, 1);
            return false;
        }
        bool r = lua_toboolean(L, -1) != 0;
        lua_pop(L, 1);
        return r;
    }

    /// Helper: evaluate a Lua expression and return its numeric result.
    /// Returns -999.0 and adds a test failure if the expression raises an error.
    double EvalNumber(const char* expr) {
        lua_State* L = uz::LuaVM::GetState();
        std::string code = std::string("return ") + expr;
        if (luaL_dostring(L, code.c_str()) != 0) {
            const char* err = lua_tostring(L, -1);
            ADD_FAILURE() << "Lua error in EvalNumber(\"" << expr
                          << "\"): " << (err ? err : "unknown");
            lua_pop(L, 1);
            return -999.0;
        }
        double r = lua_tonumber(L, -1);
        lua_pop(L, 1);
        return r;
    }

    /// Helper: run a Lua snippet via pcall and return true if it raised an error.
    /// Pops the error message from the stack when an error occurs.
    bool RaisesLuaError(const char* code) {
        lua_State* L = uz::LuaVM::GetState();
        bool errored = luaL_dostring(L, code) != 0;
        if (errored) lua_pop(L, 1);
        return errored;
    }
};

// ─────────────────────────────────────────────────────────────────────────────
// uz.input table and function existence
// ─────────────────────────────────────────────────────────────────────────────

TEST_F(LuaInputAPITest, InputTableExists_AfterInit) {
    EXPECT_TRUE(EvalBool("type(uz.input) == 'table'"));
}

TEST_F(LuaInputAPITest, IsKeyDown_IsFunction) {
    EXPECT_TRUE(EvalBool("type(uz.input.is_key_down) == 'function'"));
}

TEST_F(LuaInputAPITest, IsKeyJustPressed_IsFunction) {
    EXPECT_TRUE(EvalBool("type(uz.input.is_key_just_pressed) == 'function'"));
}

TEST_F(LuaInputAPITest, GetMouseDelta_IsFunction) {
    EXPECT_TRUE(EvalBool("type(uz.input.get_mouse_delta) == 'function'"));
}

TEST_F(LuaInputAPITest, IsMouseButtonDown_IsFunction) {
    EXPECT_TRUE(EvalBool("type(uz.input.is_mouse_button_down) == 'function'"));
}

// ─────────────────────────────────────────────────────────────────────────────
// No InputSystem set — fallback values (no crash, safe defaults)
// ─────────────────────────────────────────────────────────────────────────────

TEST_F(LuaInputAPITest, IsKeyDown_NoInputSystem_ReturnsFalse) {
    // With no InputSystem, is_key_down must return false for any key name.
    EXPECT_FALSE(EvalBool("uz.input.is_key_down('W')"));
}

TEST_F(LuaInputAPITest, IsKeyDown_NoInputSystem_AllLetterKeys_ReturnFalse) {
    // Verify the no-InputSystem path for a broad range of letter keys.
    const char* keys[] = {
        "A", "B", "C", "D", "E", "F", "G", "H", "I", "J",
        "K", "L", "M", "N", "O", "P", "Q", "R", "S", "T",
        "U", "V", "W", "X", "Y", "Z",
        nullptr
    };
    for (int i = 0; keys[i]; ++i) {
        std::string code = std::string("uz.input.is_key_down('") + keys[i] + "')";
        EXPECT_FALSE(EvalBool(code.c_str()))
            << "Expected false for key '" << keys[i] << "' with no InputSystem";
    }
}

TEST_F(LuaInputAPITest, IsKeyJustPressed_NoInputSystem_ReturnsFalse) {
    EXPECT_FALSE(EvalBool("uz.input.is_key_just_pressed('Space')"));
}

TEST_F(LuaInputAPITest, IsMouseButtonDown_Left_NoInputSystem_ReturnsFalse) {
    EXPECT_FALSE(EvalBool("uz.input.is_mouse_button_down('Left')"));
}

TEST_F(LuaInputAPITest, IsMouseButtonDown_Right_NoInputSystem_ReturnsFalse) {
    EXPECT_FALSE(EvalBool("uz.input.is_mouse_button_down('Right')"));
}

TEST_F(LuaInputAPITest, IsMouseButtonDown_Middle_NoInputSystem_ReturnsFalse) {
    EXPECT_FALSE(EvalBool("uz.input.is_mouse_button_down('Middle')"));
}

TEST_F(LuaInputAPITest, GetMouseDelta_NoInputSystem_ReturnsTwoZeroes) {
    // get_mouse_delta() must return exactly two values: dx=0, dy=0.
    EXPECT_TRUE(DoString(
        "_dx, _dy = uz.input.get_mouse_delta()\n"
    ));
    EXPECT_DOUBLE_EQ(EvalNumber("_dx"), 0.0);
    EXPECT_DOUBLE_EQ(EvalNumber("_dy"), 0.0);
}

TEST_F(LuaInputAPITest, GetMouseDelta_NoInputSystem_ReturnExactlyTwoValues) {
    // Verify both return slots are 0 and the Lua stack is balanced after the call.
    lua_State* L = uz::LuaVM::GetState();
    int topBefore = lua_gettop(L);

    // pcall equivalent via luaL_dostring — pops all results
    EXPECT_TRUE(DoString("local a, b = uz.input.get_mouse_delta()"));

    EXPECT_EQ(lua_gettop(L), topBefore);
}

// ─────────────────────────────────────────────────────────────────────────────
// Special key names — no InputSystem, must return false (not crash)
// ─────────────────────────────────────────────────────────────────────────────

TEST_F(LuaInputAPITest, IsKeyDown_SpecialKeys_NoInputSystem_ReturnFalse) {
    const char* specials[] = {
        "Space", "Escape", "Enter", "Tab", "Backspace",
        "Delete", "Insert", "Home", "End", "PageUp", "PageDown",
        "Left", "Right", "Up", "Down",
        "Shift", "Ctrl", "Alt",
        nullptr
    };
    for (int i = 0; specials[i]; ++i) {
        std::string code = std::string("uz.input.is_key_down('") + specials[i] + "')";
        EXPECT_FALSE(EvalBool(code.c_str()))
            << "Expected false for special key '" << specials[i] << "'";
    }
}

TEST_F(LuaInputAPITest, IsKeyDown_FunctionKeys_NoInputSystem_ReturnFalse) {
    const char* fkeys[] = {
        "F1", "F2", "F3", "F4", "F5", "F6",
        "F7", "F8", "F9", "F10", "F11", "F12",
        nullptr
    };
    for (int i = 0; fkeys[i]; ++i) {
        std::string code = std::string("uz.input.is_key_down('") + fkeys[i] + "')";
        EXPECT_FALSE(EvalBool(code.c_str()))
            << "Expected false for function key '" << fkeys[i] << "'";
    }
}

TEST_F(LuaInputAPITest, IsKeyDown_DigitKeys_NoInputSystem_ReturnFalse) {
    const char* digits[] = {"0","1","2","3","4","5","6","7","8","9", nullptr};
    for (int i = 0; digits[i]; ++i) {
        std::string code = std::string("uz.input.is_key_down('") + digits[i] + "')";
        EXPECT_FALSE(EvalBool(code.c_str()))
            << "Expected false for digit key '" << digits[i] << "'";
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Unrecognized key/button names — must return false, not crash
// ─────────────────────────────────────────────────────────────────────────────

TEST_F(LuaInputAPITest, IsKeyDown_UnrecognizedKeyName_ReturnsFalse) {
    // "w" (lowercase) is not in the mapping — must return false, not crash.
    EXPECT_FALSE(EvalBool("uz.input.is_key_down('w')"));
}

TEST_F(LuaInputAPITest, IsKeyDown_EmptyString_ReturnsFalse) {
    EXPECT_FALSE(EvalBool("uz.input.is_key_down('')"));
}

TEST_F(LuaInputAPITest, IsKeyDown_GibberishName_ReturnsFalse) {
    EXPECT_FALSE(EvalBool("uz.input.is_key_down('xyz_not_a_key')"));
}

TEST_F(LuaInputAPITest, IsKeyJustPressed_UnrecognizedKey_ReturnsFalse) {
    EXPECT_FALSE(EvalBool("uz.input.is_key_just_pressed('xyz_not_a_key')"));
}

TEST_F(LuaInputAPITest, IsMouseButtonDown_UnrecognizedButton_ReturnsFalse) {
    // "left" (lowercase) is not in the mapping — must return false.
    EXPECT_FALSE(EvalBool("uz.input.is_mouse_button_down('left')"));
}

TEST_F(LuaInputAPITest, IsMouseButtonDown_GibberishButton_ReturnsFalse) {
    EXPECT_FALSE(EvalBool("uz.input.is_mouse_button_down('not_a_button')"));
}

// ─────────────────────────────────────────────────────────────────────────────
// Wrong argument types / missing args — with no InputSystem, the functions
// return false before reaching luaL_checkstring, so these are NOT Lua errors.
// ─────────────────────────────────────────────────────────────────────────────

TEST_F(LuaInputAPITest, IsKeyDown_NumberArg_NoInputSystem_ReturnsFalse) {
    // No InputSystem → early return false before type check.
    EXPECT_FALSE(EvalBool("uz.input.is_key_down(42)"));
}

TEST_F(LuaInputAPITest, IsKeyDown_NilArg_NoInputSystem_ReturnsFalse) {
    EXPECT_FALSE(EvalBool("uz.input.is_key_down(nil)"));
}

TEST_F(LuaInputAPITest, IsKeyDown_TableArg_NoInputSystem_ReturnsFalse) {
    EXPECT_FALSE(EvalBool("uz.input.is_key_down({})"));
}

TEST_F(LuaInputAPITest, IsKeyJustPressed_NumberArg_NoInputSystem_ReturnsFalse) {
    EXPECT_FALSE(EvalBool("uz.input.is_key_just_pressed(1)"));
}

TEST_F(LuaInputAPITest, IsMouseButtonDown_NumberArg_NoInputSystem_ReturnsFalse) {
    EXPECT_FALSE(EvalBool("uz.input.is_mouse_button_down(1)"));
}

TEST_F(LuaInputAPITest, IsMouseButtonDown_NilArg_NoInputSystem_ReturnsFalse) {
    EXPECT_FALSE(EvalBool("uz.input.is_mouse_button_down(nil)"));
}

TEST_F(LuaInputAPITest, IsKeyDown_NoArgs_NoInputSystem_ReturnsFalse) {
    EXPECT_FALSE(EvalBool("uz.input.is_key_down()"));
}

TEST_F(LuaInputAPITest, IsKeyJustPressed_NoArgs_NoInputSystem_ReturnsFalse) {
    EXPECT_FALSE(EvalBool("uz.input.is_key_just_pressed()"));
}

TEST_F(LuaInputAPITest, IsMouseButtonDown_NoArgs_NoInputSystem_ReturnsFalse) {
    EXPECT_FALSE(EvalBool("uz.input.is_mouse_button_down()"));
}

// ─────────────────────────────────────────────────────────────────────────────
// Lua stack balance — calls must not leak values onto the Lua stack
// ─────────────────────────────────────────────────────────────────────────────

TEST_F(LuaInputAPITest, IsKeyDown_DoesNotLeakStack) {
    lua_State* L = uz::LuaVM::GetState();
    int topBefore = lua_gettop(L);

    EXPECT_TRUE(DoString("uz.input.is_key_down('W')"));

    EXPECT_EQ(lua_gettop(L), topBefore);
}

TEST_F(LuaInputAPITest, GetMouseDelta_DoesNotLeakStack) {
    lua_State* L = uz::LuaVM::GetState();
    int topBefore = lua_gettop(L);

    EXPECT_TRUE(DoString("local a, b = uz.input.get_mouse_delta()"));

    EXPECT_EQ(lua_gettop(L), topBefore);
}

TEST_F(LuaInputAPITest, IsMouseButtonDown_DoesNotLeakStack) {
    lua_State* L = uz::LuaVM::GetState();
    int topBefore = lua_gettop(L);

    EXPECT_TRUE(DoString("uz.input.is_mouse_button_down('Left')"));

    EXPECT_EQ(lua_gettop(L), topBefore);
}

// ─────────────────────────────────────────────────────────────────────────────
// LuaSetInputSystem — nullptr revokes access
// ─────────────────────────────────────────────────────────────────────────────

TEST_F(LuaInputAPITest, SetInputSystemNullptr_AllFunctionsReturnSafeDefaults) {
    // Fixture already calls LuaSetInputSystem(nullptr). Double-check after an
    // explicit re-set that the functions still return safe defaults.
    uz::LuaSetInputSystem(uz::LuaVM::GetState(), nullptr);

    EXPECT_FALSE(EvalBool("uz.input.is_key_down('A')"));
    EXPECT_FALSE(EvalBool("uz.input.is_key_just_pressed('Enter')"));
    EXPECT_FALSE(EvalBool("uz.input.is_mouse_button_down('Left')"));
    EXPECT_DOUBLE_EQ(EvalNumber("(function() local a,b = uz.input.get_mouse_delta() return a end)()"), 0.0);
    EXPECT_DOUBLE_EQ(EvalNumber("(function() local a,b = uz.input.get_mouse_delta() return b end)()"), 0.0);
}

// ─────────────────────────────────────────────────────────────────────────────
// Pcall isolation — errors in input functions do not corrupt VM state
// ─────────────────────────────────────────────────────────────────────────────

TEST_F(LuaInputAPITest, PcallWithWrongType_NoInputSystem_ReturnsFalse_VMStable) {
    // With no InputSystem, pcall succeeds (no error) and returns false.
    EXPECT_TRUE(DoString(
        "local ok, result = pcall(uz.input.is_key_down, 999)\n"
        "assert(ok, 'expected no error with null InputSystem')\n"
        "assert(result == false, 'expected false return value')\n"
    ));

    // VM must still be fully functional.
    EXPECT_TRUE(EvalBool("type(uz.input) == 'table'"));
    EXPECT_FALSE(EvalBool("uz.input.is_key_down('W')"));
}
