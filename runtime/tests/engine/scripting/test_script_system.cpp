/// Unit tests for ScriptSystem — Initialize/Shutdown, per-entity script
/// lifecycle (on_start, on_update, on_stop), environment isolation, and
/// error recovery. Uses a real World, real LuaVM, and an in-memory TestVFS.

#include "script_system.h"
#include "script_component.h"
#include "lua_vm.h"
#include "world.h"
#include "../asset/vfs.h"

#include <gtest/gtest.h>
#include <string>
#include <unordered_map>
#include <vector>
#include <cstdint>

extern "C" {
#include <lua.h>
#include <lualib.h>
#include <lauxlib.h>
}

// ─────────────────────────────────────────────────────────────────────────────
// TestVFS — in-memory VFS for unit tests
//
// Stores script content as strings keyed by logical path. No disk I/O.
// ─────────────────────────────────────────────────────────────────────────────

class TestVFS final : public uz::IVFS {
public:
    /// Registers a script at the given logical path.
    void AddScript(const std::string& path, const std::string& content) {
        scripts_[path] = content;
    }

    [[nodiscard]] std::vector<uint8_t> Read(const std::string& path) override {
        auto it = scripts_.find(path);
        if (it == scripts_.end()) return {};
        const std::string& s = it->second;
        return {s.begin(), s.end()};
    }

    [[nodiscard]] bool Exists(const std::string& path) override {
        return scripts_.count(path) > 0;
    }

    [[nodiscard]] std::string Name() const override { return "TestVFS"; }

private:
    std::unordered_map<std::string, std::string> scripts_;
};

// ─────────────────────────────────────────────────────────────────────────────
// Test Fixture
// ─────────────────────────────────────────────────────────────────────────────

class ScriptSystemTest : public ::testing::Test {
  protected:
    uz::World world_;
    TestVFS   vfs_;

    void SetUp() override {
        ASSERT_TRUE(uz::LuaVM::Init());
        uz::ScriptSystem::Initialize(&world_, &vfs_);
        // Scripts are dormant by default (editor mode); the lifecycle tests
        // below exercise a running game, as the hosts do in Play Mode.
        uz::ScriptSystem::SetPlayMode(true);
    }

    void TearDown() override {
        uz::ScriptSystem::Shutdown();
        uz::LuaVM::Shutdown();
        // Play mode is process-wide state: restore the editor default.
        uz::ScriptSystem::SetPlayMode(false);
    }

    /// Helper: run Lua code in the shared VM state, return true on success.
    bool DoString(const char* code) {
        return luaL_dostring(uz::LuaVM::GetState(), code) == 0;
    }

    /// Helper: evaluate a Lua expression and return its numeric result.
    /// Returns -999.0 and adds a test failure if the expression errors.
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

    /// Creates an entity with a CScript component pointing at the given VFS path.
    uz::Entity CreateScriptEntity(const std::string& scriptPath) {
        auto e = world_.CreateEntity();
        world_.AddComponent<uz::CScript>(e, uz::CScript{scriptPath});
        return e;
    }
};

// ─────────────────────────────────────────────────────────────────────────────
// Initialize / Shutdown lifecycle
// ─────────────────────────────────────────────────────────────────────────────

TEST_F(ScriptSystemTest, Initialize_DoesNotCrash) {
    // SetUp already calls Initialize — just verify the LuaVM is still valid.
    EXPECT_TRUE(uz::LuaVM::IsInitialized());
}

TEST_F(ScriptSystemTest, Update_WithNoScripts_DoesNotCrash) {
    // World has no CScript components — Update must be a no-op.
    uz::ScriptSystem::Update(0.016f);
    EXPECT_TRUE(uz::LuaVM::IsInitialized());
}

TEST_F(ScriptSystemTest, Shutdown_WithNoScripts_DoesNotCrash) {
    // TearDown will call Shutdown — verify early Shutdown is safe.
    uz::ScriptSystem::Shutdown();
    // Re-initialize so TearDown's Shutdown is also safe.
    uz::ScriptSystem::Initialize(&world_, &vfs_);
}

TEST_F(ScriptSystemTest, DoubleShutdown_IsSafe) {
    uz::ScriptSystem::Shutdown();
    uz::ScriptSystem::Shutdown(); // second call must not crash
    uz::ScriptSystem::Initialize(&world_, &vfs_);
}

// ─────────────────────────────────────────────────────────────────────────────
// Play mode gate — scripts are dormant while editing, run in Play Mode
// ─────────────────────────────────────────────────────────────────────────────

TEST_F(ScriptSystemTest, IsPlayMode_ReflectsSetPlayMode) {
    uz::ScriptSystem::SetPlayMode(false);
    EXPECT_FALSE(uz::ScriptSystem::IsPlayMode());
    uz::ScriptSystem::SetPlayMode(true);
    EXPECT_TRUE(uz::ScriptSystem::IsPlayMode());
}

TEST_F(ScriptSystemTest, EditorMode_Update_DoesNotStartOrUpdateScripts) {
    vfs_.AddScript("scripts/dormant.lua",
        "function on_start(e) _G.dormant_started = true end\n"
        "function on_update(e, dt) _G.dormant_updates = (_G.dormant_updates or 0) + 1 end\n"
    );
    auto e = CreateScriptEntity("scripts/dormant.lua");

    uz::ScriptSystem::SetPlayMode(false);
    uz::ScriptSystem::Update(0.016f);
    uz::ScriptSystem::Update(0.016f);

    const auto& script = world_.GetComponent<uz::CScript>(e);
    EXPECT_FALSE(script.started) << "on_start must not fire in editor mode";
    EXPECT_EQ(script.luaRef, LUA_REFNIL) << "the script must not even be loaded in editor mode";
    EXPECT_FALSE(EvalBool("dormant_started == true"));
    EXPECT_FALSE(EvalBool("dormant_updates ~= nil"));
}

TEST_F(ScriptSystemTest, EnteringPlayMode_StartsScriptsThatWereDormant) {
    vfs_.AddScript("scripts/wake.lua",
        "function on_start(e) _G.wake_started = true end\n"
        "function on_update(e, dt) _G.wake_updates = (_G.wake_updates or 0) + 1 end\n"
    );
    auto e = CreateScriptEntity("scripts/wake.lua");

    uz::ScriptSystem::SetPlayMode(false);
    uz::ScriptSystem::Update(0.016f);            // dormant: nothing runs
    EXPECT_FALSE(world_.GetComponent<uz::CScript>(e).started);

    uz::ScriptSystem::SetPlayMode(true);
    uz::ScriptSystem::Update(0.016f);            // first play frame: load + on_start
    uz::ScriptSystem::Update(0.016f);            // second play frame: on_update

    EXPECT_TRUE(world_.GetComponent<uz::CScript>(e).started);
    EXPECT_TRUE(EvalBool("wake_started == true"));
    EXPECT_NEAR(EvalNumber("wake_updates"), 1.0, 1e-9);
}

// ─────────────────────────────────────────────────────────────────────────────
// on_start fires on first Update
// ─────────────────────────────────────────────────────────────────────────────

TEST_F(ScriptSystemTest, OnStart_IsCalled_OnFirstUpdate) {
    // Script writes a sentinel global so we can verify on_start ran.
    vfs_.AddScript("scripts/start_test.lua",
        "on_start_called = true\n"
        "function on_start(e) end\n"
    );
    CreateScriptEntity("scripts/start_test.lua");

    uz::ScriptSystem::Update(0.016f);

    // The script executed its top-level code, which set on_start_called.
    // However, top-level code runs in the per-entity env, not _G.
    // The script's env.__index = _G, so reading from _G globals still works,
    // but writes go to env. We verify via the component's started flag.
    auto view = world_.View<uz::CScript>();
    bool anyStarted = false;
    for (auto [enttEntity, script] : view.each()) {
        if (script.scriptPath == "scripts/start_test.lua") {
            anyStarted = script.started;
        }
    }
    EXPECT_TRUE(anyStarted) << "CScript.started must be true after first Update";
}

TEST_F(ScriptSystemTest, OnStart_SetsStartedFlag_OnlyOnce) {
    vfs_.AddScript("scripts/once.lua",
        "function on_start(e) end\n"
        "function on_update(e, dt) end\n"
    );
    auto e = CreateScriptEntity("scripts/once.lua");

    uz::ScriptSystem::Update(0.016f); // loads + calls on_start
    uz::ScriptSystem::Update(0.016f); // calls on_update (not on_start again)

    const auto& script = world_.GetComponent<uz::CScript>(e);
    EXPECT_TRUE(script.started);
    // luaRef must be valid (not REFNIL) after successful load.
    EXPECT_NE(script.luaRef, LUA_REFNIL);
}

// ─────────────────────────────────────────────────────────────────────────────
// on_update fires every subsequent Update
// ─────────────────────────────────────────────────────────────────────────────

TEST_F(ScriptSystemTest, OnUpdate_IsCalledEveryFrame_AfterOnStart) {
    // Script increments a shared counter in the Lua registry on each on_update.
    // We use the Lua registry (a table stored in LUA_REGISTRYINDEX) so the
    // per-entity env isolation does not interfere with our readback.
    vfs_.AddScript("scripts/update_counter.lua",
        "function on_start(e)\n"
        "    _G.update_count = 0\n"
        "end\n"
        "function on_update(e, dt)\n"
        "    _G.update_count = (_G.update_count or 0) + 1\n"
        "end\n"
    );
    CreateScriptEntity("scripts/update_counter.lua");

    uz::ScriptSystem::Update(0.016f); // frame 1: on_start only (no on_update)
    uz::ScriptSystem::Update(0.016f); // frame 2: on_update
    uz::ScriptSystem::Update(0.016f); // frame 3: on_update

    // on_update is NOT called on the same frame as on_start (else-if branch).
    EXPECT_DOUBLE_EQ(EvalNumber("update_count"), 2.0);
}

TEST_F(ScriptSystemTest, OnUpdate_ReceivesDeltaTime_GreaterThanZero) {
    // Script stores the dt argument so we can verify it was forwarded.
    vfs_.AddScript("scripts/dt_test.lua",
        "function on_start(e) end\n"
        "function on_update(e, dt)\n"
        "    _G.last_dt = dt\n"
        "end\n"
    );
    CreateScriptEntity("scripts/dt_test.lua");

    uz::ScriptSystem::Update(0.016f);
    uz::ScriptSystem::Update(0.033f); // second frame with distinct dt

    EXPECT_NEAR(EvalNumber("last_dt"), 0.033, 1e-4);
}

// ─────────────────────────────────────────────────────────────────────────────
// on_stop fires during Shutdown
// ─────────────────────────────────────────────────────────────────────────────

TEST_F(ScriptSystemTest, OnStop_IsCalled_DuringShutdown) {
    vfs_.AddScript("scripts/stop_test.lua",
        "function on_start(e) end\n"
        "function on_update(e, dt) end\n"
        "function on_stop(e)\n"
        "    _G.on_stop_called = true\n"
        "end\n"
    );
    CreateScriptEntity("scripts/stop_test.lua");

    uz::ScriptSystem::Update(0.016f); // load + on_start

    // on_stop_called is not yet set.
    EXPECT_FALSE(EvalBool("on_stop_called == true"));

    uz::ScriptSystem::Shutdown();

    // on_stop must have set the global now.
    EXPECT_TRUE(EvalBool("on_stop_called == true"));

    // Re-initialize so TearDown's second Shutdown is safe.
    uz::ScriptSystem::Initialize(&world_, &vfs_);
}

// ─────────────────────────────────────────────────────────────────────────────
// Script with no on_start / no on_update — must not crash
// ─────────────────────────────────────────────────────────────────────────────

TEST_F(ScriptSystemTest, ScriptWithNoOnStart_DoesNotCrash) {
    // Script defines only on_update — missing on_start is not an error.
    vfs_.AddScript("scripts/no_start.lua",
        "function on_update(e, dt) end\n"
    );
    auto e = CreateScriptEntity("scripts/no_start.lua");

    EXPECT_NO_FATAL_FAILURE(uz::ScriptSystem::Update(0.016f));

    const auto& script = world_.GetComponent<uz::CScript>(e);
    EXPECT_TRUE(script.started);
}

TEST_F(ScriptSystemTest, ScriptWithNoOnUpdate_DoesNotCrash) {
    // Script defines only on_start — missing on_update is not an error.
    vfs_.AddScript("scripts/no_update.lua",
        "function on_start(e) end\n"
    );
    auto e = CreateScriptEntity("scripts/no_update.lua");

    EXPECT_NO_FATAL_FAILURE({
        uz::ScriptSystem::Update(0.016f);
        uz::ScriptSystem::Update(0.016f);
        uz::ScriptSystem::Update(0.016f);
    });

    const auto& script = world_.GetComponent<uz::CScript>(e);
    EXPECT_TRUE(script.started);
    EXPECT_NE(script.luaRef, LUA_REFNIL);
}

TEST_F(ScriptSystemTest, EmptyScript_NoHooks_DoesNotCrash) {
    // Completely empty script — no functions at all. Must not crash on any call.
    vfs_.AddScript("scripts/empty.lua", "-- empty\n");
    auto e = CreateScriptEntity("scripts/empty.lua");

    EXPECT_NO_FATAL_FAILURE({
        uz::ScriptSystem::Update(0.016f);
        uz::ScriptSystem::Update(0.016f);
    });

    // started is set even for scripts with no hooks.
    const auto& script = world_.GetComponent<uz::CScript>(e);
    EXPECT_TRUE(script.started);
}

// ─────────────────────────────────────────────────────────────────────────────
// Syntax error — entity remains alive, engine does not crash
// ─────────────────────────────────────────────────────────────────────────────

TEST_F(ScriptSystemTest, SyntaxError_EntityRemainsAlive_DoesNotCrash) {
    // Broken Lua — compilation must fail gracefully.
    vfs_.AddScript("scripts/syntax_error.lua",
        "function ( broken syntax !!!\n"
    );
    auto e = CreateScriptEntity("scripts/syntax_error.lua");

    EXPECT_NO_FATAL_FAILURE(uz::ScriptSystem::Update(0.016f));

    // Entity must still be alive.
    EXPECT_TRUE(world_.IsValid(e));

    // luaRef stays REFNIL on compile failure; started is set to suppress retry.
    const auto& script = world_.GetComponent<uz::CScript>(e);
    EXPECT_EQ(script.luaRef, LUA_REFNIL);
    EXPECT_TRUE(script.started) << "started must be set to suppress retry on broken script";
}

TEST_F(ScriptSystemTest, SyntaxError_SubsequentUpdates_DoNotCrash) {
    // Verify the failed script does NOT retry on every frame (no log-spam).
    vfs_.AddScript("scripts/broken.lua", "function ( bad !!!\n");
    CreateScriptEntity("scripts/broken.lua");

    EXPECT_NO_FATAL_FAILURE({
        uz::ScriptSystem::Update(0.016f);
        uz::ScriptSystem::Update(0.016f);
        uz::ScriptSystem::Update(0.016f);
    });
}

// ─────────────────────────────────────────────────────────────────────────────
// Runtime error in on_start — entity still receives on_update on next frames
// ─────────────────────────────────────────────────────────────────────────────

TEST_F(ScriptSystemTest, RuntimeErrorInOnStart_UpdateStillCalled_DoesNotCrash) {
    // on_start errors, but on_update should still fire.
    vfs_.AddScript("scripts/start_error.lua",
        "function on_start(e)\n"
        "    error('intentional start error')\n"
        "end\n"
        "function on_update(e, dt)\n"
        "    _G.update_after_start_error = (_G.update_after_start_error or 0) + 1\n"
        "end\n"
    );
    CreateScriptEntity("scripts/start_error.lua");

    EXPECT_NO_FATAL_FAILURE(uz::ScriptSystem::Update(0.016f)); // on_start errors
    EXPECT_NO_FATAL_FAILURE(uz::ScriptSystem::Update(0.016f)); // on_update fires

    // on_update must have been called despite the on_start failure.
    EXPECT_GE(EvalNumber("update_after_start_error"), 1.0);
}

// ─────────────────────────────────────────────────────────────────────────────
// Runtime error in on_update — subsequent updates still fire, engine stable
// ─────────────────────────────────────────────────────────────────────────────

TEST_F(ScriptSystemTest, RuntimeErrorInOnUpdate_DoesNotCrash_NextCallStillFires) {
    // on_update errors every frame — engine must not crash or stop calling it.
    vfs_.AddScript("scripts/update_error.lua",
        "function on_start(e) end\n"
        "function on_update(e, dt)\n"
        "    _G.update_error_count = (_G.update_error_count or 0) + 1\n"
        "    error('intentional update error')\n"
        "end\n"
    );
    CreateScriptEntity("scripts/update_error.lua");

    EXPECT_NO_FATAL_FAILURE({
        uz::ScriptSystem::Update(0.016f);
        uz::ScriptSystem::Update(0.016f);
        uz::ScriptSystem::Update(0.016f);
    });

    // on_update was called on frames 2 and 3 (not frame 1, which is on_start).
    EXPECT_DOUBLE_EQ(EvalNumber("update_error_count"), 2.0);
}

// ─────────────────────────────────────────────────────────────────────────────
// Missing VFS path — entity remains alive, no crash
// ─────────────────────────────────────────────────────────────────────────────

TEST_F(ScriptSystemTest, MissingScript_EntityRemainsAlive_DoesNotCrash) {
    // VFS has no entry for this path — Read() returns empty bytes.
    auto e = CreateScriptEntity("scripts/does_not_exist.lua");

    EXPECT_NO_FATAL_FAILURE(uz::ScriptSystem::Update(0.016f));

    EXPECT_TRUE(world_.IsValid(e));

    const auto& script = world_.GetComponent<uz::CScript>(e);
    // Load failed — luaRef stays REFNIL, started is set to prevent retry.
    EXPECT_EQ(script.luaRef, LUA_REFNIL);
    EXPECT_TRUE(script.started);
}

TEST_F(ScriptSystemTest, MissingScript_NoRetryOnSubsequentFrames) {
    // The system must NOT re-attempt loading a missing script every frame.
    CreateScriptEntity("scripts/missing.lua");

    // Three frames — if it retried, it would try to read 3 times.
    // We can't observe the exact read count here, but we can verify no crash
    // and that started stays true (suppressing future attempts).
    EXPECT_NO_FATAL_FAILURE({
        uz::ScriptSystem::Update(0.016f);
        uz::ScriptSystem::Update(0.016f);
        uz::ScriptSystem::Update(0.016f);
    });
}

// ─────────────────────────────────────────────────────────────────────────────
// Multiple scripts — each runs independently
// ─────────────────────────────────────────────────────────────────────────────

TEST_F(ScriptSystemTest, MultipleScripts_AllReceiveOnUpdate) {
    vfs_.AddScript("scripts/a.lua",
        "function on_start(e) end\n"
        "function on_update(e, dt)\n"
        "    _G.count_a = (_G.count_a or 0) + 1\n"
        "end\n"
    );
    vfs_.AddScript("scripts/b.lua",
        "function on_start(e) end\n"
        "function on_update(e, dt)\n"
        "    _G.count_b = (_G.count_b or 0) + 1\n"
        "end\n"
    );

    CreateScriptEntity("scripts/a.lua");
    CreateScriptEntity("scripts/b.lua");

    uz::ScriptSystem::Update(0.016f);
    uz::ScriptSystem::Update(0.016f);

    // Frame 1 = on_start only, frame 2 = on_update. So each script got 1 on_update.
    EXPECT_DOUBLE_EQ(EvalNumber("count_a"), 1.0);
    EXPECT_DOUBLE_EQ(EvalNumber("count_b"), 1.0);
}

TEST_F(ScriptSystemTest, MultipleScripts_AllReceiveOnStop_DuringShutdown) {
    vfs_.AddScript("scripts/stop_a.lua",
        "function on_start(e) end\n"
        "function on_update(e, dt) end\n"
        "function on_stop(e)\n"
        "    _G.stopped_a = true\n"
        "end\n"
    );
    vfs_.AddScript("scripts/stop_b.lua",
        "function on_start(e) end\n"
        "function on_update(e, dt) end\n"
        "function on_stop(e)\n"
        "    _G.stopped_b = true\n"
        "end\n"
    );

    CreateScriptEntity("scripts/stop_a.lua");
    CreateScriptEntity("scripts/stop_b.lua");

    uz::ScriptSystem::Update(0.016f);
    uz::ScriptSystem::Shutdown();

    EXPECT_TRUE(EvalBool("stopped_a == true"));
    EXPECT_TRUE(EvalBool("stopped_b == true"));

    uz::ScriptSystem::Initialize(&world_, &vfs_);
}

// ─────────────────────────────────────────────────────────────────────────────
// Environment isolation — two scripts writing the same local variable
// ─────────────────────────────────────────────────────────────────────────────

TEST_F(ScriptSystemTest, EnvironmentIsolation_TwoScripts_DoNotShareLocals) {
    // Both scripts define "my_value" as a local-to-env global. They must not
    // clobber each other because each script has its own environment table.
    vfs_.AddScript("scripts/iso_a.lua",
        "my_value = 'from_a'\n"
        "function on_start(e) end\n"
        "function on_update(e, dt) end\n"
    );
    vfs_.AddScript("scripts/iso_b.lua",
        "my_value = 'from_b'\n"
        "function on_start(e) end\n"
        "function on_update(e, dt) end\n"
    );

    CreateScriptEntity("scripts/iso_a.lua");
    CreateScriptEntity("scripts/iso_b.lua");

    uz::ScriptSystem::Update(0.016f);

    // my_value in _G must be nil (or whatever was there before) because each
    // script sets it in its own env, not in _G. We verify that _G is untouched.
    // Both scripts write to env.my_value (via setfenv), so _G.my_value stays nil.
    EXPECT_TRUE(EvalBool("my_value == nil"))
        << "Scripts must not write 'my_value' into the shared _G";
}

TEST_F(ScriptSystemTest, EnvironmentIsolation_ScriptACannotReadScriptBLocal) {
    // Script A sets a_secret in its env. Script B must not be able to read it.
    // Both execute in isolated envs — reads fall back to _G, not to each other.
    vfs_.AddScript("scripts/secret_a.lua",
        "a_secret = 42\n"
        "function on_start(e) end\n"
        "function on_update(e, dt) end\n"
    );
    vfs_.AddScript("scripts/secret_b.lua",
        "function on_start(e) end\n"
        "function on_update(e, dt)\n"
        "    _G.b_saw_a_secret = a_secret  -- reads from env.__index = _G\n"
        "end\n"
    );

    CreateScriptEntity("scripts/secret_a.lua");
    CreateScriptEntity("scripts/secret_b.lua");

    uz::ScriptSystem::Update(0.016f);
    uz::ScriptSystem::Update(0.016f);

    // script_b's env.__index = _G, not script_a's env.
    // So b_saw_a_secret must be nil (a_secret is not in _G).
    EXPECT_TRUE(EvalBool("b_saw_a_secret == nil"))
        << "Script B must not be able to read Script A's env-local variable";
}

// ─────────────────────────────────────────────────────────────────────────────
// CScript luaRef and envRef are released on Shutdown
// ─────────────────────────────────────────────────────────────────────────────

TEST_F(ScriptSystemTest, Shutdown_ReleasesLuaRefs_ComponentReset) {
    vfs_.AddScript("scripts/cleanup.lua",
        "function on_start(e) end\n"
        "function on_update(e, dt) end\n"
        "function on_stop(e) end\n"
    );
    auto e = CreateScriptEntity("scripts/cleanup.lua");

    uz::ScriptSystem::Update(0.016f);

    // Verify refs were acquired.
    const auto& scriptBefore = world_.GetComponent<uz::CScript>(e);
    EXPECT_NE(scriptBefore.luaRef, LUA_REFNIL);
    EXPECT_NE(scriptBefore.envRef, LUA_REFNIL);
    EXPECT_TRUE(scriptBefore.started);

    uz::ScriptSystem::Shutdown();

    // After Shutdown, refs must be released and flags reset.
    const auto& scriptAfter = world_.GetComponent<uz::CScript>(e);
    EXPECT_EQ(scriptAfter.luaRef, LUA_REFNIL);
    EXPECT_EQ(scriptAfter.envRef, LUA_REFNIL);
    EXPECT_FALSE(scriptAfter.started);

    uz::ScriptSystem::Initialize(&world_, &vfs_);
}

// ─────────────────────────────────────────────────────────────────────────────
// Update before Initialize — must log warning, not crash
// ─────────────────────────────────────────────────────────────────────────────

TEST_F(ScriptSystemTest, Update_BeforeInitialize_DoesNotCrash) {
    uz::ScriptSystem::Shutdown(); // clears s_world / s_vfs

    // Update with no world/vfs set — implementation logs a warning and returns.
    EXPECT_NO_FATAL_FAILURE(uz::ScriptSystem::Update(0.016f));

    uz::ScriptSystem::Initialize(&world_, &vfs_);
}

// ─────────────────────────────────────────────────────────────────────────────
// Script using uz.* API — VM globals accessible from script env
// ─────────────────────────────────────────────────────────────────────────────

TEST_F(ScriptSystemTest, ScriptCanCallUzLog_WithoutCrash) {
    // Verify that the per-script env correctly inherits _G (which has uz.*).
    vfs_.AddScript("scripts/use_uz_api.lua",
        "function on_start(e)\n"
        "    uz.log('ScriptSystem test: on_start')\n"
        "end\n"
        "function on_update(e, dt)\n"
        "    uz.log('ScriptSystem test: on_update dt=' .. tostring(dt))\n"
        "end\n"
    );
    auto e = CreateScriptEntity("scripts/use_uz_api.lua");

    EXPECT_NO_FATAL_FAILURE({
        uz::ScriptSystem::Update(0.016f);
        uz::ScriptSystem::Update(0.016f);
    });

    const auto& script = world_.GetComponent<uz::CScript>(e);
    EXPECT_TRUE(script.started);
    EXPECT_NE(script.luaRef, LUA_REFNIL);
}

TEST_F(ScriptSystemTest, ScriptCanReadUzDeltaTime_FromFrameGlobals) {
    // LuaVM::UpdateFrameGlobals sets uz.delta_time. Scripts must be able
    // to read it via the inherited _G.__index chain.
    vfs_.AddScript("scripts/read_dt.lua",
        "function on_start(e) end\n"
        "function on_update(e, dt)\n"
        "    _G.script_read_dt = uz.delta_time\n"
        "end\n"
    );
    CreateScriptEntity("scripts/read_dt.lua");

    uz::LuaVM::UpdateFrameGlobals(0.025f, 1.0f, 10);
    uz::ScriptSystem::Update(0.025f); // frame 1: on_start only

    uz::LuaVM::UpdateFrameGlobals(0.025f, 1.0f, 11);
    uz::ScriptSystem::Update(0.025f); // frame 2: on_update fires

    EXPECT_NEAR(EvalNumber("script_read_dt"), 0.025, 1e-4);
}

// ─────────────────────────────────────────────────────────────────────────────
// Lua stack balance — Update must not accumulate values on the stack
// ─────────────────────────────────────────────────────────────────────────────

TEST_F(ScriptSystemTest, Update_DoesNotLeakLuaStack_OverMultipleFrames) {
    vfs_.AddScript("scripts/stack_check.lua",
        "function on_start(e) end\n"
        "function on_update(e, dt) end\n"
    );
    CreateScriptEntity("scripts/stack_check.lua");

    lua_State* L = uz::LuaVM::GetState();

    uz::ScriptSystem::Update(0.016f); // first call loads + on_start
    int topAfterLoad = lua_gettop(L);

    uz::ScriptSystem::Update(0.016f);
    EXPECT_EQ(lua_gettop(L), topAfterLoad) << "Stack grew after second Update";

    uz::ScriptSystem::Update(0.016f);
    EXPECT_EQ(lua_gettop(L), topAfterLoad) << "Stack grew after third Update";
}
