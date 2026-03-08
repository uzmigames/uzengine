// test_shared_state.cpp -- Tests for GameState (shared state system).
//
// Covers: get/set for all types, has/remove/clear, keys(), deferred events,
// persistent keys, on_change subscriptions, Lua uz.shared bindings,
// edge cases, and round-trip scenarios.

#include <gtest/gtest.h>

#include "engine/scripting/game_state.h"
#include "engine/core/event_bus.h"
#include "engine/core/math.h"

extern "C" {
#include <lua.h>
#include <lauxlib.h>
#include <lualib.h>
}

#include <string>
#include <vector>

using namespace uz;

// ============================================================================
// Test fixture — provides EventBus + GameState
// ============================================================================

class GameStateTest : public ::testing::Test {
protected:
    EventBus   bus;
    GameState  state{bus};
};

// ============================================================================
// Section 2: Core get/set for all types
// ============================================================================

TEST_F(GameStateTest, SetAndGetBool) {
    state.set<bool>("flag", true);
    EXPECT_TRUE(state.get<bool>("flag"));
}

TEST_F(GameStateTest, SetAndGetInt) {
    state.set<int32_t>("score", 100);
    EXPECT_EQ(state.get<int32_t>("score"), 100);
}

TEST_F(GameStateTest, SetAndGetFloat) {
    state.set<float>("health", 75.5f);
    EXPECT_FLOAT_EQ(state.get<float>("health"), 75.5f);
}

TEST_F(GameStateTest, SetAndGetString) {
    state.set<std::string>("name", "player1");
    EXPECT_EQ(state.get<std::string>("name"), "player1");
}

TEST_F(GameStateTest, SetAndGetVec3) {
    Vec3 v{1.0f, 2.0f, 3.0f, 0.0f};
    state.set<Vec3>("pos", v);
    Vec3 result = state.get<Vec3>("pos");
    EXPECT_FLOAT_EQ(result.x, 1.0f);
    EXPECT_FLOAT_EQ(result.y, 2.0f);
    EXPECT_FLOAT_EQ(result.z, 3.0f);
}

// ============================================================================
// Default values for missing keys
// ============================================================================

TEST_F(GameStateTest, GetMissingBoolReturnsFalse) {
    EXPECT_FALSE(state.get<bool>("missing"));
}

TEST_F(GameStateTest, GetMissingIntReturnsZero) {
    EXPECT_EQ(state.get<int32_t>("missing"), 0);
}

TEST_F(GameStateTest, GetMissingFloatReturnsZero) {
    EXPECT_FLOAT_EQ(state.get<float>("missing"), 0.0f);
}

TEST_F(GameStateTest, GetMissingStringReturnsEmpty) {
    EXPECT_EQ(state.get<std::string>("missing"), "");
}

TEST_F(GameStateTest, GetMissingVec3ReturnsZero) {
    Vec3 v = state.get<Vec3>("missing");
    EXPECT_FLOAT_EQ(v.x, 0.0f);
    EXPECT_FLOAT_EQ(v.y, 0.0f);
    EXPECT_FLOAT_EQ(v.z, 0.0f);
}

TEST_F(GameStateTest, GetWithCustomDefault) {
    EXPECT_EQ(state.get<int32_t>("missing", 42), 42);
    EXPECT_FLOAT_EQ(state.get<float>("missing", 3.14f), 3.14f);
}

// ============================================================================
// Type mismatch: wrong type returns default
// ============================================================================

TEST_F(GameStateTest, GetWrongTypeReturnsDefault) {
    state.set<int32_t>("score", 100);
    // Request as float — wrong type, should return default
    EXPECT_FLOAT_EQ(state.get<float>("score", 1.0f), 1.0f);
    // Request as bool — wrong type, should return default
    EXPECT_FALSE(state.get<bool>("score"));
    // Request as string — wrong type
    EXPECT_EQ(state.get<std::string>("score"), "");
}

// ============================================================================
// get_raw
// ============================================================================

TEST_F(GameStateTest, GetRawReturnsPointerForExisting) {
    state.set<int32_t>("x", 10);
    const GameStateValue* v = state.get_raw("x");
    ASSERT_NE(v, nullptr);
    EXPECT_EQ(std::get<int32_t>(*v), 10);
}

TEST_F(GameStateTest, GetRawReturnsNullForMissing) {
    EXPECT_EQ(state.get_raw("missing"), nullptr);
}

// ============================================================================
// has / remove / clear
// ============================================================================

TEST_F(GameStateTest, HasReturnsTrueForExisting) {
    state.set<int32_t>("x", 1);
    EXPECT_TRUE(state.has("x"));
}

TEST_F(GameStateTest, HasReturnsFalseForMissing) {
    EXPECT_FALSE(state.has("x"));
}

TEST_F(GameStateTest, RemoveDeletesKey) {
    state.set<int32_t>("score", 100);
    EXPECT_TRUE(state.has("score"));
    state.remove("score");
    EXPECT_FALSE(state.has("score"));
}

TEST_F(GameStateTest, RemoveNonexistentIsNoOp) {
    state.remove("nonexistent");  // no crash, no event
    EXPECT_EQ(state.pending_event_count(), 0u);
}

TEST_F(GameStateTest, ClearEmptiesStore) {
    state.set<int32_t>("a", 1);
    state.set<float>("b", 2.0f);
    state.set<bool>("c", true);
    state.clear();
    EXPECT_FALSE(state.has("a"));
    EXPECT_FALSE(state.has("b"));
    EXPECT_FALSE(state.has("c"));
    EXPECT_TRUE(state.keys().empty());
}

TEST_F(GameStateTest, ClearDoesNotEmitPerKeyEvents) {
    state.set<int32_t>("a", 1);
    state.dispatch_deferred_events();  // flush set events

    // Subscribe to removal events
    int remove_count = 0;
    bus.subscribe<GameStateRemovedEvent>([&](const GameStateRemovedEvent&) {
        ++remove_count;
    });

    state.clear();
    state.dispatch_deferred_events();
    EXPECT_EQ(remove_count, 0);
}

// ============================================================================
// keys()
// ============================================================================

TEST_F(GameStateTest, KeysReturnsSortedList) {
    state.set<int32_t>("zebra", 1);
    state.set<int32_t>("apple", 2);
    state.set<int32_t>("mango", 3);
    auto ks = state.keys();
    ASSERT_EQ(ks.size(), 3u);
    EXPECT_EQ(ks[0], "apple");
    EXPECT_EQ(ks[1], "mango");
    EXPECT_EQ(ks[2], "zebra");
}

TEST_F(GameStateTest, KeysEmptyWhenNoEntries) {
    EXPECT_TRUE(state.keys().empty());
}

// ============================================================================
// set_value (variant API)
// ============================================================================

TEST_F(GameStateTest, SetValueWithVariant) {
    GameStateValue val{42};
    state.set_value("x", val);
    EXPECT_EQ(state.get<int32_t>("x"), 42);
}

// ============================================================================
// Overwrite with same value — no event
// ============================================================================

TEST_F(GameStateTest, SetSameValueDoesNotQueueEvent) {
    state.set<int32_t>("score", 100);
    state.dispatch_deferred_events();  // flush initial set event

    state.set<int32_t>("score", 100);  // same value
    EXPECT_EQ(state.pending_event_count(), 0u);
}

// ============================================================================
// Overwrite with different value — event queued
// ============================================================================

TEST_F(GameStateTest, SetDifferentValueQueuesEvent) {
    state.set<int32_t>("score", 100);
    state.dispatch_deferred_events();

    state.set<int32_t>("score", 200);
    EXPECT_EQ(state.pending_event_count(), 1u);
}

// ============================================================================
// Overwrite with different type
// ============================================================================

TEST_F(GameStateTest, OverwriteWithDifferentTypeWorks) {
    state.set<int32_t>("val", 10);
    state.dispatch_deferred_events();

    // Overwrite int with float — different type in variant, always triggers event
    state.set<float>("val", 3.14f);
    EXPECT_FLOAT_EQ(state.get<float>("val"), 3.14f);
    // Old int accessor returns default now
    EXPECT_EQ(state.get<int32_t>("val"), 0);
}

// ============================================================================
// Section 3: Deferred event dispatch
// ============================================================================

TEST_F(GameStateTest, ChangedEventFiredOnDispatch) {
    std::vector<GameStateChangedEvent> fired;
    bus.subscribe<GameStateChangedEvent>([&](const GameStateChangedEvent& e) {
        fired.push_back(e);
    });

    state.set<int32_t>("health", 100);
    EXPECT_TRUE(fired.empty());  // Not fired yet

    state.dispatch_deferred_events();
    ASSERT_EQ(fired.size(), 1u);
    EXPECT_EQ(fired[0].key, "health");
    EXPECT_EQ(std::get<int32_t>(fired[0].new_value), 100);
    EXPECT_EQ(std::get<int32_t>(fired[0].old_value), 0);
}

TEST_F(GameStateTest, NoChangedEventIfValueUnchanged) {
    state.set<int32_t>("health", 100);
    state.dispatch_deferred_events();

    std::vector<GameStateChangedEvent> fired;
    bus.subscribe<GameStateChangedEvent>([&](const GameStateChangedEvent& e) {
        fired.push_back(e);
    });

    state.set<int32_t>("health", 100);  // same value
    state.dispatch_deferred_events();
    EXPECT_TRUE(fired.empty());
}

TEST_F(GameStateTest, RemovedEventFiredOnDispatch) {
    state.set<int32_t>("score", 50);
    state.dispatch_deferred_events();

    std::vector<GameStateRemovedEvent> fired;
    bus.subscribe<GameStateRemovedEvent>([&](const GameStateRemovedEvent& e) {
        fired.push_back(e);
    });

    state.remove("score");
    EXPECT_TRUE(fired.empty());

    state.dispatch_deferred_events();
    ASSERT_EQ(fired.size(), 1u);
    EXPECT_EQ(fired[0].key, "score");
    EXPECT_EQ(std::get<int32_t>(fired[0].last_value), 50);
}

TEST_F(GameStateTest, MultipleEventsDispatchedInOrder) {
    std::vector<std::string> order;
    bus.subscribe<GameStateChangedEvent>([&](const GameStateChangedEvent& e) {
        order.push_back("changed:" + e.key);
    });
    bus.subscribe<GameStateRemovedEvent>([&](const GameStateRemovedEvent& e) {
        order.push_back("removed:" + e.key);
    });

    state.set<int32_t>("a", 1);
    state.set<int32_t>("b", 2);
    state.dispatch_deferred_events();

    ASSERT_EQ(order.size(), 2u);
    EXPECT_EQ(order[0], "changed:a");
    EXPECT_EQ(order[1], "changed:b");
}

// ============================================================================
// Silent mode (deserialization)
// ============================================================================

TEST_F(GameStateTest, SilentModeDoesNotQueueEvents) {
    state.set_silent(true);
    state.set<int32_t>("x", 10);
    state.set<float>("y", 3.14f);
    EXPECT_EQ(state.pending_event_count(), 0u);
    state.set_silent(false);
}

TEST_F(GameStateTest, SilentModeRemoveDoesNotQueueEvent) {
    state.set<int32_t>("x", 10);
    state.dispatch_deferred_events();

    state.set_silent(true);
    state.remove("x");
    EXPECT_EQ(state.pending_event_count(), 0u);
    state.set_silent(false);
}

TEST_F(GameStateTest, IsSilentReflectsState) {
    EXPECT_FALSE(state.is_silent());
    state.set_silent(true);
    EXPECT_TRUE(state.is_silent());
    state.set_silent(false);
    EXPECT_FALSE(state.is_silent());
}

// ============================================================================
// Section 2.13: Persistent keys
// ============================================================================

TEST_F(GameStateTest, KeysArePersistentByDefault) {
    state.set<int32_t>("score", 100);
    EXPECT_TRUE(state.is_persistent("score"));
}

TEST_F(GameStateTest, SetPersistentFalseMarksNonPersistent) {
    state.set<int32_t>("debug_flag", 1);
    state.set_persistent("debug_flag", false);
    EXPECT_FALSE(state.is_persistent("debug_flag"));
}

TEST_F(GameStateTest, SetPersistentTrueRestores) {
    state.set<int32_t>("flag", 1);
    state.set_persistent("flag", false);
    EXPECT_FALSE(state.is_persistent("flag"));
    state.set_persistent("flag", true);
    EXPECT_TRUE(state.is_persistent("flag"));
}

TEST_F(GameStateTest, ClearResetsNonPersistentSet) {
    state.set<int32_t>("x", 1);
    state.set_persistent("x", false);
    state.clear();
    // After clear, non-persistent set is also cleared
    EXPECT_TRUE(state.is_persistent("x"));  // default is persistent
}

TEST_F(GameStateTest, RemoveResetsNonPersistentForKey) {
    state.set<int32_t>("x", 1);
    state.set_persistent("x", false);
    state.remove("x");
    EXPECT_TRUE(state.is_persistent("x"));  // removed key reverts to default
}

// ============================================================================
// Section 3.4-3.5: on_change / unsubscribe
// ============================================================================

TEST_F(GameStateTest, OnChangeCallbackFiredOnDispatch) {
    state.set<int32_t>("health", 100);
    state.dispatch_deferred_events();

    int callback_count = 0;
    GameStateValue last_old, last_new;
    uint32_t sub = state.on_change("health", [&](const GameStateValue& old_v, const GameStateValue& new_v) {
        ++callback_count;
        last_old = old_v;
        last_new = new_v;
    });

    state.set<int32_t>("health", 75);
    state.dispatch_deferred_events();

    EXPECT_EQ(callback_count, 1);
    EXPECT_EQ(std::get<int32_t>(last_old), 100);
    EXPECT_EQ(std::get<int32_t>(last_new), 75);

    state.unsubscribe(sub);
}

TEST_F(GameStateTest, OnChangeNotFiredForOtherKeys) {
    int callback_count = 0;
    state.on_change("health", [&](const GameStateValue&, const GameStateValue&) {
        ++callback_count;
    });

    state.set<int32_t>("score", 200);
    state.dispatch_deferred_events();

    EXPECT_EQ(callback_count, 0);
}

TEST_F(GameStateTest, UnsubscribeStopsCallbacks) {
    int callback_count = 0;
    uint32_t sub = state.on_change("x", [&](const GameStateValue&, const GameStateValue&) {
        ++callback_count;
    });

    state.set<int32_t>("x", 1);
    state.dispatch_deferred_events();
    EXPECT_EQ(callback_count, 1);

    state.unsubscribe(sub);

    state.set<int32_t>("x", 2);
    state.dispatch_deferred_events();
    EXPECT_EQ(callback_count, 1);  // no new callback
}

TEST_F(GameStateTest, MultipleSubscriptionsForSameKey) {
    int count_a = 0, count_b = 0;
    state.on_change("x", [&](const GameStateValue&, const GameStateValue&) { ++count_a; });
    state.on_change("x", [&](const GameStateValue&, const GameStateValue&) { ++count_b; });

    state.set<int32_t>("x", 1);
    state.dispatch_deferred_events();

    EXPECT_EQ(count_a, 1);
    EXPECT_EQ(count_b, 1);
}

TEST_F(GameStateTest, UnsubscribeInvalidIdIsNoOp) {
    state.unsubscribe(999);  // no crash
}

// ============================================================================
// get_all
// ============================================================================

TEST_F(GameStateTest, GetAllReturnsAllEntries) {
    state.set<int32_t>("a", 1);
    state.set<float>("b", 2.0f);
    const auto& all = state.get_all();
    EXPECT_EQ(all.size(), 2u);
    EXPECT_EQ(std::get<int32_t>(all.at("a")), 1);
    EXPECT_FLOAT_EQ(std::get<float>(all.at("b")), 2.0f);
}

// ============================================================================
// Lua binding tests
// ============================================================================

class SharedStateLuaTest : public ::testing::Test {
protected:
    EventBus   bus;
    GameState  state{bus};
    lua_State* L = nullptr;

    void SetUp() override {
        L = luaL_newstate();
        ASSERT_NE(L, nullptr);
        luaL_openlibs(L);

        // Create the uz table
        lua_newtable(L);
        lua_setglobal(L, "uz");

        register_shared_state_bindings(L, &state);
    }

    void TearDown() override {
        if (L) lua_close(L);
    }

    // Helper: run Lua code, return 0 on success
    int run(const char* code) {
        return luaL_dostring(L, code);
    }
};

TEST_F(SharedStateLuaTest, SharedTableExists) {
    int err = run("return type(uz.shared)");
    EXPECT_EQ(err, 0);
    EXPECT_STREQ(lua_tostring(L, -1), "table");
    lua_pop(L, 1);
}

TEST_F(SharedStateLuaTest, SetIntViaNewindex) {
    int err = run("uz.shared.score = 42");
    EXPECT_EQ(err, 0);
    EXPECT_EQ(state.get<int32_t>("score"), 42);
}

TEST_F(SharedStateLuaTest, SetFloatViaNewindex) {
    int err = run("uz.shared.speed = 3.14");
    EXPECT_EQ(err, 0);
    EXPECT_FLOAT_EQ(state.get<float>("speed"), 3.14f);
}

TEST_F(SharedStateLuaTest, SetBoolViaNewindex) {
    int err = run("uz.shared.alive = true");
    EXPECT_EQ(err, 0);
    EXPECT_TRUE(state.get<bool>("alive"));
}

TEST_F(SharedStateLuaTest, SetStringViaNewindex) {
    int err = run("uz.shared.name = 'hero'");
    EXPECT_EQ(err, 0);
    EXPECT_EQ(state.get<std::string>("name"), "hero");
}

TEST_F(SharedStateLuaTest, SetVec3ViaNewindex) {
    int err = run("uz.shared.pos = {x=1, y=2, z=3}");
    EXPECT_EQ(err, 0);
    Vec3 v = state.get<Vec3>("pos");
    EXPECT_FLOAT_EQ(v.x, 1.0f);
    EXPECT_FLOAT_EQ(v.y, 2.0f);
    EXPECT_FLOAT_EQ(v.z, 3.0f);
}

TEST_F(SharedStateLuaTest, GetIntViaIndex) {
    state.set<int32_t>("score", 100);
    int err = run("return uz.shared.score");
    EXPECT_EQ(err, 0);
    EXPECT_EQ(lua_tointeger(L, -1), 100);
    lua_pop(L, 1);
}

TEST_F(SharedStateLuaTest, GetFloatViaIndex) {
    state.set<float>("speed", 5.5f);
    int err = run("return uz.shared.speed");
    EXPECT_EQ(err, 0);
    EXPECT_NEAR(lua_tonumber(L, -1), 5.5, 0.01);
    lua_pop(L, 1);
}

TEST_F(SharedStateLuaTest, GetBoolViaIndex) {
    state.set<bool>("alive", true);
    int err = run("return uz.shared.alive");
    EXPECT_EQ(err, 0);
    EXPECT_TRUE(lua_toboolean(L, -1));
    lua_pop(L, 1);
}

TEST_F(SharedStateLuaTest, GetStringViaIndex) {
    state.set<std::string>("name", "wizard");
    int err = run("return uz.shared.name");
    EXPECT_EQ(err, 0);
    EXPECT_STREQ(lua_tostring(L, -1), "wizard");
    lua_pop(L, 1);
}

TEST_F(SharedStateLuaTest, GetMissingKeyReturnsNil) {
    int err = run("return uz.shared.nonexistent");
    EXPECT_EQ(err, 0);
    EXPECT_TRUE(lua_isnil(L, -1));
    lua_pop(L, 1);
}

TEST_F(SharedStateLuaTest, GetVec3ViaIndex) {
    state.set<Vec3>("pos", Vec3{10.0f, 20.0f, 30.0f, 0.0f});
    int err = run("local v = uz.shared.pos; return v.x, v.y, v.z");
    EXPECT_EQ(err, 0);
    EXPECT_NEAR(lua_tonumber(L, -3), 10.0, 0.01);
    EXPECT_NEAR(lua_tonumber(L, -2), 20.0, 0.01);
    EXPECT_NEAR(lua_tonumber(L, -1), 30.0, 0.01);
    lua_pop(L, 3);
}

// ---------------------------------------------------------------------------
// Lua method API: get, has, remove, keys, set_persistent
// ---------------------------------------------------------------------------

TEST_F(SharedStateLuaTest, MethodGetWithDefault) {
    int err = run("return uz.shared:get('missing', 42)");
    EXPECT_EQ(err, 0);
    EXPECT_EQ(lua_tointeger(L, -1), 42);
    lua_pop(L, 1);
}

TEST_F(SharedStateLuaTest, MethodGetExistingKey) {
    state.set<int32_t>("x", 10);
    int err = run("return uz.shared:get('x', 99)");
    EXPECT_EQ(err, 0);
    EXPECT_EQ(lua_tointeger(L, -1), 10);
    lua_pop(L, 1);
}

TEST_F(SharedStateLuaTest, MethodGetWithoutDefault) {
    int err = run("return uz.shared:get('missing')");
    EXPECT_EQ(err, 0);
    EXPECT_TRUE(lua_isnil(L, -1));
    lua_pop(L, 1);
}

TEST_F(SharedStateLuaTest, MethodHas) {
    state.set<int32_t>("x", 1);
    int err = run("return uz.shared:has('x'), uz.shared:has('y')");
    EXPECT_EQ(err, 0);
    EXPECT_TRUE(lua_toboolean(L, -2));
    EXPECT_FALSE(lua_toboolean(L, -1));
    lua_pop(L, 2);
}

TEST_F(SharedStateLuaTest, MethodRemove) {
    state.set<int32_t>("x", 1);
    int err = run("uz.shared:remove('x')");
    EXPECT_EQ(err, 0);
    EXPECT_FALSE(state.has("x"));
}

TEST_F(SharedStateLuaTest, MethodKeys) {
    state.set<int32_t>("b", 1);
    state.set<int32_t>("a", 2);
    int err = run(
        "local ks = uz.shared:keys()\n"
        "return ks[1], ks[2], #ks");
    EXPECT_EQ(err, 0);
    EXPECT_STREQ(lua_tostring(L, -3), "a");
    EXPECT_STREQ(lua_tostring(L, -2), "b");
    EXPECT_EQ(lua_tointeger(L, -1), 2);
    lua_pop(L, 3);
}

TEST_F(SharedStateLuaTest, MethodSetPersistent) {
    state.set<int32_t>("debug", 1);
    int err = run("uz.shared:set_persistent('debug', false)");
    EXPECT_EQ(err, 0);
    EXPECT_FALSE(state.is_persistent("debug"));
}

TEST_F(SharedStateLuaTest, SetNilRemovesKey) {
    state.set<int32_t>("x", 1);
    int err = run("uz.shared.x = nil");
    EXPECT_EQ(err, 0);
    EXPECT_FALSE(state.has("x"));
}

// ---------------------------------------------------------------------------
// Lua on_change / unsubscribe
// ---------------------------------------------------------------------------

TEST_F(SharedStateLuaTest, OnChangeCallbackFires) {
    state.set<int32_t>("health", 100);
    state.dispatch_deferred_events();

    int err = run(
        "test_old = nil\n"
        "test_new = nil\n"
        "test_sub = uz.shared:on_change('health', function(old, new)\n"
        "  test_old = old\n"
        "  test_new = new\n"
        "end)\n");
    EXPECT_EQ(err, 0);

    state.set<int32_t>("health", 75);
    state.dispatch_deferred_events();

    err = run("return test_old, test_new");
    EXPECT_EQ(err, 0);
    EXPECT_EQ(lua_tointeger(L, -2), 100);
    EXPECT_EQ(lua_tointeger(L, -1), 75);
    lua_pop(L, 2);
}

TEST_F(SharedStateLuaTest, UnsubscribeStopsCallback) {
    state.set<int32_t>("x", 1);
    state.dispatch_deferred_events();

    int err = run(
        "call_count = 0\n"
        "sub_id = uz.shared:on_change('x', function(o, n)\n"
        "  call_count = call_count + 1\n"
        "end)\n");
    EXPECT_EQ(err, 0);

    state.set<int32_t>("x", 2);
    state.dispatch_deferred_events();

    err = run("return call_count");
    EXPECT_EQ(err, 0);
    EXPECT_EQ(lua_tointeger(L, -1), 1);
    lua_pop(L, 1);

    // Unsubscribe
    err = run("uz.shared:unsubscribe(sub_id)");
    EXPECT_EQ(err, 0);

    state.set<int32_t>("x", 3);
    state.dispatch_deferred_events();

    err = run("return call_count");
    EXPECT_EQ(err, 0);
    EXPECT_EQ(lua_tointeger(L, -1), 1);  // still 1, not incremented
    lua_pop(L, 1);
}

// ---------------------------------------------------------------------------
// Lua integration: Script A writes, Script B reads
// ---------------------------------------------------------------------------

TEST_F(SharedStateLuaTest, ScriptAWritesScriptBReads) {
    // Script A sets a value
    int err = run("uz.shared.score = 42");
    EXPECT_EQ(err, 0);

    // Script B reads it
    err = run("return uz.shared.score");
    EXPECT_EQ(err, 0);
    EXPECT_EQ(lua_tointeger(L, -1), 42);
    lua_pop(L, 1);
}

TEST_F(SharedStateLuaTest, CppSetsLuaReads) {
    state.set<std::string>("quest", "dragon");
    int err = run("return uz.shared.quest");
    EXPECT_EQ(err, 0);
    EXPECT_STREQ(lua_tostring(L, -1), "dragon");
    lua_pop(L, 1);
}

TEST_F(SharedStateLuaTest, LuaSetsCppReads) {
    int err = run("uz.shared.level = 5");
    EXPECT_EQ(err, 0);
    EXPECT_EQ(state.get<int32_t>("level"), 5);
}

// ---------------------------------------------------------------------------
// Edge cases
// ---------------------------------------------------------------------------

TEST_F(SharedStateLuaTest, EmptyStringKey) {
    state.set<int32_t>("", 1);
    EXPECT_EQ(state.get<int32_t>(""), 1);
    EXPECT_TRUE(state.has(""));
}

TEST_F(SharedStateLuaTest, LongKey) {
    std::string long_key(1000, 'a');
    state.set<int32_t>(long_key, 42);
    EXPECT_EQ(state.get<int32_t>(long_key), 42);
}

TEST_F(SharedStateLuaTest, ManyKeys) {
    for (int i = 0; i < 100; ++i) {
        state.set<int32_t>("key_" + std::to_string(i), i);
    }
    EXPECT_EQ(state.keys().size(), 100u);
    for (int i = 0; i < 100; ++i) {
        EXPECT_EQ(state.get<int32_t>("key_" + std::to_string(i)), i);
    }
}

TEST_F(SharedStateLuaTest, SetBoolFalse) {
    state.set<bool>("flag", true);
    state.set<bool>("flag", false);
    EXPECT_FALSE(state.get<bool>("flag"));
}

TEST_F(SharedStateLuaTest, SetNegativeInt) {
    state.set<int32_t>("neg", -50);
    EXPECT_EQ(state.get<int32_t>("neg"), -50);
}

TEST_F(SharedStateLuaTest, SetZeroFloat) {
    state.set<float>("zero", 0.0f);
    EXPECT_FLOAT_EQ(state.get<float>("zero"), 0.0f);
    // Setting same value again should not queue event
    state.dispatch_deferred_events();
    state.set<float>("zero", 0.0f);
    EXPECT_EQ(state.pending_event_count(), 0u);
}

// ============================================================================
// Register bindings with null args
// ============================================================================

TEST(SharedStateBindings, RegisterWithNullLuaIsNoOp) {
    EventBus bus;
    GameState state(bus);
    register_shared_state_bindings(nullptr, &state);
    // no crash
}

TEST(SharedStateBindings, RegisterWithNullStateIsNoOp) {
    lua_State* L = luaL_newstate();
    register_shared_state_bindings(L, nullptr);
    lua_close(L);
    // no crash
}

// ============================================================================
// Dispatch clears pending events
// ============================================================================

TEST_F(GameStateTest, DispatchClearsPendingEvents) {
    state.set<int32_t>("a", 1);
    state.set<int32_t>("b", 2);
    EXPECT_EQ(state.pending_event_count(), 2u);

    state.dispatch_deferred_events();
    EXPECT_EQ(state.pending_event_count(), 0u);
}

TEST_F(GameStateTest, DispatchWithNoPendingIsNoOp) {
    state.dispatch_deferred_events();
    EXPECT_EQ(state.pending_event_count(), 0u);
}

// ============================================================================
// Lua type inference: integer vs float distinction
// ============================================================================

TEST_F(SharedStateLuaTest, LuaIntegerMapsToInt32) {
    int err = run("uz.shared.count = 10");
    EXPECT_EQ(err, 0);
    // Should be stored as int32_t
    const GameStateValue* v = state.get_raw("count");
    ASSERT_NE(v, nullptr);
    EXPECT_TRUE(std::holds_alternative<int32_t>(*v));
    EXPECT_EQ(std::get<int32_t>(*v), 10);
}

TEST_F(SharedStateLuaTest, LuaFloatMapsToFloat) {
    int err = run("uz.shared.ratio = 0.5");
    EXPECT_EQ(err, 0);
    const GameStateValue* v = state.get_raw("ratio");
    ASSERT_NE(v, nullptr);
    EXPECT_TRUE(std::holds_alternative<float>(*v));
    EXPECT_NEAR(std::get<float>(*v), 0.5f, 0.001f);
}

// ============================================================================
// Lua boolean false stored correctly
// ============================================================================

TEST_F(SharedStateLuaTest, LuaBoolFalse) {
    int err = run("uz.shared.dead = false");
    EXPECT_EQ(err, 0);
    const GameStateValue* v = state.get_raw("dead");
    ASSERT_NE(v, nullptr);
    EXPECT_TRUE(std::holds_alternative<bool>(*v));
    EXPECT_FALSE(std::get<bool>(*v));
}

// ============================================================================
// Changed events include correct old/new values for all types
// ============================================================================

TEST_F(GameStateTest, ChangedEventForBool) {
    state.set<bool>("flag", false);
    state.dispatch_deferred_events();

    GameStateChangedEvent captured{};
    bus.subscribe<GameStateChangedEvent>([&](const GameStateChangedEvent& e) {
        if (e.key == "flag") captured = e;
    });

    state.set<bool>("flag", true);
    state.dispatch_deferred_events();

    EXPECT_EQ(captured.key, "flag");
    EXPECT_FALSE(std::get<bool>(captured.old_value));
    EXPECT_TRUE(std::get<bool>(captured.new_value));
}

TEST_F(GameStateTest, ChangedEventForFloat) {
    state.set<float>("hp", 100.0f);
    state.dispatch_deferred_events();

    GameStateChangedEvent captured{};
    bus.subscribe<GameStateChangedEvent>([&](const GameStateChangedEvent& e) {
        if (e.key == "hp") captured = e;
    });

    state.set<float>("hp", 50.0f);
    state.dispatch_deferred_events();

    EXPECT_FLOAT_EQ(std::get<float>(captured.old_value), 100.0f);
    EXPECT_FLOAT_EQ(std::get<float>(captured.new_value), 50.0f);
}

TEST_F(GameStateTest, ChangedEventForString) {
    state.set<std::string>("quest", "find_key");
    state.dispatch_deferred_events();

    GameStateChangedEvent captured{};
    bus.subscribe<GameStateChangedEvent>([&](const GameStateChangedEvent& e) {
        if (e.key == "quest") captured = e;
    });

    state.set<std::string>("quest", "slay_dragon");
    state.dispatch_deferred_events();

    EXPECT_EQ(std::get<std::string>(captured.old_value), "find_key");
    EXPECT_EQ(std::get<std::string>(captured.new_value), "slay_dragon");
}

TEST_F(GameStateTest, ChangedEventForVec3) {
    Vec3 v1{1.0f, 2.0f, 3.0f, 0.0f};
    Vec3 v2{4.0f, 5.0f, 6.0f, 0.0f};
    state.set<Vec3>("pos", v1);
    state.dispatch_deferred_events();

    GameStateChangedEvent captured{};
    bus.subscribe<GameStateChangedEvent>([&](const GameStateChangedEvent& e) {
        if (e.key == "pos") captured = e;
    });

    state.set<Vec3>("pos", v2);
    state.dispatch_deferred_events();

    Vec3 old_v = std::get<Vec3>(captured.old_value);
    Vec3 new_v = std::get<Vec3>(captured.new_value);
    EXPECT_FLOAT_EQ(old_v.x, 1.0f);
    EXPECT_FLOAT_EQ(new_v.x, 4.0f);
}

// ============================================================================
// Removed event for string value
// ============================================================================

TEST_F(GameStateTest, RemovedEventContainsLastValue) {
    state.set<std::string>("item", "sword");
    state.dispatch_deferred_events();

    GameStateRemovedEvent captured{};
    bus.subscribe<GameStateRemovedEvent>([&](const GameStateRemovedEvent& e) {
        captured = e;
    });

    state.remove("item");
    state.dispatch_deferred_events();

    EXPECT_EQ(captured.key, "item");
    EXPECT_EQ(std::get<std::string>(captured.last_value), "sword");
}

// ============================================================================
// New key triggers changed event with type-default old value
// ============================================================================

TEST_F(GameStateTest, NewKeyChangedEventHasDefaultOldValue) {
    GameStateChangedEvent captured{};
    bus.subscribe<GameStateChangedEvent>([&](const GameStateChangedEvent& e) {
        captured = e;
    });

    state.set<int32_t>("level", 5);
    state.dispatch_deferred_events();

    EXPECT_EQ(std::get<int32_t>(captured.old_value), 0);
    EXPECT_EQ(std::get<int32_t>(captured.new_value), 5);
}

// ============================================================================
// Lua method keys() on empty state
// ============================================================================

TEST_F(SharedStateLuaTest, MethodKeysEmpty) {
    int err = run("local ks = uz.shared:keys(); return #ks");
    EXPECT_EQ(err, 0);
    EXPECT_EQ(lua_tointeger(L, -1), 0);
    lua_pop(L, 1);
}

// ============================================================================
// Lua set then immediately get in same script
// ============================================================================

TEST_F(SharedStateLuaTest, SetThenGetInSameScript) {
    int err = run(
        "uz.shared.hp = 100\n"
        "return uz.shared.hp");
    EXPECT_EQ(err, 0);
    EXPECT_EQ(lua_tointeger(L, -1), 100);
    lua_pop(L, 1);
}

// ============================================================================
// Persistent key filtering
// ============================================================================

TEST_F(GameStateTest, PersistentKeysFiltering) {
    state.set<int32_t>("save_me", 1);
    state.set<int32_t>("debug_only", 2);
    state.set_persistent("debug_only", false);

    // Collect persistent keys
    std::vector<std::string> persistent;
    for (const auto& k : state.keys()) {
        if (state.is_persistent(k)) {
            persistent.push_back(k);
        }
    }

    EXPECT_EQ(persistent.size(), 1u);
    EXPECT_EQ(persistent[0], "save_me");
}

// ============================================================================
// Dispatch with mixed changed + removed
// ============================================================================

TEST_F(GameStateTest, DispatchMixedEvents) {
    state.set<int32_t>("a", 1);
    state.set<int32_t>("b", 2);
    state.dispatch_deferred_events();

    std::vector<std::string> event_log;
    bus.subscribe<GameStateChangedEvent>([&](const GameStateChangedEvent& e) {
        event_log.push_back("changed:" + e.key);
    });
    bus.subscribe<GameStateRemovedEvent>([&](const GameStateRemovedEvent& e) {
        event_log.push_back("removed:" + e.key);
    });

    state.set<int32_t>("a", 10);  // changed
    state.remove("b");             // removed

    state.dispatch_deferred_events();

    ASSERT_EQ(event_log.size(), 2u);
    EXPECT_EQ(event_log[0], "changed:a");
    EXPECT_EQ(event_log[1], "removed:b");
}
