/// Unit tests for LuaVM — initialization, sandbox, uz.* API registration,
/// script loading/unloading, protected call error handling, and frame globals.

#include "lua_vm.h"

#include <gtest/gtest.h>
#include <string>
#include <vector>

extern "C" {
#include <lua.h>
#include <lualib.h>
#include <lauxlib.h>
}

// ─────────────────────────────────────────────────────────────────────────────
// Test Fixture
// ─────────────────────────────────────────────────────────────────────────────

class LuaVMTest : public ::testing::Test {
  protected:
    void SetUp() override {
        ASSERT_TRUE(uz::LuaVM::Init());
    }

    void TearDown() override {
        uz::LuaVM::Shutdown();
    }

    /// Helper: run a Lua string and return true on success.
    bool DoString(const char* code) {
        lua_State* L = uz::LuaVM::GetState();
        return luaL_dostring(L, code) == 0;
    }

    /// Helper: run a Lua expression and return its boolean result.
    bool EvalBool(const char* expr) {
        lua_State* L = uz::LuaVM::GetState();
        std::string code = std::string("return ") + expr;
        if (luaL_dostring(L, code.c_str()) != 0) {
            lua_pop(L, 1);
            return false;
        }
        bool result = lua_toboolean(L, -1) != 0;
        lua_pop(L, 1);
        return result;
    }

    /// Helper: run a Lua expression and return its number result.
    double EvalNumber(const char* expr) {
        lua_State* L = uz::LuaVM::GetState();
        std::string code = std::string("return ") + expr;
        if (luaL_dostring(L, code.c_str()) != 0) {
            lua_pop(L, 1);
            return -999.0;
        }
        double result = lua_tonumber(L, -1);
        lua_pop(L, 1);
        return result;
    }

    /// Helper: convert a string to script bytes.
    static std::vector<uint8_t> ToBytes(const std::string& s) {
        return {s.begin(), s.end()};
    }
};

// ─────────────────────────────────────────────────────────────────────────────
// Init / Shutdown
// ─────────────────────────────────────────────────────────────────────────────

TEST_F(LuaVMTest, InitReturnsValidState) {
    EXPECT_TRUE(uz::LuaVM::IsInitialized());
    EXPECT_NE(uz::LuaVM::GetState(), nullptr);
}

TEST_F(LuaVMTest, DoubleInitIsNoOp) {
    EXPECT_TRUE(uz::LuaVM::Init()); // second init
    EXPECT_TRUE(uz::LuaVM::IsInitialized());
}

TEST_F(LuaVMTest, ShutdownClearsState) {
    uz::LuaVM::Shutdown();
    EXPECT_FALSE(uz::LuaVM::IsInitialized());
    EXPECT_EQ(uz::LuaVM::GetState(), nullptr);
}

TEST_F(LuaVMTest, DoubleShutdownIsSafe) {
    uz::LuaVM::Shutdown();
    uz::LuaVM::Shutdown(); // no crash
    EXPECT_FALSE(uz::LuaVM::IsInitialized());
}

TEST_F(LuaVMTest, ReinitAfterShutdown) {
    uz::LuaVM::Shutdown();
    ASSERT_TRUE(uz::LuaVM::Init());
    EXPECT_TRUE(uz::LuaVM::IsInitialized());
    EXPECT_NE(uz::LuaVM::GetState(), nullptr);
}

// ─────────────────────────────────────────────────────────────────────────────
// Sandbox
// ─────────────────────────────────────────────────────────────────────────────

TEST_F(LuaVMTest, SandboxRemovesDangerousGlobals) {
    EXPECT_TRUE(EvalBool("os == nil"));
    EXPECT_TRUE(EvalBool("io == nil"));
    EXPECT_TRUE(EvalBool("loadfile == nil"));
    EXPECT_TRUE(EvalBool("dofile == nil"));
    EXPECT_TRUE(EvalBool("load == nil"));
    EXPECT_TRUE(EvalBool("require == nil"));
    EXPECT_TRUE(EvalBool("collectgarbage == nil"));
    EXPECT_TRUE(EvalBool("rawset == nil"));
    EXPECT_TRUE(EvalBool("rawget == nil"));
    EXPECT_TRUE(EvalBool("rawequal == nil"));
}

TEST_F(LuaVMTest, SandboxKeepsSafeGlobals) {
    EXPECT_TRUE(EvalBool("math ~= nil"));
    EXPECT_TRUE(EvalBool("string ~= nil"));
    EXPECT_TRUE(EvalBool("table ~= nil"));
    EXPECT_TRUE(EvalBool("print ~= nil"));
    EXPECT_TRUE(EvalBool("pairs ~= nil"));
    EXPECT_TRUE(EvalBool("ipairs ~= nil"));
    EXPECT_TRUE(EvalBool("type ~= nil"));
    EXPECT_TRUE(EvalBool("tostring ~= nil"));
    EXPECT_TRUE(EvalBool("tonumber ~= nil"));
    EXPECT_TRUE(EvalBool("error ~= nil"));
    EXPECT_TRUE(EvalBool("pcall ~= nil"));
    EXPECT_TRUE(EvalBool("xpcall ~= nil"));
    EXPECT_TRUE(EvalBool("select ~= nil"));
    EXPECT_TRUE(EvalBool("unpack ~= nil"));
}

// ─────────────────────────────────────────────────────────────────────────────
// uz.* global table
// ─────────────────────────────────────────────────────────────────────────────

TEST_F(LuaVMTest, UzTableExists) {
    EXPECT_TRUE(EvalBool("type(uz) == 'table'"));
}

TEST_F(LuaVMTest, UzLogFunctionsExist) {
    EXPECT_TRUE(EvalBool("type(uz.log) == 'function'"));
    EXPECT_TRUE(EvalBool("type(uz.log_warn) == 'function'"));
    EXPECT_TRUE(EvalBool("type(uz.log_error) == 'function'"));
}

TEST_F(LuaVMTest, UzLogDoesNotCrash) {
    EXPECT_TRUE(DoString("uz.log('test info message')"));
    EXPECT_TRUE(DoString("uz.log_warn('test warning')"));
    EXPECT_TRUE(DoString("uz.log_error('test error')"));
}

TEST_F(LuaVMTest, UzQuitExists) {
    EXPECT_TRUE(EvalBool("type(uz.quit) == 'function'"));
}

TEST_F(LuaVMTest, UzQuitSetsRegistryFlag) {
    EXPECT_TRUE(DoString("uz.quit()"));

    lua_State* L = uz::LuaVM::GetState();
    lua_getfield(L, LUA_REGISTRYINDEX, "uz_quit_requested");
    EXPECT_TRUE(lua_toboolean(L, -1) != 0);
    lua_pop(L, 1);
}

TEST_F(LuaVMTest, UzFrameGlobalsInitialValues) {
    EXPECT_DOUBLE_EQ(EvalNumber("uz.delta_time"), 0.0);
    EXPECT_DOUBLE_EQ(EvalNumber("uz.total_time"), 0.0);
    EXPECT_DOUBLE_EQ(EvalNumber("uz.frame_index"), 0.0);
}

// ─────────────────────────────────────────────────────────────────────────────
// Frame globals update
// ─────────────────────────────────────────────────────────────────────────────

TEST_F(LuaVMTest, UpdateFrameGlobalsSetsValues) {
    uz::LuaVM::UpdateFrameGlobals(0.016f, 1.5f, 90);

    EXPECT_NEAR(EvalNumber("uz.delta_time"), 0.016, 0.001);
    EXPECT_NEAR(EvalNumber("uz.total_time"), 1.5, 0.001);
    EXPECT_DOUBLE_EQ(EvalNumber("uz.frame_index"), 90.0);
}

TEST_F(LuaVMTest, UpdateFrameGlobalsMultipleTimes) {
    uz::LuaVM::UpdateFrameGlobals(0.016f, 1.0f, 60);
    uz::LuaVM::UpdateFrameGlobals(0.033f, 2.0f, 120);

    EXPECT_NEAR(EvalNumber("uz.delta_time"), 0.033, 0.001);
    EXPECT_NEAR(EvalNumber("uz.total_time"), 2.0, 0.001);
    EXPECT_DOUBLE_EQ(EvalNumber("uz.frame_index"), 120.0);
}

// ─────────────────────────────────────────────────────────────────────────────
// Script loading
// ─────────────────────────────────────────────────────────────────────────────

TEST_F(LuaVMTest, LoadValidScript) {
    auto bytes = ToBytes("return 42");
    int ref = uz::LuaVM::LoadScript(bytes, "test.lua");
    EXPECT_NE(ref, LUA_REFNIL);
    uz::LuaVM::UnloadScript(ref);
}

TEST_F(LuaVMTest, LoadEmptyScriptFails) {
    std::vector<uint8_t> empty;
    int ref = uz::LuaVM::LoadScript(empty, "empty.lua");
    EXPECT_EQ(ref, LUA_REFNIL);
}

TEST_F(LuaVMTest, LoadSyntaxErrorFails) {
    auto bytes = ToBytes("function ( broken syntax !!!");
    int ref = uz::LuaVM::LoadScript(bytes, "bad.lua");
    EXPECT_EQ(ref, LUA_REFNIL);
}

TEST_F(LuaVMTest, UnloadNilRefIsSafe) {
    uz::LuaVM::UnloadScript(LUA_REFNIL); // no crash
}

TEST_F(LuaVMTest, LoadAndExecuteScript) {
    auto bytes = ToBytes("function hello() return 'world' end");
    int ref = uz::LuaVM::LoadScript(bytes, "hello.lua");
    ASSERT_NE(ref, LUA_REFNIL);

    // Execute the chunk to define the function
    EXPECT_TRUE(uz::LuaVM::PCall(ref, 0, 0, "hello.lua"));

    // Verify the function was defined
    EXPECT_TRUE(EvalBool("type(hello) == 'function'"));
    uz::LuaVM::UnloadScript(ref);
}

// ─────────────────────────────────────────────────────────────────────────────
// Protected call (PCall)
// ─────────────────────────────────────────────────────────────────────────────

TEST_F(LuaVMTest, PCallValidFunction) {
    auto bytes = ToBytes("function add(a, b) return a + b end");
    int ref = uz::LuaVM::LoadScript(bytes, "add.lua");
    ASSERT_NE(ref, LUA_REFNIL);
    EXPECT_TRUE(uz::LuaVM::PCall(ref, 0, 0, "add.lua"));
    uz::LuaVM::UnloadScript(ref);
}

TEST_F(LuaVMTest, PCallRuntimeErrorReturnsFalse) {
    auto bytes = ToBytes("error('intentional test error')");
    int ref = uz::LuaVM::LoadScript(bytes, "error.lua");
    ASSERT_NE(ref, LUA_REFNIL);
    EXPECT_FALSE(uz::LuaVM::PCall(ref, 0, 0, "error.lua"));
    uz::LuaVM::UnloadScript(ref);
}

TEST_F(LuaVMTest, PCallWithUzApi) {
    auto bytes = ToBytes("uz.log('hello from test script')");
    int ref = uz::LuaVM::LoadScript(bytes, "api_test.lua");
    ASSERT_NE(ref, LUA_REFNIL);
    EXPECT_TRUE(uz::LuaVM::PCall(ref, 0, 0, "api_test.lua"));
    uz::LuaVM::UnloadScript(ref);
}

TEST_F(LuaVMTest, PCallDoesNotLeakStack) {
    lua_State* L = uz::LuaVM::GetState();
    int topBefore = lua_gettop(L);

    auto bytes = ToBytes("return 1, 2, 3");
    int ref = uz::LuaVM::LoadScript(bytes, "multi.lua");
    ASSERT_NE(ref, LUA_REFNIL);
    EXPECT_TRUE(uz::LuaVM::PCall(ref, 0, 0, "multi.lua")); // 0 results requested
    uz::LuaVM::UnloadScript(ref);

    EXPECT_EQ(lua_gettop(L), topBefore);
}

// ─────────────────────────────────────────────────────────────────────────────
// Edge cases
// ─────────────────────────────────────────────────────────────────────────────

TEST_F(LuaVMTest, LoadScriptWhenNotInitializedFails) {
    uz::LuaVM::Shutdown();
    auto bytes = ToBytes("return 1");
    int ref = uz::LuaVM::LoadScript(bytes, "fail.lua");
    EXPECT_EQ(ref, LUA_REFNIL);
    // Re-init for TearDown
    uz::LuaVM::Init();
}

TEST_F(LuaVMTest, UpdateFrameGlobalsWhenNotInitializedIsSafe) {
    uz::LuaVM::Shutdown();
    uz::LuaVM::UpdateFrameGlobals(0.016f, 1.0f, 60); // no crash
    uz::LuaVM::Init();
}

TEST_F(LuaVMTest, LuaArithmeticWorks) {
    EXPECT_DOUBLE_EQ(EvalNumber("1 + 1"), 2.0);
    EXPECT_DOUBLE_EQ(EvalNumber("math.sqrt(16)"), 4.0);
}

TEST_F(LuaVMTest, UzTableIsNotFrozen) {
    // Scripts can extend the uz table (for sub-API registration)
    EXPECT_TRUE(DoString("uz.custom_value = 42"));
    EXPECT_DOUBLE_EQ(EvalNumber("uz.custom_value"), 42.0);
}
