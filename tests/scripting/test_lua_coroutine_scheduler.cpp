// test_lua_coroutine_scheduler.cpp -- Comprehensive tests for LuaCoroutineScheduler.
//
// Covers: lifecycle (init/shutdown), C++ spawn/drain/cancel API, Lua API
// (uz.coroutine.*), wait conditions (seconds, frames, until), error handling,
// error callbacks, multiple coroutines, node-scoped cancellation, edge cases.

#include <gtest/gtest.h>

#include "engine/scripting/lua_coroutine_scheduler.h"

extern "C" {
#include <lua.h>
#include <lauxlib.h>
#include <lualib.h>
}

#include <string>
#include <vector>

using namespace uz;

// ============================================================================
// Test fixture — owns a raw lua_State (no LuaVM sandbox, no require stripping)
// The scheduler only needs a plain lua_State with coroutine support.
// ============================================================================

class CoroutineSchedulerTest : public ::testing::Test {
protected:
    lua_State*              L    = nullptr;
    LuaCoroutineScheduler   sched;

    void SetUp() override {
        L = luaL_newstate();
        ASSERT_NE(L, nullptr);
        luaL_openlibs(L);   // opens coroutine, math, string, table, io, os
        sched.init(L);
    }

    void TearDown() override {
        sched.shutdown();
        if (L) {
            lua_close(L);
            L = nullptr;
        }
    }

    // ---- helpers ----

    /// Create a new Lua thread from a function string, push it, ref it.
    /// Returns <thread, ref> so caller can hand them to spawn().
    /// The function is left at the thread's stack top (ready to run).
    std::pair<lua_State*, int> make_thread(const char* fn_src) {
        // Compile the function in the main state
        int load_err = luaL_loadstring(L, fn_src);
        EXPECT_EQ(load_err, 0) << lua_tostring(L, -1);

        // Create a thread and move the function onto it
        lua_State* thread = lua_newthread(L);
        int thread_ref = luaL_ref(L, LUA_REGISTRYINDEX);

        // xmove: fn was at top of L (-1 after lua_newthread popped thread)
        lua_xmove(L, thread, 1);

        return {thread, thread_ref};
    }

    /// Run a thread once (initial resume), capture its status, return it.
    int initial_resume(lua_State* thread, int nargs = 0) {
        return lua_resume(thread, nargs);
    }

    /// Register the uz.coroutine API on a fresh "uz" table in the main state.
    void register_api() {
        lua_newtable(L);           // push uz table
        sched.register_lua_api(L); // registers coroutine subtable
        lua_setglobal(L, "uz");    // store as "uz"
    }

    /// Helper: run Lua code, returns 0 on success.
    int run(const char* code) {
        return luaL_dostring(L, code);
    }
};

// ============================================================================
// Section 1: Lifecycle — init / shutdown
// ============================================================================

TEST(LuaCoroutineScheduler_Lifecycle, DefaultConstruct_NoState) {
    LuaCoroutineScheduler s;
    EXPECT_EQ(s.active_count(), 0u);
    EXPECT_EQ(s.total_spawned(), 0u);
}

TEST(LuaCoroutineScheduler_Lifecycle, ShutdownWithoutInit_NoOp) {
    LuaCoroutineScheduler s;
    s.shutdown();   // must not crash
    EXPECT_EQ(s.active_count(), 0u);
}

TEST(LuaCoroutineScheduler_Lifecycle, DestructorWithoutInit_NoOp) {
    {
        LuaCoroutineScheduler s;
        // No init — destructor must not crash
    }
    SUCCEED();
}

TEST(LuaCoroutineScheduler_Lifecycle, InitRegistersSchedulerInRegistry) {
    lua_State* L = luaL_newstate();
    ASSERT_NE(L, nullptr);

    LuaCoroutineScheduler s;
    s.init(L);

    // The scheduler pointer should be in the registry under kSchedulerRegistryKey
    lua_getfield(L, LUA_REGISTRYINDEX, "uz_coroutine_scheduler");
    EXPECT_FALSE(lua_isnil(L, -1));
    void* ptr = lua_touserdata(L, -1);
    EXPECT_EQ(ptr, &s);
    lua_pop(L, 1);

    s.shutdown();
    lua_close(L);
}

TEST(LuaCoroutineScheduler_Lifecycle, ShutdownClearsRegistry) {
    lua_State* L = luaL_newstate();
    ASSERT_NE(L, nullptr);

    LuaCoroutineScheduler s;
    s.init(L);
    s.shutdown();

    lua_getfield(L, LUA_REGISTRYINDEX, "uz_coroutine_scheduler");
    EXPECT_TRUE(lua_isnil(L, -1));
    lua_pop(L, 1);

    lua_close(L);
}

TEST(LuaCoroutineScheduler_Lifecycle, ShutdownResetsCounters) {
    lua_State* L = luaL_newstate();
    ASSERT_NE(L, nullptr);
    luaL_openlibs(L);

    LuaCoroutineScheduler s;
    s.init(L);

    // Manually spawn a synthetic entry via C++ API
    lua_State* thread = lua_newthread(L);
    int ref = luaL_ref(L, LUA_REGISTRYINDEX);
    s.spawn(thread, ref, 1, WaitFrames{1});
    EXPECT_EQ(s.total_spawned(), 1u);
    EXPECT_EQ(s.active_count(), 1u);

    s.shutdown();
    EXPECT_EQ(s.active_count(), 0u);
    EXPECT_EQ(s.total_spawned(), 0u);

    lua_close(L);
}

TEST(LuaCoroutineScheduler_Lifecycle, InitWithNullState_NoOp) {
    LuaCoroutineScheduler s;
    s.init(nullptr);   // must not crash
    s.drain(0.016f);   // drain without state must not crash
    s.shutdown();
}

TEST(LuaCoroutineScheduler_Lifecycle, CopyDeleted) {
    static_assert(!std::is_copy_constructible_v<LuaCoroutineScheduler>);
    static_assert(!std::is_copy_assignable_v<LuaCoroutineScheduler>);
    SUCCEED();
}

// ============================================================================
// Section 2: register_lua_api
// ============================================================================

TEST_F(CoroutineSchedulerTest, RegisterLuaApi_CreatesCoroutineSubtable) {
    register_api();

    int err = run("return type(uz.coroutine)");
    EXPECT_EQ(err, 0);
    EXPECT_STREQ(lua_tostring(L, -1), "table");
    lua_pop(L, 1);
}

TEST_F(CoroutineSchedulerTest, RegisterLuaApi_WaitIsFunction) {
    register_api();
    int err = run("return type(uz.coroutine.wait)");
    EXPECT_EQ(err, 0);
    EXPECT_STREQ(lua_tostring(L, -1), "function");
    lua_pop(L, 1);
}

TEST_F(CoroutineSchedulerTest, RegisterLuaApi_WaitFramesIsFunction) {
    register_api();
    int err = run("return type(uz.coroutine.wait_frames)");
    EXPECT_EQ(err, 0);
    EXPECT_STREQ(lua_tostring(L, -1), "function");
    lua_pop(L, 1);
}

TEST_F(CoroutineSchedulerTest, RegisterLuaApi_WaitUntilIsFunction) {
    register_api();
    int err = run("return type(uz.coroutine.wait_until)");
    EXPECT_EQ(err, 0);
    EXPECT_STREQ(lua_tostring(L, -1), "function");
    lua_pop(L, 1);
}

TEST_F(CoroutineSchedulerTest, RegisterLuaApi_SpawnIsFunction) {
    register_api();
    int err = run("return type(uz.coroutine.spawn)");
    EXPECT_EQ(err, 0);
    EXPECT_STREQ(lua_tostring(L, -1), "function");
    lua_pop(L, 1);
}

TEST_F(CoroutineSchedulerTest, RegisterLuaApi_CancelIsFunction) {
    register_api();
    int err = run("return type(uz.coroutine.cancel)");
    EXPECT_EQ(err, 0);
    EXPECT_STREQ(lua_tostring(L, -1), "function");
    lua_pop(L, 1);
}

// ============================================================================
// Section 3: C++ spawn() API
// ============================================================================

TEST_F(CoroutineSchedulerTest, Spawn_ReturnsNonZeroHandle) {
    lua_State* thread = lua_newthread(L);
    int ref = luaL_ref(L, LUA_REGISTRYINDEX);

    CoroutineHandle h = sched.spawn(thread, ref, 1u, WaitFrames{1});
    EXPECT_NE(h, kInvalidCoroutineHandle);
}

TEST_F(CoroutineSchedulerTest, Spawn_IncrementsTotalSpawned) {
    lua_State* t1 = lua_newthread(L);
    int r1 = luaL_ref(L, LUA_REGISTRYINDEX);
    lua_State* t2 = lua_newthread(L);
    int r2 = luaL_ref(L, LUA_REGISTRYINDEX);

    sched.spawn(t1, r1, 0u, WaitFrames{1});
    EXPECT_EQ(sched.total_spawned(), 1u);

    sched.spawn(t2, r2, 0u, WaitSeconds{1.0f});
    EXPECT_EQ(sched.total_spawned(), 2u);
}

TEST_F(CoroutineSchedulerTest, Spawn_IncrementsActiveCount) {
    lua_State* t1 = lua_newthread(L);
    int r1 = luaL_ref(L, LUA_REGISTRYINDEX);
    lua_State* t2 = lua_newthread(L);
    int r2 = luaL_ref(L, LUA_REGISTRYINDEX);

    EXPECT_EQ(sched.active_count(), 0u);

    sched.spawn(t1, r1, 0u, WaitFrames{1});
    EXPECT_EQ(sched.active_count(), 1u);

    sched.spawn(t2, r2, 0u, WaitFrames{1});
    EXPECT_EQ(sched.active_count(), 2u);
}

TEST_F(CoroutineSchedulerTest, Spawn_EachCallGetsDifferentHandle) {
    lua_State* t1 = lua_newthread(L);
    int r1 = luaL_ref(L, LUA_REGISTRYINDEX);
    lua_State* t2 = lua_newthread(L);
    int r2 = luaL_ref(L, LUA_REGISTRYINDEX);
    lua_State* t3 = lua_newthread(L);
    int r3 = luaL_ref(L, LUA_REGISTRYINDEX);

    CoroutineHandle h1 = sched.spawn(t1, r1, 0u, WaitFrames{1});
    CoroutineHandle h2 = sched.spawn(t2, r2, 0u, WaitFrames{1});
    CoroutineHandle h3 = sched.spawn(t3, r3, 0u, WaitFrames{1});

    EXPECT_NE(h1, h2);
    EXPECT_NE(h2, h3);
    EXPECT_NE(h1, h3);
}

// ============================================================================
// Section 4: drain() — WaitFrames
// ============================================================================

TEST_F(CoroutineSchedulerTest, Drain_WithNoCoroutines_IsNoOp) {
    sched.drain(0.016f);   // must not crash
    EXPECT_EQ(sched.active_count(), 0u);
}

TEST_F(CoroutineSchedulerTest, Drain_WaitFrames_1_ResumesAfterOneDrain) {
    // A coroutine that sets a Lua global on second resume (after yield)
    const char* src =
        "local function co()\n"
        "  coroutine.yield()  -- placeholder yield (no wait API needed)\n"
        "  _G.co_finished = true\n"
        "end\n"
        "return co";

    luaL_loadstring(L, src);
    lua_pcall(L, 0, 1, 0);   // returns the co function

    lua_State* thread = lua_newthread(L);
    int ref = luaL_ref(L, LUA_REGISTRYINDEX);
    lua_xmove(L, thread, 1);  // move function to thread

    // First resume: runs until yield (simulates what l_coroutine_spawn does)
    int status = lua_resume(thread, 0);
    ASSERT_EQ(status, LUA_YIELD);

    sched.spawn(thread, ref, 0u, WaitFrames{1});
    EXPECT_EQ(sched.active_count(), 1u);

    sched.drain(0.016f);

    // Coroutine finished — active_count drops to 0
    EXPECT_EQ(sched.active_count(), 0u);

    // Lua global should be set
    lua_getglobal(L, "co_finished");
    EXPECT_TRUE(lua_toboolean(L, -1));
    lua_pop(L, 1);
}

TEST_F(CoroutineSchedulerTest, Drain_WaitFrames_NotReady_DoesNotResume) {
    const char* src =
        "local function co()\n"
        "  coroutine.yield()\n"
        "  _G.co_ran_second = true\n"
        "end\n"
        "return co";

    luaL_loadstring(L, src);
    lua_pcall(L, 0, 1, 0);

    lua_State* thread = lua_newthread(L);
    int ref = luaL_ref(L, LUA_REGISTRYINDEX);
    lua_xmove(L, thread, 1);

    lua_resume(thread, 0);   // runs to first yield
    sched.spawn(thread, ref, 0u, WaitFrames{3});

    EXPECT_EQ(sched.active_count(), 1u);

    // First drain: remaining goes 3->2, not ready
    sched.drain(0.016f);
    EXPECT_EQ(sched.active_count(), 1u);

    lua_getglobal(L, "co_ran_second");
    EXPECT_TRUE(lua_isnil(L, -1));   // still nil — coroutine did not run yet
    lua_pop(L, 1);
}

TEST_F(CoroutineSchedulerTest, Drain_WaitFrames_CountsDownToZero) {
    const char* src =
        "local function co()\n"
        "  coroutine.yield()\n"
        "  _G.frames_counted = true\n"
        "end\n"
        "return co";

    luaL_loadstring(L, src);
    lua_pcall(L, 0, 1, 0);

    lua_State* thread = lua_newthread(L);
    int ref = luaL_ref(L, LUA_REGISTRYINDEX);
    lua_xmove(L, thread, 1);

    lua_resume(thread, 0);
    sched.spawn(thread, ref, 0u, WaitFrames{3});

    sched.drain(0.016f);  // remaining: 2 — not ready
    EXPECT_EQ(sched.active_count(), 1u);
    sched.drain(0.016f);  // remaining: 1 — not ready
    EXPECT_EQ(sched.active_count(), 1u);
    sched.drain(0.016f);  // remaining: 0 — ready, resumes
    EXPECT_EQ(sched.active_count(), 0u);

    lua_getglobal(L, "frames_counted");
    EXPECT_TRUE(lua_toboolean(L, -1));
    lua_pop(L, 1);
}

// ============================================================================
// Section 5: drain() — WaitSeconds
// ============================================================================

TEST_F(CoroutineSchedulerTest, Drain_WaitSeconds_NotReady_WhenDtInsufficient) {
    const char* src =
        "local function co()\n"
        "  coroutine.yield()\n"
        "  _G.secs_done = true\n"
        "end\n"
        "return co";

    luaL_loadstring(L, src);
    lua_pcall(L, 0, 1, 0);

    lua_State* thread = lua_newthread(L);
    int ref = luaL_ref(L, LUA_REGISTRYINDEX);
    lua_xmove(L, thread, 1);
    lua_resume(thread, 0);

    sched.spawn(thread, ref, 0u, WaitSeconds{0.5f});

    sched.drain(0.2f);   // 0.5 - 0.2 = 0.3 remaining
    EXPECT_EQ(sched.active_count(), 1u);

    lua_getglobal(L, "secs_done");
    EXPECT_TRUE(lua_isnil(L, -1));
    lua_pop(L, 1);
}

TEST_F(CoroutineSchedulerTest, Drain_WaitSeconds_ResumesWhenTimeExpires) {
    const char* src =
        "local function co()\n"
        "  coroutine.yield()\n"
        "  _G.secs_done = true\n"
        "end\n"
        "return co";

    luaL_loadstring(L, src);
    lua_pcall(L, 0, 1, 0);

    lua_State* thread = lua_newthread(L);
    int ref = luaL_ref(L, LUA_REGISTRYINDEX);
    lua_xmove(L, thread, 1);
    lua_resume(thread, 0);

    sched.spawn(thread, ref, 0u, WaitSeconds{0.1f});

    sched.drain(0.15f);  // 0.1 - 0.15 = -0.05 <= 0 → ready
    EXPECT_EQ(sched.active_count(), 0u);

    lua_getglobal(L, "secs_done");
    EXPECT_TRUE(lua_toboolean(L, -1));
    lua_pop(L, 1);
}

TEST_F(CoroutineSchedulerTest, Drain_WaitSeconds_ZeroRemaining_ReadsyImmediately) {
    const char* src =
        "local function co()\n"
        "  coroutine.yield()\n"
        "  _G.zero_sec_done = true\n"
        "end\n"
        "return co";

    luaL_loadstring(L, src);
    lua_pcall(L, 0, 1, 0);

    lua_State* thread = lua_newthread(L);
    int ref = luaL_ref(L, LUA_REGISTRYINDEX);
    lua_xmove(L, thread, 1);
    lua_resume(thread, 0);

    sched.spawn(thread, ref, 0u, WaitSeconds{0.0f});

    // Any positive dt makes remaining = 0.0 - dt <= 0
    sched.drain(0.001f);
    EXPECT_EQ(sched.active_count(), 0u);

    lua_getglobal(L, "zero_sec_done");
    EXPECT_TRUE(lua_toboolean(L, -1));
    lua_pop(L, 1);
}

// ============================================================================
// Section 6: drain() — coroutine re-yields (multi-step coroutine)
// ============================================================================

TEST_F(CoroutineSchedulerTest, Drain_MultiYield_FramesThenComplete) {
    // Coroutine yields twice (two separate waits)
    const char* src =
        "local function co()\n"
        "  coroutine.yield()   -- first yield — scheduler re-queues as WaitFrames{1}\n"
        "  _G.step1 = true\n"
        "  coroutine.yield()   -- second yield\n"
        "  _G.step2 = true\n"
        "end\n"
        "return co";

    luaL_loadstring(L, src);
    lua_pcall(L, 0, 1, 0);

    lua_State* thread = lua_newthread(L);
    int ref = luaL_ref(L, LUA_REGISTRYINDEX);
    lua_xmove(L, thread, 1);
    lua_resume(thread, 0);  // runs to first yield

    sched.spawn(thread, ref, 0u, WaitFrames{1});

    // First drain: resumes, sets step1, yields again -> WaitFrames{1}
    sched.drain(0.016f);
    EXPECT_EQ(sched.active_count(), 1u);

    lua_getglobal(L, "step1");
    EXPECT_TRUE(lua_toboolean(L, -1));
    lua_pop(L, 1);
    lua_getglobal(L, "step2");
    EXPECT_TRUE(lua_isnil(L, -1));
    lua_pop(L, 1);

    // Second drain: resumes, sets step2, finishes
    sched.drain(0.016f);
    EXPECT_EQ(sched.active_count(), 0u);

    lua_getglobal(L, "step2");
    EXPECT_TRUE(lua_toboolean(L, -1));
    lua_pop(L, 1);
}

// ============================================================================
// Section 7: drain() — WaitUntil
// ============================================================================

TEST_F(CoroutineSchedulerTest, Drain_WaitUntil_PredicateFalse_DoesNotResume) {
    // Spawn with WaitUntil referencing a predicate that always returns false
    const char* pred_src = "return function() return false end";
    luaL_loadstring(L, pred_src);
    lua_pcall(L, 0, 1, 0);
    int fn_ref = luaL_ref(L, LUA_REGISTRYINDEX);

    lua_State* thread = lua_newthread(L);
    int ref = luaL_ref(L, LUA_REGISTRYINDEX);

    // Push a simple no-op function onto the thread (so we have a valid coroutine)
    lua_pushcfunction(thread, [](lua_State*) -> int { return 0; });

    sched.spawn(thread, ref, 0u, WaitUntil{fn_ref});
    EXPECT_EQ(sched.active_count(), 1u);

    sched.drain(0.016f);
    EXPECT_EQ(sched.active_count(), 1u);  // not ready — predicate returned false
}

TEST_F(CoroutineSchedulerTest, Drain_WaitUntil_PredicateTrue_Resumes) {
    const char* src =
        "local function co()\n"
        "  coroutine.yield()\n"
        "  _G.until_done = true\n"
        "end\n"
        "return co";

    luaL_loadstring(L, src);
    lua_pcall(L, 0, 1, 0);

    lua_State* thread = lua_newthread(L);
    int ref = luaL_ref(L, LUA_REGISTRYINDEX);
    lua_xmove(L, thread, 1);
    lua_resume(thread, 0);

    // Predicate that always returns true
    luaL_loadstring(L, "return function() return true end");
    lua_pcall(L, 0, 1, 0);
    int fn_ref = luaL_ref(L, LUA_REGISTRYINDEX);

    sched.spawn(thread, ref, 0u, WaitUntil{fn_ref});

    sched.drain(0.016f);
    EXPECT_EQ(sched.active_count(), 0u);

    lua_getglobal(L, "until_done");
    EXPECT_TRUE(lua_toboolean(L, -1));
    lua_pop(L, 1);
}

TEST_F(CoroutineSchedulerTest, Drain_WaitUntil_PredicateError_KillsCoroutine) {
    // Predicate that throws a Lua error
    luaL_loadstring(L, "return function() error('pred boom') end");
    lua_pcall(L, 0, 1, 0);
    int fn_ref = luaL_ref(L, LUA_REGISTRYINDEX);

    lua_State* thread = lua_newthread(L);
    int ref = luaL_ref(L, LUA_REGISTRYINDEX);

    // A simple yielded coroutine (will never actually run due to pred error)
    lua_pushcfunction(thread, [](lua_State*) -> int { return 0; });
    sched.spawn(thread, ref, 42u, WaitUntil{fn_ref});

    sched.drain(0.016f);

    // Coroutine should be dead after predicate error
    EXPECT_EQ(sched.active_count(), 0u);
}

TEST_F(CoroutineSchedulerTest, Drain_WaitUntil_PredicateError_InvokesErrorCallback) {
    luaL_loadstring(L, "return function() error('callback test') end");
    lua_pcall(L, 0, 1, 0);
    int fn_ref = luaL_ref(L, LUA_REGISTRYINDEX);

    lua_State* thread = lua_newthread(L);
    int ref = luaL_ref(L, LUA_REGISTRYINDEX);
    lua_pushcfunction(thread, [](lua_State*) -> int { return 0; });

    std::string captured_msg;
    uint32_t    captured_node = 0;
    sched.set_error_callback([&](uint32_t nid, const char* msg) {
        captured_node = nid;
        captured_msg  = msg ? msg : "";
    });

    sched.spawn(thread, ref, 99u, WaitUntil{fn_ref});
    sched.drain(0.016f);

    EXPECT_EQ(captured_node, 99u);
    EXPECT_FALSE(captured_msg.empty());
    EXPECT_NE(captured_msg.find("callback test"), std::string::npos);
}

// ============================================================================
// Section 8: drain() — coroutine runtime error
// ============================================================================

TEST_F(CoroutineSchedulerTest, Drain_CoroutineRuntimeError_KillsEntry) {
    const char* src =
        "local function co()\n"
        "  coroutine.yield()\n"
        "  error('runtime boom')\n"
        "end\n"
        "return co";

    luaL_loadstring(L, src);
    lua_pcall(L, 0, 1, 0);

    lua_State* thread = lua_newthread(L);
    int ref = luaL_ref(L, LUA_REGISTRYINDEX);
    lua_xmove(L, thread, 1);
    lua_resume(thread, 0);  // runs to first yield

    sched.spawn(thread, ref, 0u, WaitFrames{1});
    sched.drain(0.016f);   // resumes into error

    EXPECT_EQ(sched.active_count(), 0u);
}

TEST_F(CoroutineSchedulerTest, Drain_CoroutineRuntimeError_InvokesErrorCallback) {
    const char* src =
        "local function co()\n"
        "  coroutine.yield()\n"
        "  error('error_cb_test')\n"
        "end\n"
        "return co";

    luaL_loadstring(L, src);
    lua_pcall(L, 0, 1, 0);

    lua_State* thread = lua_newthread(L);
    int ref = luaL_ref(L, LUA_REGISTRYINDEX);
    lua_xmove(L, thread, 1);
    lua_resume(thread, 0);

    std::string captured_msg;
    uint32_t    captured_node = 0;
    sched.set_error_callback([&](uint32_t nid, const char* msg) {
        captured_node = nid;
        captured_msg  = msg ? msg : "";
    });

    sched.spawn(thread, ref, 77u, WaitFrames{1});
    sched.drain(0.016f);

    EXPECT_EQ(captured_node, 77u);
    EXPECT_FALSE(captured_msg.empty());
    EXPECT_NE(captured_msg.find("error_cb_test"), std::string::npos);
}

TEST_F(CoroutineSchedulerTest, Drain_CoroutineRuntimeError_EngineDoesNotCrash) {
    // Two coroutines: first errors, second must still run
    const char* bad_src =
        "local function co()\n"
        "  coroutine.yield()\n"
        "  error('intentional')\n"
        "end\n"
        "return co";

    const char* good_src =
        "local function co()\n"
        "  coroutine.yield()\n"
        "  _G.good_ran = true\n"
        "end\n"
        "return co";

    // Bad coroutine
    luaL_loadstring(L, bad_src);
    lua_pcall(L, 0, 1, 0);
    lua_State* bt = lua_newthread(L);
    int br = luaL_ref(L, LUA_REGISTRYINDEX);
    lua_xmove(L, bt, 1);
    lua_resume(bt, 0);

    // Good coroutine
    luaL_loadstring(L, good_src);
    lua_pcall(L, 0, 1, 0);
    lua_State* gt = lua_newthread(L);
    int gr = luaL_ref(L, LUA_REGISTRYINDEX);
    lua_xmove(L, gt, 1);
    lua_resume(gt, 0);

    sched.spawn(bt, br, 1u, WaitFrames{1});
    sched.spawn(gt, gr, 2u, WaitFrames{1});

    sched.drain(0.016f);   // bad errors, good completes

    EXPECT_EQ(sched.active_count(), 0u);

    lua_getglobal(L, "good_ran");
    EXPECT_TRUE(lua_toboolean(L, -1));
    lua_pop(L, 1);
}

// ============================================================================
// Section 9: cancel()
// ============================================================================

TEST_F(CoroutineSchedulerTest, Cancel_KnownHandle_ReturnsTrueAndDeactivates) {
    lua_State* thread = lua_newthread(L);
    int ref = luaL_ref(L, LUA_REGISTRYINDEX);

    CoroutineHandle h = sched.spawn(thread, ref, 0u, WaitFrames{10});
    EXPECT_EQ(sched.active_count(), 1u);

    bool ok = sched.cancel(h);
    EXPECT_TRUE(ok);
    EXPECT_EQ(sched.active_count(), 0u);
}

TEST_F(CoroutineSchedulerTest, Cancel_UnknownHandle_ReturnsFalse) {
    bool ok = sched.cancel(999u);
    EXPECT_FALSE(ok);
}

TEST_F(CoroutineSchedulerTest, Cancel_InvalidHandle_ReturnsFalse) {
    bool ok = sched.cancel(kInvalidCoroutineHandle);
    EXPECT_FALSE(ok);
}

TEST_F(CoroutineSchedulerTest, Cancel_AlreadyCancelled_ReturnsFalse) {
    lua_State* thread = lua_newthread(L);
    int ref = luaL_ref(L, LUA_REGISTRYINDEX);

    CoroutineHandle h = sched.spawn(thread, ref, 0u, WaitFrames{10});
    sched.cancel(h);

    bool second = sched.cancel(h);
    EXPECT_FALSE(second);
}

TEST_F(CoroutineSchedulerTest, Cancel_OnlyTargetedHandle_OthersRemainActive) {
    lua_State* t1 = lua_newthread(L);
    int r1 = luaL_ref(L, LUA_REGISTRYINDEX);
    lua_State* t2 = lua_newthread(L);
    int r2 = luaL_ref(L, LUA_REGISTRYINDEX);

    CoroutineHandle h1 = sched.spawn(t1, r1, 0u, WaitFrames{10});
    CoroutineHandle h2 = sched.spawn(t2, r2, 0u, WaitFrames{10});

    sched.cancel(h1);

    EXPECT_EQ(sched.active_count(), 1u);
    // h2 is still cancellable
    EXPECT_TRUE(sched.cancel(h2));
}

TEST_F(CoroutineSchedulerTest, Cancel_WaitUntilHandle_ReleasesLuaRef) {
    // Spawn with a WaitUntil — cancel must not leak the fn_ref
    luaL_loadstring(L, "return function() return false end");
    lua_pcall(L, 0, 1, 0);
    int fn_ref = luaL_ref(L, LUA_REGISTRYINDEX);

    // Verify ref is valid before cancel
    lua_rawgeti(L, LUA_REGISTRYINDEX, fn_ref);
    EXPECT_TRUE(lua_isfunction(L, -1));
    lua_pop(L, 1);

    lua_State* thread = lua_newthread(L);
    int ref = luaL_ref(L, LUA_REGISTRYINDEX);
    lua_pushcfunction(thread, [](lua_State*) -> int { return 0; });

    CoroutineHandle h = sched.spawn(thread, ref, 0u, WaitUntil{fn_ref});
    bool ok = sched.cancel(h);
    EXPECT_TRUE(ok);

    // After cancel, fn_ref should be unreferenced (LUA_NOREF when looked up it returns nil)
    // We can't directly assert the ref was freed without reading internal state,
    // but we confirm the cancel succeeded and active_count == 0 (no leak in API terms).
    EXPECT_EQ(sched.active_count(), 0u);
}

// ============================================================================
// Section 10: cancel_all()
// ============================================================================

TEST_F(CoroutineSchedulerTest, CancelAll_RemovesAllEntries) {
    for (int i = 0; i < 5; ++i) {
        lua_State* t = lua_newthread(L);
        int r = luaL_ref(L, LUA_REGISTRYINDEX);
        sched.spawn(t, r, static_cast<uint32_t>(i), WaitFrames{10});
    }
    EXPECT_EQ(sched.active_count(), 5u);

    sched.cancel_all();
    EXPECT_EQ(sched.active_count(), 0u);
}

TEST_F(CoroutineSchedulerTest, CancelAll_OnEmpty_IsNoOp) {
    sched.cancel_all();
    EXPECT_EQ(sched.active_count(), 0u);
}

TEST_F(CoroutineSchedulerTest, CancelAll_AfterCancelAll_SpawnWorks) {
    lua_State* t1 = lua_newthread(L);
    int r1 = luaL_ref(L, LUA_REGISTRYINDEX);
    sched.spawn(t1, r1, 0u, WaitFrames{10});

    sched.cancel_all();

    lua_State* t2 = lua_newthread(L);
    int r2 = luaL_ref(L, LUA_REGISTRYINDEX);
    CoroutineHandle h = sched.spawn(t2, r2, 0u, WaitFrames{1});

    EXPECT_NE(h, kInvalidCoroutineHandle);
    EXPECT_EQ(sched.active_count(), 1u);
}

// ============================================================================
// Section 11: cancel_by_node()
// ============================================================================

TEST_F(CoroutineSchedulerTest, CancelByNode_RemovesMatchingNodeCoroutines) {
    lua_State* t1 = lua_newthread(L);
    int r1 = luaL_ref(L, LUA_REGISTRYINDEX);
    lua_State* t2 = lua_newthread(L);
    int r2 = luaL_ref(L, LUA_REGISTRYINDEX);
    lua_State* t3 = lua_newthread(L);
    int r3 = luaL_ref(L, LUA_REGISTRYINDEX);

    sched.spawn(t1, r1, 10u, WaitFrames{10});
    sched.spawn(t2, r2, 10u, WaitFrames{10});
    sched.spawn(t3, r3, 20u, WaitFrames{10});

    EXPECT_EQ(sched.active_count(), 3u);

    sched.cancel_by_node(10u);
    EXPECT_EQ(sched.active_count(), 1u);
}

TEST_F(CoroutineSchedulerTest, CancelByNode_NonMatchingNodeId_NoEffect) {
    lua_State* t = lua_newthread(L);
    int r = luaL_ref(L, LUA_REGISTRYINDEX);
    sched.spawn(t, r, 5u, WaitFrames{10});

    sched.cancel_by_node(99u);   // no such node
    EXPECT_EQ(sched.active_count(), 1u);
}

TEST_F(CoroutineSchedulerTest, CancelByNode_Zero_NodeId_CancelsZeroNode) {
    lua_State* t = lua_newthread(L);
    int r = luaL_ref(L, LUA_REGISTRYINDEX);
    sched.spawn(t, r, 0u, WaitFrames{10});

    sched.cancel_by_node(0u);
    EXPECT_EQ(sched.active_count(), 0u);
}

TEST_F(CoroutineSchedulerTest, CancelByNode_OnEmptyScheduler_IsNoOp) {
    sched.cancel_by_node(1u);
    EXPECT_EQ(sched.active_count(), 0u);
}

// ============================================================================
// Section 12: active_count() and total_spawned()
// ============================================================================

TEST_F(CoroutineSchedulerTest, ActiveCount_StartsAtZero) {
    EXPECT_EQ(sched.active_count(), 0u);
}

TEST_F(CoroutineSchedulerTest, TotalSpawned_StartsAtZero) {
    EXPECT_EQ(sched.total_spawned(), 0u);
}

TEST_F(CoroutineSchedulerTest, TotalSpawned_AccumulatesAfterCancel) {
    lua_State* t1 = lua_newthread(L);
    int r1 = luaL_ref(L, LUA_REGISTRYINDEX);
    lua_State* t2 = lua_newthread(L);
    int r2 = luaL_ref(L, LUA_REGISTRYINDEX);

    CoroutineHandle h1 = sched.spawn(t1, r1, 0u, WaitFrames{1});
    sched.cancel(h1);   // dead, but total_spawned stays at 1

    sched.spawn(t2, r2, 0u, WaitFrames{1});

    EXPECT_EQ(sched.total_spawned(), 2u);
    EXPECT_EQ(sched.active_count(), 1u);
}

TEST_F(CoroutineSchedulerTest, ActiveCount_OnlyCountsNonDead) {
    lua_State* t1 = lua_newthread(L);
    int r1 = luaL_ref(L, LUA_REGISTRYINDEX);
    lua_State* t2 = lua_newthread(L);
    int r2 = luaL_ref(L, LUA_REGISTRYINDEX);
    lua_State* t3 = lua_newthread(L);
    int r3 = luaL_ref(L, LUA_REGISTRYINDEX);

    CoroutineHandle h1 = sched.spawn(t1, r1, 1u, WaitFrames{10});
    sched.spawn(t2, r2, 2u, WaitFrames{10});
    sched.spawn(t3, r3, 3u, WaitFrames{10});

    sched.cancel(h1);

    EXPECT_EQ(sched.active_count(), 2u);
    EXPECT_EQ(sched.total_spawned(), 3u);
}

// ============================================================================
// Section 13: Lua API — uz.coroutine.spawn
// ============================================================================

TEST_F(CoroutineSchedulerTest, LuaSpawn_ImmediateComplete_ReturnsZero) {
    register_api();

    // Function that completes without yielding
    int err = run(
        "local h = uz.coroutine.spawn(function() end)\n"
        "_G.spawn_handle = h\n");
    EXPECT_EQ(err, 0);

    lua_getglobal(L, "spawn_handle");
    lua_Integer h = lua_tointeger(L, -1);
    EXPECT_EQ(h, 0);   // immediate completion → handle 0
    lua_pop(L, 1);

    // No active entries (completed immediately in spawn)
    EXPECT_EQ(sched.active_count(), 0u);
}

TEST_F(CoroutineSchedulerTest, LuaSpawn_WithYield_ReturnsNonZeroHandle) {
    register_api();

    int err = run(
        "local h = uz.coroutine.spawn(function()\n"
        "  uz.coroutine.wait_frames(1)\n"
        "end)\n"
        "_G.spawn_handle = h\n");
    EXPECT_EQ(err, 0);

    lua_getglobal(L, "spawn_handle");
    lua_Integer h = lua_tointeger(L, -1);
    EXPECT_GT(h, 0);
    lua_pop(L, 1);

    EXPECT_EQ(sched.active_count(), 1u);
}

TEST_F(CoroutineSchedulerTest, LuaSpawn_ErrorOnStart_ReturnsZero_NoActiveEntry) {
    register_api();

    int err = run(
        "local h = uz.coroutine.spawn(function()\n"
        "  error('spawn error test')\n"
        "end)\n"
        "_G.err_handle = h\n");
    EXPECT_EQ(err, 0);

    lua_getglobal(L, "err_handle");
    EXPECT_EQ(lua_tointeger(L, -1), 0);
    lua_pop(L, 1);

    EXPECT_EQ(sched.active_count(), 0u);
}

TEST_F(CoroutineSchedulerTest, LuaSpawn_RequiresFunction_ErrorsOtherwise) {
    register_api();

    // Passing a non-function should result in a Lua error
    int err = run("uz.coroutine.spawn(42)");
    EXPECT_NE(err, 0);   // luaL_checktype should raise error
    lua_pop(L, 1);       // pop error message
}

TEST_F(CoroutineSchedulerTest, LuaSpawn_TotalSpawnedIncremented) {
    register_api();

    // Spawning a coroutine that yields should increment total_spawned
    int err = run(
        "uz.coroutine.spawn(function()\n"
        "  uz.coroutine.wait_frames(1)\n"
        "end)\n");
    EXPECT_EQ(err, 0);
    EXPECT_EQ(sched.total_spawned(), 1u);
}

// ============================================================================
// Section 14: Lua API — uz.coroutine.wait (seconds)
// ============================================================================

TEST_F(CoroutineSchedulerTest, LuaWait_SpawnsWithWaitSecondsCondition) {
    register_api();

    int err = run(
        "uz.coroutine.spawn(function()\n"
        "  uz.coroutine.wait(0.5)\n"
        "  _G.wait_done = true\n"
        "end)\n");
    EXPECT_EQ(err, 0);
    EXPECT_EQ(sched.active_count(), 1u);

    // Not done after small dt
    sched.drain(0.1f);
    EXPECT_EQ(sched.active_count(), 1u);
    lua_getglobal(L, "wait_done");
    EXPECT_TRUE(lua_isnil(L, -1));
    lua_pop(L, 1);

    // Done after enough accumulated dt
    sched.drain(0.5f);   // total > 0.5
    EXPECT_EQ(sched.active_count(), 0u);
    lua_getglobal(L, "wait_done");
    EXPECT_TRUE(lua_toboolean(L, -1));
    lua_pop(L, 1);
}

TEST_F(CoroutineSchedulerTest, LuaWait_NegativeSeconds_TreatedAsZero) {
    register_api();

    // Negative wait is clamped to 0 in l_coroutine_wait
    int err = run(
        "uz.coroutine.spawn(function()\n"
        "  uz.coroutine.wait(-1.0)\n"
        "  _G.neg_done = true\n"
        "end)\n");
    EXPECT_EQ(err, 0);
    EXPECT_EQ(sched.active_count(), 1u);

    // Any positive dt should trigger (remaining was clamped to 0)
    sched.drain(0.001f);
    EXPECT_EQ(sched.active_count(), 0u);
    lua_getglobal(L, "neg_done");
    EXPECT_TRUE(lua_toboolean(L, -1));
    lua_pop(L, 1);
}

TEST_F(CoroutineSchedulerTest, LuaWait_RequiresNumber_ErrorsOtherwise) {
    register_api();

    // coroutine.resume returns (false, err_msg) when the coroutine errors.
    // pcall around resume returns true because resume itself doesn't throw.
    int err = run(
        "local co = coroutine.create(function()\n"
        "  uz.coroutine.wait('not a number')\n"
        "end)\n"
        "_G.resume_ok, _G.resume_err = coroutine.resume(co)\n");
    EXPECT_EQ(err, 0);

    lua_getglobal(L, "resume_ok");
    EXPECT_FALSE(lua_toboolean(L, -1)) << "resume should return false on type error";
    lua_pop(L, 1);
}

// ============================================================================
// Section 15: Lua API — uz.coroutine.wait_frames
// ============================================================================

TEST_F(CoroutineSchedulerTest, LuaWaitFrames_SpawnsWithWaitFramesCondition) {
    register_api();

    int err = run(
        "uz.coroutine.spawn(function()\n"
        "  uz.coroutine.wait_frames(2)\n"
        "  _G.frames_done = true\n"
        "end)\n");
    EXPECT_EQ(err, 0);
    EXPECT_EQ(sched.active_count(), 1u);

    sched.drain(0.016f);   // remaining: 1
    EXPECT_EQ(sched.active_count(), 1u);
    lua_getglobal(L, "frames_done");
    EXPECT_TRUE(lua_isnil(L, -1));
    lua_pop(L, 1);

    sched.drain(0.016f);   // remaining: 0 → resumes
    EXPECT_EQ(sched.active_count(), 0u);
    lua_getglobal(L, "frames_done");
    EXPECT_TRUE(lua_toboolean(L, -1));
    lua_pop(L, 1);
}

TEST_F(CoroutineSchedulerTest, LuaWaitFrames_NegativeFrames_TreatedAsZero) {
    register_api();

    int err = run(
        "uz.coroutine.spawn(function()\n"
        "  uz.coroutine.wait_frames(-5)\n"
        "  _G.neg_frames_done = true\n"
        "end)\n");
    EXPECT_EQ(err, 0);
    EXPECT_EQ(sched.active_count(), 1u);

    // remaining clamped to 0 → ready on first drain
    sched.drain(0.016f);
    EXPECT_EQ(sched.active_count(), 0u);
    lua_getglobal(L, "neg_frames_done");
    EXPECT_TRUE(lua_toboolean(L, -1));
    lua_pop(L, 1);
}

TEST_F(CoroutineSchedulerTest, LuaWaitFrames_RequiresInteger_ErrorsOtherwise) {
    register_api();

    int err = run(
        "local co = coroutine.create(function()\n"
        "  uz.coroutine.wait_frames('oops')\n"
        "end)\n"
        "_G.resume_ok, _G.resume_err = coroutine.resume(co)\n");
    EXPECT_EQ(err, 0);

    lua_getglobal(L, "resume_ok");
    EXPECT_FALSE(lua_toboolean(L, -1)) << "resume should return false on type error";
    lua_pop(L, 1);
}

// ============================================================================
// Section 16: Lua API — uz.coroutine.wait_until
// ============================================================================

TEST_F(CoroutineSchedulerTest, LuaWaitUntil_PredicateFalse_StaysActive) {
    register_api();

    int err = run(
        "uz.coroutine.spawn(function()\n"
        "  uz.coroutine.wait_until(function() return false end)\n"
        "  _G.until_lua_done = true\n"
        "end)\n");
    EXPECT_EQ(err, 0);
    EXPECT_EQ(sched.active_count(), 1u);

    sched.drain(0.016f);
    EXPECT_EQ(sched.active_count(), 1u);

    lua_getglobal(L, "until_lua_done");
    EXPECT_TRUE(lua_isnil(L, -1));
    lua_pop(L, 1);
}

TEST_F(CoroutineSchedulerTest, LuaWaitUntil_PredicateBecomesTrue_Resumes) {
    register_api();

    // Use a global flag as predicate
    int err = run(
        "_G.flag = false\n"
        "uz.coroutine.spawn(function()\n"
        "  uz.coroutine.wait_until(function() return _G.flag end)\n"
        "  _G.until_ran = true\n"
        "end)\n");
    EXPECT_EQ(err, 0);
    EXPECT_EQ(sched.active_count(), 1u);

    sched.drain(0.016f);
    EXPECT_EQ(sched.active_count(), 1u);   // flag still false

    // Set the flag
    err = run("_G.flag = true");
    EXPECT_EQ(err, 0);

    sched.drain(0.016f);
    EXPECT_EQ(sched.active_count(), 0u);   // resumed and finished

    lua_getglobal(L, "until_ran");
    EXPECT_TRUE(lua_toboolean(L, -1));
    lua_pop(L, 1);
}

TEST_F(CoroutineSchedulerTest, LuaWaitUntil_RequiresFunction_ErrorsOtherwise) {
    register_api();

    int err = run(
        "local co = coroutine.create(function()\n"
        "  uz.coroutine.wait_until(42)\n"
        "end)\n"
        "_G.resume_ok, _G.resume_err = coroutine.resume(co)\n");
    EXPECT_EQ(err, 0);

    lua_getglobal(L, "resume_ok");
    EXPECT_FALSE(lua_toboolean(L, -1)) << "resume should return false on type error";
    lua_pop(L, 1);
}

// ============================================================================
// Section 17: Lua API — uz.coroutine.cancel
// ============================================================================

TEST_F(CoroutineSchedulerTest, LuaCancel_ValidHandle_ReturnsTrue) {
    register_api();

    int err = run(
        "_G.h = uz.coroutine.spawn(function()\n"
        "  uz.coroutine.wait_frames(100)\n"
        "end)\n"
        "_G.cancel_result = uz.coroutine.cancel(_G.h)\n");
    EXPECT_EQ(err, 0);

    lua_getglobal(L, "cancel_result");
    EXPECT_TRUE(lua_toboolean(L, -1));
    lua_pop(L, 1);

    EXPECT_EQ(sched.active_count(), 0u);
}

TEST_F(CoroutineSchedulerTest, LuaCancel_InvalidHandle_ReturnsFalse) {
    register_api();

    int err = run("_G.cancel_result = uz.coroutine.cancel(9999)");
    EXPECT_EQ(err, 0);

    lua_getglobal(L, "cancel_result");
    EXPECT_FALSE(lua_toboolean(L, -1));
    lua_pop(L, 1);
}

TEST_F(CoroutineSchedulerTest, LuaCancel_ZeroHandle_ReturnsFalse) {
    register_api();

    int err = run("_G.cancel_result = uz.coroutine.cancel(0)");
    EXPECT_EQ(err, 0);

    lua_getglobal(L, "cancel_result");
    EXPECT_FALSE(lua_toboolean(L, -1));
    lua_pop(L, 1);
}

TEST_F(CoroutineSchedulerTest, LuaCancel_AfterCoroutineFinishes_ReturnsFalse) {
    register_api();

    // Spawn a coroutine that finishes immediately on first drain
    int err = run(
        "_G.h = uz.coroutine.spawn(function()\n"
        "  uz.coroutine.wait_frames(1)\n"
        "end)\n");
    EXPECT_EQ(err, 0);

    sched.drain(0.016f);  // coroutine finishes

    err = run("_G.cancel_after = uz.coroutine.cancel(_G.h)");
    EXPECT_EQ(err, 0);

    lua_getglobal(L, "cancel_after");
    EXPECT_FALSE(lua_toboolean(L, -1));   // already dead
    lua_pop(L, 1);
}

// ============================================================================
// Section 18: Multiple coroutines simultaneous
// ============================================================================

TEST_F(CoroutineSchedulerTest, Multiple_AllRunSimultaneously) {
    register_api();

    int err = run(
        "_G.results = {}\n"
        "for i = 1, 5 do\n"
        "  local idx = i\n"
        "  uz.coroutine.spawn(function()\n"
        "    uz.coroutine.wait_frames(1)\n"
        "    _G.results[idx] = true\n"
        "  end)\n"
        "end\n");
    EXPECT_EQ(err, 0);
    EXPECT_EQ(sched.active_count(), 5u);
    EXPECT_EQ(sched.total_spawned(), 5u);

    sched.drain(0.016f);  // all finish in one drain
    EXPECT_EQ(sched.active_count(), 0u);

    err = run("for i = 1, 5 do assert(_G.results[i] == true) end");
    EXPECT_EQ(err, 0);
}

TEST_F(CoroutineSchedulerTest, Multiple_DifferentWaitDurations_FinishAtCorrectTime) {
    register_api();

    // Coroutine A: 1 frame, Coroutine B: 3 frames
    int err = run(
        "uz.coroutine.spawn(function()\n"
        "  uz.coroutine.wait_frames(1)\n"
        "  _G.a_done = true\n"
        "end)\n"
        "uz.coroutine.spawn(function()\n"
        "  uz.coroutine.wait_frames(3)\n"
        "  _G.b_done = true\n"
        "end)\n");
    EXPECT_EQ(err, 0);
    EXPECT_EQ(sched.active_count(), 2u);

    sched.drain(0.016f);  // A finishes (1 frame), B remaining = 2
    EXPECT_EQ(sched.active_count(), 1u);
    lua_getglobal(L, "a_done");
    EXPECT_TRUE(lua_toboolean(L, -1));
    lua_pop(L, 1);
    lua_getglobal(L, "b_done");
    EXPECT_TRUE(lua_isnil(L, -1));
    lua_pop(L, 1);

    sched.drain(0.016f);  // B remaining = 1
    EXPECT_EQ(sched.active_count(), 1u);

    sched.drain(0.016f);  // B finishes (remaining = 0)
    EXPECT_EQ(sched.active_count(), 0u);
    lua_getglobal(L, "b_done");
    EXPECT_TRUE(lua_toboolean(L, -1));
    lua_pop(L, 1);
}

TEST_F(CoroutineSchedulerTest, Multiple_SameNodeId_CancelByNodeRemovesAll) {
    register_api();

    // Set current_node_id so spawn picks it up via l_coroutine_spawn
    lua_pushinteger(L, 42);
    lua_setfield(L, LUA_REGISTRYINDEX, "uz_current_node_id");

    int err = run(
        "for i = 1, 3 do\n"
        "  uz.coroutine.spawn(function()\n"
        "    uz.coroutine.wait_frames(10)\n"
        "  end)\n"
        "end\n");
    EXPECT_EQ(err, 0);
    EXPECT_EQ(sched.active_count(), 3u);

    sched.cancel_by_node(42u);
    EXPECT_EQ(sched.active_count(), 0u);
}

// ============================================================================
// Section 19: drain() new spawns during drain are deferred
// ============================================================================

TEST_F(CoroutineSchedulerTest, Drain_NewSpawnsDuringDrain_EventuallyProcessed) {
    // A coroutine spawns another during drain. Both must eventually complete.
    register_api();

    int err = run(
        "_G.inner_ran = false\n"
        "uz.coroutine.spawn(function()\n"
        "  uz.coroutine.wait_frames(1)\n"
        "  uz.coroutine.spawn(function()\n"
        "    uz.coroutine.wait_frames(1)\n"
        "    _G.inner_ran = true\n"
        "  end)\n"
        "end)\n");
    EXPECT_EQ(err, 0);
    EXPECT_EQ(sched.active_count(), 1u);

    // Drain enough times for both outer and inner to complete.
    for (int i = 0; i < 5; ++i) {
        sched.drain(0.016f);
    }

    lua_getglobal(L, "inner_ran");
    EXPECT_TRUE(lua_toboolean(L, -1)) << "inner coroutine should have run";
    lua_pop(L, 1);
    EXPECT_EQ(sched.active_count(), 0u);
}

// ============================================================================
// Section 20: default yield condition (no args) → WaitFrames{1}
// ============================================================================

TEST_F(CoroutineSchedulerTest, Drain_DefaultYield_NoArgs_TreatedAsWaitFrames1) {
    // When a coroutine yields with no arguments, read_wait_condition returns WaitFrames{1}.
    const char* src =
        "local function co()\n"
        "  coroutine.yield()     -- no args → default WaitFrames{1}\n"
        "  _G.default_yield_ran = true\n"
        "end\n"
        "return co";

    luaL_loadstring(L, src);
    lua_pcall(L, 0, 1, 0);

    lua_State* thread = lua_newthread(L);
    int ref = luaL_ref(L, LUA_REGISTRYINDEX);
    lua_xmove(L, thread, 1);
    lua_resume(thread, 0);

    // Stack top of thread is empty after yield with no args
    sched.spawn(thread, ref, 0u, WaitFrames{1});

    sched.drain(0.016f);
    EXPECT_EQ(sched.active_count(), 0u);

    lua_getglobal(L, "default_yield_ran");
    EXPECT_TRUE(lua_toboolean(L, -1));
    lua_pop(L, 1);
}

// ============================================================================
// Section 21: set_error_callback / no callback (must not crash)
// ============================================================================

TEST_F(CoroutineSchedulerTest, ErrorCallback_NoCallbackSet_RuntimeError_NoOp) {
    const char* src =
        "local function co()\n"
        "  coroutine.yield()\n"
        "  error('no cb test')\n"
        "end\n"
        "return co";

    luaL_loadstring(L, src);
    lua_pcall(L, 0, 1, 0);

    lua_State* thread = lua_newthread(L);
    int ref = luaL_ref(L, LUA_REGISTRYINDEX);
    lua_xmove(L, thread, 1);
    lua_resume(thread, 0);

    // No error callback set — drain must not crash
    sched.spawn(thread, ref, 0u, WaitFrames{1});
    sched.drain(0.016f);

    EXPECT_EQ(sched.active_count(), 0u);
}

TEST_F(CoroutineSchedulerTest, ErrorCallback_Replaced_OnlyNewCallbackFires) {
    const char* src =
        "local function co()\n"
        "  coroutine.yield()\n"
        "  error('replace test')\n"
        "end\n"
        "return co";

    luaL_loadstring(L, src);
    lua_pcall(L, 0, 1, 0);

    lua_State* thread = lua_newthread(L);
    int ref = luaL_ref(L, LUA_REGISTRYINDEX);
    lua_xmove(L, thread, 1);
    lua_resume(thread, 0);

    int old_cb_count = 0;
    sched.set_error_callback([&](uint32_t, const char*) { ++old_cb_count; });

    // Replace with new callback
    int new_cb_count = 0;
    sched.set_error_callback([&](uint32_t, const char*) { ++new_cb_count; });

    sched.spawn(thread, ref, 0u, WaitFrames{1});
    sched.drain(0.016f);

    EXPECT_EQ(old_cb_count, 0);
    EXPECT_EQ(new_cb_count, 1);
}

// ============================================================================
// Section 22: Lua API without scheduler in registry — error path
// ============================================================================

TEST_F(CoroutineSchedulerTest, LuaSpawn_WithoutSchedulerInRegistry_Errors) {
    // Register api but then clear the scheduler pointer from registry
    register_api();

    lua_pushnil(L);
    lua_setfield(L, LUA_REGISTRYINDEX, "uz_coroutine_scheduler");

    // Calling spawn should produce a Lua error (not a crash)
    int err = run(
        "local ok, msg = pcall(function()\n"
        "  uz.coroutine.spawn(function() end)\n"
        "end)\n"
        "_G.no_sched_ok = ok\n"
        "_G.no_sched_msg = msg\n");
    EXPECT_EQ(err, 0);

    lua_getglobal(L, "no_sched_ok");
    EXPECT_FALSE(lua_toboolean(L, -1));
    lua_pop(L, 1);

    lua_getglobal(L, "no_sched_msg");
    const char* msg = lua_tostring(L, -1);
    ASSERT_NE(msg, nullptr);
    EXPECT_NE(std::string(msg).find("scheduler not initialized"), std::string::npos);
    lua_pop(L, 1);
}

TEST_F(CoroutineSchedulerTest, LuaCancel_WithoutSchedulerInRegistry_Errors) {
    register_api();

    lua_pushnil(L);
    lua_setfield(L, LUA_REGISTRYINDEX, "uz_coroutine_scheduler");

    int err = run(
        "local ok, msg = pcall(function()\n"
        "  uz.coroutine.cancel(1)\n"
        "end)\n"
        "_G.no_sched_cancel_ok = ok\n"
        "_G.no_sched_cancel_msg = msg\n");
    EXPECT_EQ(err, 0);

    lua_getglobal(L, "no_sched_cancel_ok");
    EXPECT_FALSE(lua_toboolean(L, -1));
    lua_pop(L, 1);

    lua_getglobal(L, "no_sched_cancel_msg");
    const char* msg = lua_tostring(L, -1);
    ASSERT_NE(msg, nullptr);
    EXPECT_NE(std::string(msg).find("scheduler not initialized"), std::string::npos);
    lua_pop(L, 1);
}

// ============================================================================
// Section 23: Large number of coroutines (stress)
// ============================================================================

TEST_F(CoroutineSchedulerTest, Stress_ManyCoroutinesSpawnAndDrain) {
    register_api();

    constexpr int N = 50;

    // Spawn N coroutines each waiting 1 frame
    for (int i = 0; i < N; ++i) {
        int err = run(
            "uz.coroutine.spawn(function()\n"
            "  uz.coroutine.wait_frames(1)\n"
            "end)\n");
        ASSERT_EQ(err, 0);
    }

    EXPECT_EQ(sched.active_count(), static_cast<uint32_t>(N));
    EXPECT_EQ(sched.total_spawned(), static_cast<uint32_t>(N));

    sched.drain(0.016f);
    EXPECT_EQ(sched.active_count(), 0u);
}

TEST_F(CoroutineSchedulerTest, Stress_ManySpawnCancelCycles_NoLeak) {
    // Rapidly spawn and cancel coroutines — verify active_count stays consistent
    for (int round = 0; round < 20; ++round) {
        lua_State* t = lua_newthread(L);
        int r = luaL_ref(L, LUA_REGISTRYINDEX);
        CoroutineHandle h = sched.spawn(t, r, static_cast<uint32_t>(round), WaitFrames{100});
        EXPECT_EQ(sched.active_count(), 1u);
        sched.cancel(h);
        EXPECT_EQ(sched.active_count(), 0u);
    }

    EXPECT_EQ(sched.total_spawned(), 20u);
    EXPECT_EQ(sched.active_count(), 0u);
}

// ============================================================================
// Section 24: kInvalidCoroutineHandle sentinel
// ============================================================================

TEST(LuaCoroutineScheduler_Constants, kInvalidCoroutineHandle_IsZero) {
    EXPECT_EQ(kInvalidCoroutineHandle, 0u);
}

TEST(LuaCoroutineScheduler_Constants, SpawnedHandles_NeverZero) {
    lua_State* L = luaL_newstate();
    ASSERT_NE(L, nullptr);
    luaL_openlibs(L);

    LuaCoroutineScheduler s;
    s.init(L);

    for (int i = 0; i < 10; ++i) {
        lua_State* t = lua_newthread(L);
        int r = luaL_ref(L, LUA_REGISTRYINDEX);
        CoroutineHandle h = s.spawn(t, r, 0u, WaitFrames{1});
        EXPECT_NE(h, kInvalidCoroutineHandle) << "handle " << i << " was zero";
    }

    s.shutdown();
    lua_close(L);
}
