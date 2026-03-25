/// Unit tests for VFX Lua bindings — uz.vfx:spawn() and uz.vfx:emit_event().
///
/// Task 7.1 — spawn() with graph path matching:
///   - Matches emitter by graphPath, returns non-nil integer handle
///   - No matching emitter → returns nil
///   - Two emitters share graphPath → closest (by spawnVelocity proxy) is returned
///   - Uninitialized emitter (initialized=false) → skipped, returns nil
///   - Named-key Vec3 form {x=1, y=2, z=3} is accepted by spawn()
///
/// Task 7.2 — emit_event() with count and velocity_scale:
///   - Default count=1 and velocity_scale=1.0 when args are omitted
///   - Explicit count propagates to eventQueue
///   - Explicit count + velocity_scale both propagate to eventQueue
///   - Handle 0 (invalid) → no event enqueued, no crash
///
/// All tests are CPU-only. No GPU hardware required.
/// MockRHIDevice satisfies VFXSystem::Initialize's non-null device requirement.
/// ParticleEmitter::initialized is set manually (skipping GPU buffer allocation)
/// so RegisterEmitter accepts the emitter without a real RHI device.

#include "vfx_lua_bindings.h"
#include "vfx_system.h"
#include "particle_emitter.h"
#include "events/vfx_event.h"
#include "lua_vm.h"
#include "../mock_rhi_device.h"

extern "C" {
#include <lua.h>
#include <lualib.h>
#include <lauxlib.h>
}

#include <gtest/gtest.h>

#include <cstring>
#include <string>
#include <vector>

using namespace uz;
using uz::test::MockRHIDevice;

// ─────────────────────────────────────────────────────────────────────────────
// Helpers
// ─────────────────────────────────────────────────────────────────────────────

/// Builds a minimal ParticleEmitter that passes RegisterEmitter's check.
///
/// Sets initialized=true directly to bypass GPU buffer allocation so tests
/// run CPU-only. The emitter is NOT inserted into the VFXSystem — callers
/// must call vfxSystem.RegisterEmitter(&emitter) themselves.
static ParticleEmitter MakeReadyEmitter(const std::string& graphPath,
                                        float px = 0.0f,
                                        float py = 0.0f,
                                        float pz = 0.0f)
{
    ParticleEmitter e;
    e.graphPath    = graphPath;
    e.initialized  = true;           // skip GPU init for CPU-only tests
    e.maxParticles = 100;
    e.emissionRate = 10.0f;
    // spawnVelocity is used by VFXSpawn as the emitter position proxy
    // when tiebreaking multiple emitters with the same graphPath.
    e.spawnVelocity = {px, py, pz};
    return e;
}

// ─────────────────────────────────────────────────────────────────────────────
// Fixtures
// ─────────────────────────────────────────────────────────────────────────────

/// Base fixture: initializes LuaVM, VFXSystem, and registers VFX bindings.
class VFXLuaSpawnTest : public ::testing::Test {
protected:
    MockRHIDevice    device_;
    uz::VFXSystem    vfxSystem_;

    void SetUp() override {
        ASSERT_TRUE(uz::LuaVM::Init());
        vfxSystem_.Initialize(&device_, 100000u);
        uz::RegisterVFXLuaBindings(uz::LuaVM::GetState(), &vfxSystem_);
    }

    void TearDown() override {
        uz::UnregisterVFXLuaBindings(uz::LuaVM::GetState());
        vfxSystem_.Shutdown(&device_);
        uz::LuaVM::Shutdown();
    }

    /// Runs Lua source, returns true on success.
    bool DoString(const char* code) {
        return luaL_dostring(uz::LuaVM::GetState(), code) == 0;
    }

    /// Evaluates a Lua expression and returns the number result.
    /// Returns -9999.0 and records a test failure on Lua error.
    double EvalNumber(const char* expr) {
        lua_State* L = uz::LuaVM::GetState();
        std::string code = std::string("return ") + expr;
        if (luaL_dostring(L, code.c_str()) != 0) {
            const char* err = lua_tostring(L, -1);
            ADD_FAILURE() << "Lua eval error: " << (err ? err : "unknown");
            lua_pop(L, 1);
            return -9999.0;
        }
        const double r = lua_tonumber(L, -1);
        lua_pop(L, 1);
        return r;
    }

    /// Evaluates a Lua expression and returns the boolean result.
    bool EvalBool(const char* expr) {
        lua_State* L = uz::LuaVM::GetState();
        std::string code = std::string("return ") + expr;
        if (luaL_dostring(L, code.c_str()) != 0) {
            lua_pop(L, 1);
            return false;
        }
        const bool r = lua_toboolean(L, -1) != 0;
        lua_pop(L, 1);
        return r;
    }

    /// Reads the lua_Integer stored in a global and returns it.
    /// The global must already be set by a DoString() call.
    lua_Integer ReadIntegerGlobal(const char* name) {
        lua_State* L = uz::LuaVM::GetState();
        lua_getglobal(L, name);
        const lua_Integer val = lua_tointeger(L, -1);
        lua_pop(L, 1);
        return val;
    }

    /// Returns true if the named Lua global is nil.
    bool GlobalIsNil(const char* name) {
        lua_State* L = uz::LuaVM::GetState();
        lua_getglobal(L, name);
        const bool isNil = lua_isnil(L, -1);
        lua_pop(L, 1);
        return isNil;
    }
};

// ─────────────────────────────────────────────────────────────────────────────
// 7.1 — spawn() tests
// ─────────────────────────────────────────────────────────────────────────────

/// spawn() returns a non-nil integer handle when an initialized emitter with
/// a matching graphPath is registered in the VFXSystem.
TEST_F(VFXLuaSpawnTest, SpawnMatchesByGraphPath_ReturnsHandle) {
    ParticleEmitter emitter = MakeReadyEmitter("effects/fire.uzvfx");
    vfxSystem_.RegisterEmitter(&emitter);

    ASSERT_TRUE(DoString(
        "handle = uz.vfx:spawn('effects/fire.uzvfx', {0, 0, 0})"
    ));

    EXPECT_FALSE(GlobalIsNil("handle"))
        << "Expected non-nil handle for a registered matching emitter";

    // Handle must be a non-zero integer (pointer encoded as lua_Integer).
    const lua_Integer h = ReadIntegerGlobal("handle");
    EXPECT_NE(h, 0) << "Handle must not be zero";

    vfxSystem_.UnregisterEmitter(&emitter);
}

/// spawn() returns nil when no emitter with the given graphPath is registered.
TEST_F(VFXLuaSpawnTest, SpawnNoMatch_ReturnsNil) {
    ParticleEmitter emitter = MakeReadyEmitter("effects/smoke.uzvfx");
    vfxSystem_.RegisterEmitter(&emitter);

    ASSERT_TRUE(DoString(
        "result = uz.vfx:spawn('effects/fire.uzvfx', {0, 0, 0})"
    ));

    EXPECT_TRUE(GlobalIsNil("result"))
        << "spawn() must return nil when no emitter matches the graph path";

    vfxSystem_.UnregisterEmitter(&emitter);
}

/// When two emitters share the same graphPath, spawn() returns the handle
/// of the one closest to the requested position.
///
/// The implementation uses emitter->spawnVelocity as the position proxy.
/// emitterNear is placed at (1, 0, 0), emitterFar at (100, 0, 0).
/// Spawn requests position (0, 0, 0) → emitterNear must be selected.
TEST_F(VFXLuaSpawnTest, SpawnClosestPosition_ReturnsNearestEmitter) {
    ParticleEmitter emitterNear = MakeReadyEmitter("effects/fire.uzvfx",  1.0f, 0.0f, 0.0f);
    ParticleEmitter emitterFar  = MakeReadyEmitter("effects/fire.uzvfx", 100.0f, 0.0f, 0.0f);

    vfxSystem_.RegisterEmitter(&emitterNear);
    vfxSystem_.RegisterEmitter(&emitterFar);

    ASSERT_TRUE(DoString(
        "handle = uz.vfx:spawn('effects/fire.uzvfx', {0, 0, 0})"
    ));

    EXPECT_FALSE(GlobalIsNil("handle"))
        << "Expected a valid handle when matching emitters exist";

    // Reconstruct pointer from the Lua integer handle and compare.
    const lua_Integer h = ReadIntegerGlobal("handle");
    uintptr_t ptr;
    std::memcpy(&ptr, &h, sizeof(ptr));
    ParticleEmitter* selected;
    std::memcpy(&selected, &ptr, sizeof(selected));

    EXPECT_EQ(selected, &emitterNear)
        << "spawn() must return the emitter closest to the requested position";

    vfxSystem_.UnregisterEmitter(&emitterNear);
    vfxSystem_.UnregisterEmitter(&emitterFar);
}

/// spawn() skips emitters with initialized=false and returns nil.
///
/// VFXSystem::RegisterEmitter logs a warning and does not add uninitialized
/// emitters to the active list, so spawn() never sees them.
TEST_F(VFXLuaSpawnTest, SpawnIgnoresUninitializedEmitters_ReturnsNil) {
    ParticleEmitter emitter = MakeReadyEmitter("effects/fire.uzvfx");
    emitter.initialized = false;   // force uninitialized — RegisterEmitter will skip it

    vfxSystem_.RegisterEmitter(&emitter);   // logs warning, does NOT add to active list

    ASSERT_TRUE(DoString(
        "result = uz.vfx:spawn('effects/fire.uzvfx', {0, 0, 0})"
    ));

    EXPECT_TRUE(GlobalIsNil("result"))
        << "spawn() must return nil when the only matching emitter is uninitialized";

    // emitter was not added to the active list, so no UnregisterEmitter needed.
}

/// spawn() accepts named-key Vec3 form {x=..., y=..., z=...} in addition to
/// the positional array form {1, 2, 3}.
TEST_F(VFXLuaSpawnTest, SpawnWithNamedVec3_ReturnsHandle) {
    ParticleEmitter emitter = MakeReadyEmitter("effects/sparks.uzvfx");
    vfxSystem_.RegisterEmitter(&emitter);

    ASSERT_TRUE(DoString(
        "handle = uz.vfx:spawn('effects/sparks.uzvfx', {x=1, y=2, z=3})"
    ));

    EXPECT_FALSE(GlobalIsNil("handle"))
        << "spawn() must accept named Vec3 form {x=..., y=..., z=...}";

    const lua_Integer h = ReadIntegerGlobal("handle");
    EXPECT_NE(h, 0) << "Handle must be non-zero for a valid emitter";

    vfxSystem_.UnregisterEmitter(&emitter);
}

// ─────────────────────────────────────────────────────────────────────────────
// 7.2 — emit_event() tests
// ─────────────────────────────────────────────────────────────────────────────

/// Fixture with a pre-registered emitter and its Lua handle stored as a global.
/// Inherits VFXLuaSpawnTest so it shares SetUp/TearDown.
class VFXLuaEmitEventTest : public VFXLuaSpawnTest {
protected:
    ParticleEmitter emitter_;

    void SetUp() override {
        VFXLuaSpawnTest::SetUp();

        emitter_ = MakeReadyEmitter("effects/burst.uzvfx");
        vfxSystem_.RegisterEmitter(&emitter_);

        // Store handle in a Lua global for all emit_event tests.
        ASSERT_TRUE(DoString(
            "test_handle = uz.vfx:spawn('effects/burst.uzvfx', {0, 0, 0})"
        ));
        ASSERT_FALSE(GlobalIsNil("test_handle"))
            << "Pre-condition: test_handle must be valid before emit_event tests";
    }

    void TearDown() override {
        vfxSystem_.UnregisterEmitter(&emitter_);
        VFXLuaSpawnTest::TearDown();
    }
};

/// emit_event() with only a name argument enqueues exactly one event with
/// emissionCount=1 and velocityScale=1.0 (the documented defaults).
TEST_F(VFXLuaEmitEventTest, EmitEventDefault_Count1_VelocityScale1) {
    ASSERT_TRUE(DoString(
        "uz.vfx:emit_event(test_handle, 'burst')"
    ));

    const auto events = emitter_.eventQueue.Drain();

    ASSERT_EQ(events.size(), 1u)
        << "emit_event with default args must enqueue exactly one event";
    EXPECT_EQ(events[0].name, "burst");
    EXPECT_EQ(events[0].emissionCount, 1u)
        << "Default emissionCount must be 1";
    EXPECT_FLOAT_EQ(events[0].velocityScale, 1.0f)
        << "Default velocityScale must be 1.0";
}

/// emit_event() with an explicit count propagates the count to eventQueue.
TEST_F(VFXLuaEmitEventTest, EmitEventWithCount_PropagatesCount) {
    ASSERT_TRUE(DoString(
        "uz.vfx:emit_event(test_handle, 'explosion', 5)"
    ));

    const auto events = emitter_.eventQueue.Drain();

    ASSERT_EQ(events.size(), 1u);
    EXPECT_EQ(events[0].name, "explosion");
    EXPECT_EQ(events[0].emissionCount, 5u)
        << "emissionCount must match the value passed to emit_event";
    EXPECT_FLOAT_EQ(events[0].velocityScale, 1.0f)
        << "velocityScale must default to 1.0 when not provided";
}

/// emit_event() with both count and velocity_scale propagates both values.
TEST_F(VFXLuaEmitEventTest, EmitEventWithCountAndVelocity_BothPropagated) {
    ASSERT_TRUE(DoString(
        "uz.vfx:emit_event(test_handle, 'shockwave', 3, 2.5)"
    ));

    const auto events = emitter_.eventQueue.Drain();

    ASSERT_EQ(events.size(), 1u);
    EXPECT_EQ(events[0].name, "shockwave");
    EXPECT_EQ(events[0].emissionCount, 3u)
        << "emissionCount must be 3";
    EXPECT_FLOAT_EQ(events[0].velocityScale, 2.5f)
        << "velocityScale must be 2.5";
}

/// emit_event() with handle=0 (invalid sentinel) must not enqueue any event
/// and must not crash. ResolveEmitter rejects handle=0 immediately.
TEST_F(VFXLuaEmitEventTest, EmitEventInvalidHandle_NoEventEnqueued_NoCrash) {
    // Passing literal 0 is the invalid-handle sentinel in the implementation.
    // luaL_checkinteger will accept it; ResolveEmitter will return nullptr.
    ASSERT_TRUE(DoString(
        "uz.vfx:emit_event(0, 'burst', 10, 1.0)"
    )) << "emit_event with invalid handle must not raise a Lua error";

    // The emitter's queue must remain empty — the invalid handle was rejected.
    const auto events = emitter_.eventQueue.Drain();
    EXPECT_TRUE(events.empty())
        << "No event must be enqueued when the handle is invalid (0)";
}
