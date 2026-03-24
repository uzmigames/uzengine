/// Unit tests for the uz.cvar Lua API — get, set, reset, reset_all, info, list.
///
/// All CVars are registered directly through CVarRegistry::Get().Register()
/// before the Lua VM is initialized, because LuaVM::Init() registers
/// uz.cvar API which reads from the same singleton. Tests use unique CVar
/// names with a "test." prefix to avoid collisions with engine CVars that
/// may have been registered by static initializers.
///
/// Pattern: LuaVM::Init() in SetUp, LuaVM::Shutdown() in TearDown,
/// Lua snippets executed via luaL_dostring, results verified with EXPECT_*.

#include "lua_vm.h"
#include "cvar.h"

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

class LuaCvarAPITest : public ::testing::Test {
protected:
    void SetUp() override {
        ASSERT_TRUE(uz::LuaVM::Init());
    }

    void TearDown() override {
        uz::LuaVM::Shutdown();
    }

    /// Runs Lua code, returns true on success.
    bool DoString(const char* code) {
        lua_State* L = uz::LuaVM::GetState();
        const int rc = luaL_dostring(L, code);
        if (rc != 0) {
            const char* err = lua_tostring(L, -1);
            ADD_FAILURE() << "Lua error in DoString: " << (err ? err : "unknown");
            lua_pop(L, 1);
        }
        return rc == 0;
    }

    /// Evaluates a Lua expression and returns it as a double.
    /// Returns -999.0 on Lua error.
    double EvalNumber(const char* expr) {
        lua_State* L = uz::LuaVM::GetState();
        const std::string code = std::string("return ") + expr;
        if (luaL_dostring(L, code.c_str()) != 0) {
            const char* err = lua_tostring(L, -1);
            ADD_FAILURE() << "Lua eval error: " << (err ? err : "unknown");
            lua_pop(L, 1);
            return -999.0;
        }
        const double r = lua_tonumber(L, -1);
        lua_pop(L, 1);
        return r;
    }

    /// Evaluates a Lua expression and returns it as a bool.
    bool EvalBool(const char* expr) {
        lua_State* L = uz::LuaVM::GetState();
        const std::string code = std::string("return ") + expr;
        if (luaL_dostring(L, code.c_str()) != 0) {
            lua_pop(L, 1);
            return false;
        }
        const bool r = lua_toboolean(L, -1) != 0;
        lua_pop(L, 1);
        return r;
    }

    /// Evaluates a Lua expression and returns it as a string.
    /// Returns "" on Lua error or if the value is not a string.
    std::string EvalString(const char* expr) {
        lua_State* L = uz::LuaVM::GetState();
        const std::string code = std::string("return ") + expr;
        if (luaL_dostring(L, code.c_str()) != 0) {
            lua_pop(L, 1);
            return "";
        }
        std::string r;
        if (lua_isstring(L, -1)) {
            r = lua_tostring(L, -1);
        }
        lua_pop(L, 1);
        return r;
    }
};

// ─────────────────────────────────────────────────────────────────────────────
// Table existence
// ─────────────────────────────────────────────────────────────────────────────

TEST_F(LuaCvarAPITest, CvarTableExists) {
    EXPECT_TRUE(EvalBool("type(uz.cvar) == 'table'"));
}

TEST_F(LuaCvarAPITest, CvarFunctionsExist) {
    EXPECT_TRUE(EvalBool("type(uz.cvar.get) == 'function'"));
    EXPECT_TRUE(EvalBool("type(uz.cvar.set) == 'function'"));
    EXPECT_TRUE(EvalBool("type(uz.cvar.reset) == 'function'"));
    EXPECT_TRUE(EvalBool("type(uz.cvar.reset_all) == 'function'"));
    EXPECT_TRUE(EvalBool("type(uz.cvar.info) == 'function'"));
    EXPECT_TRUE(EvalBool("type(uz.cvar.list) == 'function'"));
}

// ─────────────────────────────────────────────────────────────────────────────
// uz.cvar.get
// ─────────────────────────────────────────────────────────────────────────────

TEST_F(LuaCvarAPITest, CvarGet_ExistingFloatCVar_ReturnsDefaultValue) {
    // Register before querying — CVarRegistry is a persistent singleton.
    uz::CVarRegistry::Get().Register("test.get.float", 42.5f, "test float cvar",
                                     0.f, 100.f);

    const double val = EvalNumber("uz.cvar.get('test.get.float')");
    EXPECT_NEAR(val, 42.5, 1e-4);
}

TEST_F(LuaCvarAPITest, CvarGet_NonexistentCVar_ReturnsNil) {
    EXPECT_TRUE(EvalBool("uz.cvar.get('test.does.not.exist') == nil"));
}

TEST_F(LuaCvarAPITest, CvarGet_AfterSet_ReturnsNewValue) {
    uz::CVarRegistry::Get().Register("test.get.afterset", 10.f, "get after set",
                                     0.f, 200.f);

    EXPECT_TRUE(DoString("uz.cvar.set('test.get.afterset', 77.0)"));
    const double val = EvalNumber("uz.cvar.get('test.get.afterset')");
    EXPECT_NEAR(val, 77.0, 1e-4);

    // Restore default so other tests are not affected.
    uz::CVarRegistry::Get().Reset("test.get.afterset");
}

TEST_F(LuaCvarAPITest, CvarGet_BoolCVar_StoredAsFloat_Zero) {
    uz::CVarRegistry::Get().Register("test.get.bool.off", 0.f, "bool off", 0.f, 1.f);

    const double val = EvalNumber("uz.cvar.get('test.get.bool.off')");
    EXPECT_NEAR(val, 0.0, 1e-9);
}

TEST_F(LuaCvarAPITest, CvarGet_BoolCVar_StoredAsFloat_One) {
    uz::CVarRegistry::Get().Register("test.get.bool.on", 1.f, "bool on", 0.f, 1.f);

    const double val = EvalNumber("uz.cvar.get('test.get.bool.on')");
    EXPECT_NEAR(val, 1.0, 1e-9);
}

// ─────────────────────────────────────────────────────────────────────────────
// uz.cvar.set
// ─────────────────────────────────────────────────────────────────────────────

TEST_F(LuaCvarAPITest, CvarSet_ValidValue_WrittenToRegistry) {
    uz::CVarRegistry::Get().Register("test.set.basic", 5.f, "basic set", 0.f, 100.f);

    EXPECT_TRUE(DoString("uz.cvar.set('test.set.basic', 33.0)"));
    EXPECT_NEAR(uz::CVarRegistry::Get().GetFloat("test.set.basic"), 33.f, 1e-4f);

    uz::CVarRegistry::Get().Reset("test.set.basic");
}

TEST_F(LuaCvarAPITest, CvarSet_ClampsToMaxRange) {
    uz::CVarRegistry::Get().Register("test.set.clamp.max", 50.f, "clamp max",
                                     0.f, 100.f);

    // Setting 200 must clamp to 100.
    EXPECT_TRUE(DoString("uz.cvar.set('test.set.clamp.max', 200.0)"));
    const double val = EvalNumber("uz.cvar.get('test.set.clamp.max')");
    EXPECT_NEAR(val, 100.0, 1e-4);

    uz::CVarRegistry::Get().Reset("test.set.clamp.max");
}

TEST_F(LuaCvarAPITest, CvarSet_ClampsToMinRange) {
    uz::CVarRegistry::Get().Register("test.set.clamp.min", 50.f, "clamp min",
                                     10.f, 100.f);

    // Setting -5 must clamp to 10.
    EXPECT_TRUE(DoString("uz.cvar.set('test.set.clamp.min', -5.0)"));
    const double val = EvalNumber("uz.cvar.get('test.set.clamp.min')");
    EXPECT_NEAR(val, 10.0, 1e-4);

    uz::CVarRegistry::Get().Reset("test.set.clamp.min");
}

TEST_F(LuaCvarAPITest, CvarSet_NonexistentCVar_DoesNotCrash) {
    // Lua must not error or crash — it just logs a warning.
    EXPECT_TRUE(DoString("uz.cvar.set('test.set.nonexistent', 1.0)"));
}

TEST_F(LuaCvarAPITest, CvarSet_ExactBoundaryMin_IsAccepted) {
    uz::CVarRegistry::Get().Register("test.set.boundary.min", 50.f,
                                     "boundary min", 0.f, 100.f);

    EXPECT_TRUE(DoString("uz.cvar.set('test.set.boundary.min', 0.0)"));
    const double val = EvalNumber("uz.cvar.get('test.set.boundary.min')");
    EXPECT_NEAR(val, 0.0, 1e-6);

    uz::CVarRegistry::Get().Reset("test.set.boundary.min");
}

TEST_F(LuaCvarAPITest, CvarSet_ExactBoundaryMax_IsAccepted) {
    uz::CVarRegistry::Get().Register("test.set.boundary.max", 50.f,
                                     "boundary max", 0.f, 100.f);

    EXPECT_TRUE(DoString("uz.cvar.set('test.set.boundary.max', 100.0)"));
    const double val = EvalNumber("uz.cvar.get('test.set.boundary.max')");
    EXPECT_NEAR(val, 100.0, 1e-6);

    uz::CVarRegistry::Get().Reset("test.set.boundary.max");
}

// ─────────────────────────────────────────────────────────────────────────────
// uz.cvar.reset
// ─────────────────────────────────────────────────────────────────────────────

TEST_F(LuaCvarAPITest, CvarReset_RestoresDefaultAfterSet) {
    uz::CVarRegistry::Get().Register("test.reset.basic", 25.f, "reset basic",
                                     0.f, 100.f);

    // Modify, then reset.
    EXPECT_TRUE(DoString("uz.cvar.set('test.reset.basic', 99.0)"));
    EXPECT_TRUE(DoString("uz.cvar.reset('test.reset.basic')"));

    const double val = EvalNumber("uz.cvar.get('test.reset.basic')");
    EXPECT_NEAR(val, 25.0, 1e-4);
}

TEST_F(LuaCvarAPITest, CvarReset_NonexistentCVar_DoesNotCrash) {
    EXPECT_TRUE(DoString("uz.cvar.reset('test.reset.nonexistent')"));
}

TEST_F(LuaCvarAPITest, CvarReset_AlreadyAtDefault_IsNoOp) {
    uz::CVarRegistry::Get().Register("test.reset.noop", 7.f, "reset noop",
                                     0.f, 100.f);

    // Value is already the default — reset is idempotent.
    EXPECT_TRUE(DoString("uz.cvar.reset('test.reset.noop')"));
    const double val = EvalNumber("uz.cvar.get('test.reset.noop')");
    EXPECT_NEAR(val, 7.0, 1e-4);
}

// ─────────────────────────────────────────────────────────────────────────────
// uz.cvar.reset_all
// ─────────────────────────────────────────────────────────────────────────────

TEST_F(LuaCvarAPITest, CvarResetAll_RestoresMultipleCVarsToDefaults) {
    uz::CVarRegistry::Get().Register("test.resetall.a", 10.f, "reset all a", 0.f, 100.f);
    uz::CVarRegistry::Get().Register("test.resetall.b", 20.f, "reset all b", 0.f, 100.f);
    uz::CVarRegistry::Get().Register("test.resetall.c", 30.f, "reset all c", 0.f, 100.f);

    // Set all three to different values.
    EXPECT_TRUE(DoString("uz.cvar.set('test.resetall.a', 1.0)"));
    EXPECT_TRUE(DoString("uz.cvar.set('test.resetall.b', 2.0)"));
    EXPECT_TRUE(DoString("uz.cvar.set('test.resetall.c', 3.0)"));

    // Reset all.
    EXPECT_TRUE(DoString("uz.cvar.reset_all()"));

    EXPECT_NEAR(EvalNumber("uz.cvar.get('test.resetall.a')"), 10.0, 1e-4);
    EXPECT_NEAR(EvalNumber("uz.cvar.get('test.resetall.b')"), 20.0, 1e-4);
    EXPECT_NEAR(EvalNumber("uz.cvar.get('test.resetall.c')"), 30.0, 1e-4);
}

TEST_F(LuaCvarAPITest, CvarResetAll_DoesNotCrashOnEmptyRegistry) {
    // Even if no test CVars have been registered at this point (engine CVars
    // may be registered by static init), reset_all must not crash.
    EXPECT_TRUE(DoString("uz.cvar.reset_all()"));
}

// ─────────────────────────────────────────────────────────────────────────────
// uz.cvar.info
// ─────────────────────────────────────────────────────────────────────────────

TEST_F(LuaCvarAPITest, CvarInfo_NonexistentCVar_ReturnsNil) {
    EXPECT_TRUE(EvalBool("uz.cvar.info('test.info.nonexistent') == nil"));
}

TEST_F(LuaCvarAPITest, CvarInfo_ExistingCVar_ReturnsTable) {
    uz::CVarRegistry::Get().Register("test.info.basic", 50.f,
                                     "info basic desc", 0.f, 100.f);

    EXPECT_TRUE(EvalBool("type(uz.cvar.info('test.info.basic')) == 'table'"));
}

TEST_F(LuaCvarAPITest, CvarInfo_NameField_MatchesRegisteredName) {
    uz::CVarRegistry::Get().Register("test.info.name", 0.f, "", 0.f, 1.f);

    const std::string name = EvalString("uz.cvar.info('test.info.name').name");
    EXPECT_EQ(name, "test.info.name");
}

TEST_F(LuaCvarAPITest, CvarInfo_DescriptionField_MatchesRegisteredDescription) {
    uz::CVarRegistry::Get().Register("test.info.desc", 0.f, "my description", 0.f, 1.f);

    const std::string desc = EvalString("uz.cvar.info('test.info.desc').description");
    EXPECT_EQ(desc, "my description");
}

TEST_F(LuaCvarAPITest, CvarInfo_ValueField_ReturnsCurrentValue) {
    uz::CVarRegistry::Get().Register("test.info.value", 13.f, "value test", -100.f, 100.f);

    const double val = EvalNumber("uz.cvar.info('test.info.value').value");
    EXPECT_NEAR(val, 13.0, 1e-4);
}

TEST_F(LuaCvarAPITest, CvarInfo_DefaultField_ReturnsDefaultValue) {
    uz::CVarRegistry::Get().Register("test.info.default", 99.f, "default test", 0.f, 100.f);

    // Modify current value — default must remain 99.
    uz::CVarRegistry::Get().Set("test.info.default", 1.f);

    const double def = EvalNumber("uz.cvar.info('test.info.default').default");
    EXPECT_NEAR(def, 99.0, 1e-4);

    uz::CVarRegistry::Get().Reset("test.info.default");
}

TEST_F(LuaCvarAPITest, CvarInfo_MinField_ReturnsMinBound) {
    uz::CVarRegistry::Get().Register("test.info.min", 5.f, "min test", -50.f, 50.f);

    const double min = EvalNumber("uz.cvar.info('test.info.min').min");
    EXPECT_NEAR(min, -50.0, 1e-4);
}

TEST_F(LuaCvarAPITest, CvarInfo_MaxField_ReturnsMaxBound) {
    uz::CVarRegistry::Get().Register("test.info.max", 5.f, "max test", -50.f, 75.f);

    const double max = EvalNumber("uz.cvar.info('test.info.max').max");
    EXPECT_NEAR(max, 75.0, 1e-4);
}

TEST_F(LuaCvarAPITest, CvarInfo_AllSixFields_PresentInReturnedTable) {
    uz::CVarRegistry::Get().Register("test.info.allfields", 3.14f,
                                     "all fields", 0.f, 10.f);

    // Every mandatory field must be non-nil.
    EXPECT_TRUE(EvalBool(
        "(function()\n"
        "  local t = uz.cvar.info('test.info.allfields')\n"
        "  return t ~= nil\n"
        "     and t.name ~= nil\n"
        "     and t.description ~= nil\n"
        "     and t.value ~= nil\n"
        "     and t['default'] ~= nil\n"
        "     and t.min ~= nil\n"
        "     and t.max ~= nil\n"
        "end)()"
    ));
}

// ─────────────────────────────────────────────────────────────────────────────
// uz.cvar.list
// ─────────────────────────────────────────────────────────────────────────────

TEST_F(LuaCvarAPITest, CvarList_NoFilter_ReturnsTable) {
    // list() must return a table even if only engine CVars are registered.
    EXPECT_TRUE(EvalBool("type(uz.cvar.list()) == 'table'"));
}

TEST_F(LuaCvarAPITest, CvarList_NoFilter_ContainsRegisteredCVar) {
    uz::CVarRegistry::Get().Register("test.list.contains", 0.f, "", 0.f, 1.f);

    EXPECT_TRUE(EvalBool(
        "(function()\n"
        "  local names = uz.cvar.list()\n"
        "  for _, n in ipairs(names) do\n"
        "    if n == 'test.list.contains' then return true end\n"
        "  end\n"
        "  return false\n"
        "end)()"
    ));
}

TEST_F(LuaCvarAPITest, CvarList_WithMatchingPrefix_ReturnsOnlyMatching) {
    uz::CVarRegistry::Get().Register("test.list.prefix.a", 0.f, "", 0.f, 1.f);
    uz::CVarRegistry::Get().Register("test.list.prefix.b", 0.f, "", 0.f, 1.f);
    // This one must NOT appear in the prefix-filtered result.
    uz::CVarRegistry::Get().Register("other.list.prefix.x", 0.f, "", 0.f, 1.f);

    EXPECT_TRUE(EvalBool(
        "(function()\n"
        "  local names = uz.cvar.list('test.list.prefix')\n"
        "  -- All returned names must start with the prefix.\n"
        "  for _, n in ipairs(names) do\n"
        "    if n:sub(1, #'test.list.prefix') ~= 'test.list.prefix' then\n"
        "      return false\n"
        "    end\n"
        "  end\n"
        "  -- Must have found at least the two we registered above.\n"
        "  return #names >= 2\n"
        "end)()"
    ));
}

TEST_F(LuaCvarAPITest, CvarList_WithNonMatchingPrefix_ReturnsEmptyTable) {
    // Ensure the prefix we pick matches absolutely nothing.
    EXPECT_TRUE(EvalBool(
        "(function()\n"
        "  local names = uz.cvar.list('zzz.no.cvar.has.this.prefix.xyz')\n"
        "  return #names == 0\n"
        "end)()"
    ));
}

TEST_F(LuaCvarAPITest, CvarList_EmptyStringPrefix_SameAsNoFilter) {
    uz::CVarRegistry::Get().Register("test.list.empty.prefix", 0.f, "", 0.f, 1.f);

    EXPECT_TRUE(EvalBool(
        "(function()\n"
        "  local all  = uz.cvar.list()\n"
        "  local empty = uz.cvar.list('')\n"
        "  -- Both calls must return the same count.\n"
        "  return #all == #empty\n"
        "end)()"
    ));
}

TEST_F(LuaCvarAPITest, CvarList_ReturnedElements_AreStrings) {
    uz::CVarRegistry::Get().Register("test.list.type.check", 0.f, "", 0.f, 1.f);

    EXPECT_TRUE(EvalBool(
        "(function()\n"
        "  local names = uz.cvar.list('test.list.type.check')\n"
        "  if #names == 0 then return false end\n"
        "  for _, n in ipairs(names) do\n"
        "    if type(n) ~= 'string' then return false end\n"
        "  end\n"
        "  return true\n"
        "end)()"
    ));
}

// ─────────────────────────────────────────────────────────────────────────────
// Round-trip: set → info → get consistency
// ─────────────────────────────────────────────────────────────────────────────

TEST_F(LuaCvarAPITest, CvarSetThenInfo_ValueFieldReflectsNewValue) {
    uz::CVarRegistry::Get().Register("test.roundtrip.info", 1.f, "round trip",
                                     0.f, 100.f);

    EXPECT_TRUE(DoString("uz.cvar.set('test.roundtrip.info', 55.0)"));

    const double infoVal = EvalNumber("uz.cvar.info('test.roundtrip.info').value");
    const double getVal  = EvalNumber("uz.cvar.get('test.roundtrip.info')");
    EXPECT_NEAR(infoVal, getVal, 1e-9);
    EXPECT_NEAR(infoVal, 55.0, 1e-4);

    uz::CVarRegistry::Get().Reset("test.roundtrip.info");
}

TEST_F(LuaCvarAPITest, CvarSetThenReset_InfoDefaultFieldUnchanged) {
    uz::CVarRegistry::Get().Register("test.roundtrip.default", 22.f,
                                     "default unchanged", 0.f, 100.f);

    EXPECT_TRUE(DoString("uz.cvar.set('test.roundtrip.default', 88.0)"));
    EXPECT_TRUE(DoString("uz.cvar.reset('test.roundtrip.default')"));

    const double defaultVal = EvalNumber(
        "uz.cvar.info('test.roundtrip.default')['default']");
    const double currentVal = EvalNumber("uz.cvar.get('test.roundtrip.default')");

    // After reset, the current value must equal the default.
    EXPECT_NEAR(defaultVal, 22.0, 1e-4);
    EXPECT_NEAR(currentVal, 22.0, 1e-4);
}

// ─────────────────────────────────────────────────────────────────────────────
// Stack hygiene — no Lua stack leak after each call
// ─────────────────────────────────────────────────────────────────────────────

TEST_F(LuaCvarAPITest, CvarGet_DoesNotLeakLuaStack) {
    uz::CVarRegistry::Get().Register("test.stack.get", 1.f, "", 0.f, 10.f);

    lua_State* L = uz::LuaVM::GetState();
    const int topBefore = lua_gettop(L);

    EXPECT_TRUE(DoString("local _ = uz.cvar.get('test.stack.get')"));

    EXPECT_EQ(lua_gettop(L), topBefore);
}

TEST_F(LuaCvarAPITest, CvarSet_DoesNotLeakLuaStack) {
    uz::CVarRegistry::Get().Register("test.stack.set", 1.f, "", 0.f, 10.f);

    lua_State* L = uz::LuaVM::GetState();
    const int topBefore = lua_gettop(L);

    EXPECT_TRUE(DoString("uz.cvar.set('test.stack.set', 2.0)"));

    EXPECT_EQ(lua_gettop(L), topBefore);

    uz::CVarRegistry::Get().Reset("test.stack.set");
}

TEST_F(LuaCvarAPITest, CvarInfo_DoesNotLeakLuaStack) {
    uz::CVarRegistry::Get().Register("test.stack.info", 1.f, "", 0.f, 10.f);

    lua_State* L = uz::LuaVM::GetState();
    const int topBefore = lua_gettop(L);

    EXPECT_TRUE(DoString("local _ = uz.cvar.info('test.stack.info')"));

    EXPECT_EQ(lua_gettop(L), topBefore);
}

TEST_F(LuaCvarAPITest, CvarList_DoesNotLeakLuaStack) {
    lua_State* L = uz::LuaVM::GetState();
    const int topBefore = lua_gettop(L);

    EXPECT_TRUE(DoString("local _ = uz.cvar.list()"));

    EXPECT_EQ(lua_gettop(L), topBefore);
}
