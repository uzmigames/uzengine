/// Unit tests for VFX Lua scripting bindings — Tasks 7.5, 7.6, 7.7.
///
/// Covers:
///   7.5 Decal Lua API:
///     - uz.vfx:get_decal_stats() returns a table with the three expected
///       fields when the DecalManager is registered and when it is not.
///     - uz.vfx:spawn_decal() returns a non-zero integer handle when the
///       DecalManager is initialized and the pool is non-empty.
///     - uz.vfx:set_decal_blend_mode() with each valid mode string sets the
///       component's blendMode field.
///     - uz.vfx:set_decal_blend_mode() with an unknown string raises a Lua error.
///     - uz.vfx:set_decal_normal_intensity() updates normalIntensity on the component.
///     - uz.vfx:fade_out_decal() adjusts the component's lifetime.
///     - uz.vfx:destroy_decal() marks the component inactive.
///     - Binding functions survive gracefully when DecalManager is not registered.
///
///   7.6 LOD and budget query bindings:
///     - uz.vfx:set_lod_distances() writes to the emitter's lodSettings.
///     - uz.vfx:set_lod_distances() with invalid order raises a Lua error.
///     - uz.vfx:get_lod_level() returns the emitter's current LOD level integer.
///     - uz.vfx:set_lod_enabled() writes to emitter->lodEnabled.
///     - uz.vfx:get_budget_stats() returns a table with {total, used, available,
///       utilization_pct} where total matches the budget passed to Initialize().
///     - uz.vfx:get_total_alive() returns 0 when no particles are alive.
///     - uz.vfx:get_emitter_count() matches the number of registered emitters.
///
///   7.7 Composite spawn/stop:
///     - uz.vfx:spawn_composite() with a nonexistent file returns an empty table,
///       not nil, and does not crash.
///     - uz.vfx:spawn_composite() with a well-formed composite JSON string
///       (via a temp file) and matching registered emitters returns a table of
///       handles and configures emitter startDelay correctly.
///     - uz.vfx:stop_composite({}) called with an empty table is a no-op.
///     - uz.vfx:stop_composite() with valid handles sets isEmitting = false on
///       every resolved emitter.
///
/// All tests are CPU-only — no GPU hardware is required.
/// DecalManager::Initialize(nullptr, ...) is valid per its documented contract.

#include <gtest/gtest.h>

#include "engine/scripting/lua_vm.h"
#include "engine/scripting/scene/vfx_lua_bindings.h"
#include "engine/scripting/scene/decal_lua_bindings.h"
#include "engine/scripting/scene/lua_scene_io_api.h"
#include "engine/asset/vfs.h"
#include "engine/vfx/vfx_system.h"
#include "engine/vfx/components/particle_emitter.h"
#include "engine/vfx/decals/decal_manager.h"
#include "engine/vfx/decals/decal_component.h"
#include "engine/vfx/decals/decal_asset.h"
#include "engine/vfx/lod/vfx_lod.h"
#include "engine/vfx/core/particle_budget.h"
#include "engine/math/vector3.h"
#include "mock_rhi_device.h"

extern "C" {
#include <lua.h>
#include <lualib.h>
#include <lauxlib.h>
}

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>

using namespace uz;
using uz::test::MockRHIDevice;

// ─────────────────────────────────────────────────────────────────────────────
// Test fixture — base
// ─────────────────────────────────────────────────────────────────────────────

/// Base fixture: initializes LuaVM and registers the uz.vfx table via
/// RegisterVFXLuaBindings. Shuts everything down in TearDown.
class VFXLuaDecalLodCompositeTest : public ::testing::Test {
protected:
    MockRHIDevice device_;
    VFXSystem     vfxSystem_;
    DecalManager  decalManager_;

    void SetUp() override {
        ASSERT_TRUE(LuaVM::Init());
        vfxSystem_.Initialize(nullptr, 100000u);
        RegisterVFXLuaBindings(LuaVM::GetState(), &vfxSystem_);
        // DecalManager: nullptr device is valid for CPU-only bookkeeping.
        decalManager_.Initialize(nullptr, /*poolSize=*/50u, /*atlasSize=*/512u);
        RegisterDecalLuaBindings(LuaVM::GetState(), &decalManager_);
    }

    void TearDown() override {
        UnregisterDecalLuaBindings(LuaVM::GetState());
        UnregisterVFXLuaBindings(LuaVM::GetState());
        decalManager_.Shutdown(nullptr);
        vfxSystem_.Shutdown(nullptr);
        LuaVM::Shutdown();
    }

    /// Runs a Lua code chunk. Returns true on success.
    bool DoString(const char* code) {
        return luaL_dostring(LuaVM::GetState(), code) == 0;
    }

    /// Evaluates a Lua expression; returns the numeric result.
    /// Fails the test with ADD_FAILURE() and returns -999.0 on Lua error.
    double EvalNumber(const char* expr) {
        lua_State* L = LuaVM::GetState();
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

    /// Evaluates a Lua expression; returns the boolean result.
    bool EvalBool(const char* expr) {
        lua_State* L = LuaVM::GetState();
        std::string code = std::string("return ") + expr;
        if (luaL_dostring(L, code.c_str()) != 0) {
            lua_pop(L, 1);
            return false;
        }
        bool r = (lua_toboolean(L, -1) != 0);
        lua_pop(L, 1);
        return r;
    }

    /// Runs a Lua chunk and returns the string field from a returned table.
    /// The chunk must end with "return <table_expr>".
    std::string EvalTableStringField(const char* luaChunk, const char* field) {
        lua_State* L = LuaVM::GetState();
        if (luaL_dostring(L, luaChunk) != 0) {
            const char* err = lua_tostring(L, -1);
            ADD_FAILURE() << "Lua error: " << (err ? err : "unknown");
            lua_pop(L, 1);
            return "";
        }
        lua_getfield(L, -1, field);
        std::string result;
        if (lua_isstring(L, -1)) {
            result = lua_tostring(L, -1);
        }
        lua_pop(L, 2); // field + table
        return result;
    }

    /// Registers an emitter (already initialized from budget) and returns its
    /// Lua integer handle by running a one-shot spawn via a graphPath match.
    ///
    /// The emitter must already be registered with vfxSystem_.RegisterEmitter().
    /// Returns the raw EmitterToHandle value: cast of pointer to lua_Integer.
    lua_Integer EmitterHandleFor(ParticleEmitter* emitter) {
        uintptr_t ptr;
        std::memcpy(&ptr, &emitter, sizeof(ptr));
        return static_cast<lua_Integer>(ptr);
    }
};

// =============================================================================
// 7.5 — Decal Lua API
// =============================================================================

// ─────────────────────────────────────────────────────────────────────────────
// get_decal_stats — table structure and fields
// ─────────────────────────────────────────────────────────────────────────────

/// get_decal_stats() must return a table containing active_count.
TEST_F(VFXLuaDecalLodCompositeTest,
       GetDecalStats_ReturnsTable_WithActiveCountField) {
    ASSERT_TRUE(DoString("_stats = uz.vfx:get_decal_stats()"));
    EXPECT_TRUE(EvalBool("type(_stats) == 'table'"));
    EXPECT_TRUE(EvalBool("type(_stats.active_count) == 'number'"));
}

/// get_decal_stats() must return a table containing pool_available.
TEST_F(VFXLuaDecalLodCompositeTest,
       GetDecalStats_ReturnsTable_WithPoolAvailableField) {
    ASSERT_TRUE(DoString("_stats = uz.vfx:get_decal_stats()"));
    EXPECT_TRUE(EvalBool("type(_stats.pool_available) == 'number'"));
}

/// get_decal_stats() must return a table containing atlas_occupancy.
TEST_F(VFXLuaDecalLodCompositeTest,
       GetDecalStats_ReturnsTable_WithAtlasOccupancyField) {
    ASSERT_TRUE(DoString("_stats = uz.vfx:get_decal_stats()"));
    EXPECT_TRUE(EvalBool("type(_stats.atlas_occupancy) == 'number'"));
}

/// With a freshly initialized pool of 50 slots and no active decals,
/// active_count is 0 and pool_available is 50.
TEST_F(VFXLuaDecalLodCompositeTest,
       GetDecalStats_FreshPool_ActiveCountZero_AvailableEqualPoolSize) {
    ASSERT_TRUE(DoString("_stats = uz.vfx:get_decal_stats()"));
    EXPECT_DOUBLE_EQ(EvalNumber("_stats.active_count"),   0.0);
    EXPECT_DOUBLE_EQ(EvalNumber("_stats.pool_available"), 50.0);
}

/// atlas_occupancy is in [0, 1] for a freshly initialized, empty atlas.
TEST_F(VFXLuaDecalLodCompositeTest,
       GetDecalStats_FreshPool_AtlasOccupancyInRange) {
    ASSERT_TRUE(DoString("_stats = uz.vfx:get_decal_stats()"));
    const double occ = EvalNumber("_stats.atlas_occupancy");
    EXPECT_GE(occ, 0.0);
    EXPECT_LE(occ, 1.0);
}

// ─────────────────────────────────────────────────────────────────────────────
// spawn_decal — valid spawn returns non-zero handle
// ─────────────────────────────────────────────────────────────────────────────

/// spawn_decal with valid args returns a non-zero integer handle.
TEST_F(VFXLuaDecalLodCompositeTest,
       SpawnDecal_ValidArgs_ReturnsNonZeroHandle) {
    ASSERT_TRUE(DoString(
        "_h = uz.vfx:spawn_decal('materials/bullet.uzmat', {0,0,0}, {0,0,1})"));
    EXPECT_TRUE(EvalBool("type(_h) == 'number'"));
    EXPECT_NE(EvalNumber("_h"), 0.0);
}

/// After spawning one decal, active_count in stats becomes 1.
TEST_F(VFXLuaDecalLodCompositeTest,
       SpawnDecal_AfterSpawn_ActiveCountIncrementsToOne) {
    ASSERT_TRUE(DoString(
        "_h = uz.vfx:spawn_decal('materials/fire.uzmat', {1,2,3}, {0,1,0})"));
    ASSERT_TRUE(DoString("_s = uz.vfx:get_decal_stats()"));
    EXPECT_DOUBLE_EQ(EvalNumber("_s.active_count"), 1.0);
}

/// pool_available decreases by 1 after spawning one decal.
TEST_F(VFXLuaDecalLodCompositeTest,
       SpawnDecal_AfterSpawn_PoolAvailableDecrements) {
    ASSERT_TRUE(DoString(
        "_h = uz.vfx:spawn_decal('materials/fire.uzmat', {0,0,0}, {0,0,1})"));
    ASSERT_TRUE(DoString("_s = uz.vfx:get_decal_stats()"));
    EXPECT_DOUBLE_EQ(EvalNumber("_s.pool_available"), 49.0);
}

// ─────────────────────────────────────────────────────────────────────────────
// set_decal_blend_mode — all valid mode strings
// ─────────────────────────────────────────────────────────────────────────────

/// set_decal_blend_mode("opaque") does not raise a Lua error.
TEST_F(VFXLuaDecalLodCompositeTest,
       SetDecalBlendMode_OpaqueString_NoError) {
    ASSERT_TRUE(DoString(
        "_h = uz.vfx:spawn_decal('mat/a.uzmat', {0,0,0}, {0,0,1})"));
    EXPECT_TRUE(DoString("uz.vfx:set_decal_blend_mode(_h, 'opaque')"));
}

/// set_decal_blend_mode("additive") does not raise a Lua error.
TEST_F(VFXLuaDecalLodCompositeTest,
       SetDecalBlendMode_AdditiveString_NoError) {
    ASSERT_TRUE(DoString(
        "_h = uz.vfx:spawn_decal('mat/a.uzmat', {0,0,0}, {0,0,1})"));
    EXPECT_TRUE(DoString("uz.vfx:set_decal_blend_mode(_h, 'additive')"));
}

/// set_decal_blend_mode("multiply") does not raise a Lua error.
TEST_F(VFXLuaDecalLodCompositeTest,
       SetDecalBlendMode_MultiplyString_NoError) {
    ASSERT_TRUE(DoString(
        "_h = uz.vfx:spawn_decal('mat/a.uzmat', {0,0,0}, {0,0,1})"));
    EXPECT_TRUE(DoString("uz.vfx:set_decal_blend_mode(_h, 'multiply')"));
}

/// set_decal_blend_mode("screen") does not raise a Lua error.
TEST_F(VFXLuaDecalLodCompositeTest,
       SetDecalBlendMode_ScreenString_NoError) {
    ASSERT_TRUE(DoString(
        "_h = uz.vfx:spawn_decal('mat/a.uzmat', {0,0,0}, {0,0,1})"));
    EXPECT_TRUE(DoString("uz.vfx:set_decal_blend_mode(_h, 'screen')"));
}

/// set_decal_blend_mode with an unrecognized mode string must raise a Lua error.
TEST_F(VFXLuaDecalLodCompositeTest,
       SetDecalBlendMode_UnknownModeString_RaisesLuaError) {
    ASSERT_TRUE(DoString(
        "_h = uz.vfx:spawn_decal('mat/a.uzmat', {0,0,0}, {0,0,1})"));
    // pcall must catch the error and return false.
    ASSERT_TRUE(DoString(
        "_ok, _err = pcall(function() "
        "  uz.vfx:set_decal_blend_mode(_h, 'neon') "
        "end)"));
    EXPECT_FALSE(EvalBool("_ok"));
    EXPECT_TRUE(EvalBool("type(_err) == 'string'"));
}

// ─────────────────────────────────────────────────────────────────────────────
// set_decal_blend_mode / set_decal_normal_intensity — verify C++ state
// ─────────────────────────────────────────────────────────────────────────────

/// set_decal_blend_mode("additive") writes DecalBlendMode::Additive to the
/// component's blendMode field, verifiable via the C++ pointer.
TEST_F(VFXLuaDecalLodCompositeTest,
       SetDecalBlendMode_Additive_WritesCorrectEnumToCppComponent) {
    DecalAsset asset;
    asset.materialPath = "mat/test.uzmat";
    const Vec3 pos{0.0f, 0.0f, 0.0f};
    const Vec3 nrm{0.0f, 0.0f, 1.0f};
    VfxDecal* comp = decalManager_.SpawnDecal(asset, pos, nrm);
    ASSERT_NE(comp, nullptr);

    // Encode the pointer as a Lua integer handle matching EmitterToHandle's pattern.
    uintptr_t raw;
    std::memcpy(&raw, &comp, sizeof(raw));
    const lua_Integer handle = static_cast<lua_Integer>(raw);

    lua_State* L = LuaVM::GetState();
    lua_pushinteger(L, handle);
    lua_setglobal(L, "_dh");

    ASSERT_TRUE(DoString("uz.vfx:set_decal_blend_mode(_dh, 'additive')"));
    EXPECT_EQ(comp->blendMode, DecalBlendMode::Additive);
}

/// set_decal_normal_intensity(0.3) writes 0.3f to the C++ component field.
TEST_F(VFXLuaDecalLodCompositeTest,
       SetDecalNormalIntensity_ValidValue_WritesCppField) {
    DecalAsset asset;
    asset.materialPath = "mat/norm.uzmat";
    VfxDecal* comp = decalManager_.SpawnDecal(
        asset, Vec3{0,0,0}, Vec3{0,0,1});
    ASSERT_NE(comp, nullptr);

    uintptr_t raw;
    std::memcpy(&raw, &comp, sizeof(raw));
    lua_State* L = LuaVM::GetState();
    lua_pushinteger(L, static_cast<lua_Integer>(raw));
    lua_setglobal(L, "_dh2");

    ASSERT_TRUE(DoString("uz.vfx:set_decal_normal_intensity(_dh2, 0.3)"));
    EXPECT_NEAR(comp->normalIntensity, 0.3f, 1e-5f);
}

// ─────────────────────────────────────────────────────────────────────────────
// fade_out_decal / destroy_decal — effect on component state
// ─────────────────────────────────────────────────────────────────────────────

/// fade_out_decal(handle, 2.0) adjusts the component's lifetime so it expires
/// in 2 more seconds from its current age.
TEST_F(VFXLuaDecalLodCompositeTest,
       FadeOutDecal_ValidHandle_AdjustsComponentLifetime) {
    DecalAsset asset;
    asset.materialPath = "mat/smoke.uzmat";
    asset.lifetime     = 30.0f;
    VfxDecal* comp = decalManager_.SpawnDecal(
        asset, Vec3{0,0,0}, Vec3{0,1,0});
    ASSERT_NE(comp, nullptr);
    EXPECT_TRUE(comp->isActive);

    uintptr_t raw;
    std::memcpy(&raw, &comp, sizeof(raw));
    lua_State* L = LuaVM::GetState();
    lua_pushinteger(L, static_cast<lua_Integer>(raw));
    lua_setglobal(L, "_dh3");

    // age is 0, fade_out(2.0) => lifetime becomes age + 2.0 = 2.0.
    ASSERT_TRUE(DoString("uz.vfx:fade_out_decal(_dh3, 2.0)"));
    // lifetime should now be 2.0 (comp->age == 0 + 2.0).
    EXPECT_NEAR(comp->lifetime, 2.0f, 1e-4f);
    EXPECT_TRUE(comp->isActive); // not expired yet (age < new lifetime)
}

/// destroy_decal(handle) sets isActive = false on the component.
TEST_F(VFXLuaDecalLodCompositeTest,
       DestroyDecal_ValidHandle_SetsComponentInactive) {
    DecalAsset asset;
    asset.materialPath = "mat/dirt.uzmat";
    VfxDecal* comp = decalManager_.SpawnDecal(
        asset, Vec3{5,0,0}, Vec3{0,0,1});
    ASSERT_NE(comp, nullptr);
    EXPECT_TRUE(comp->isActive);

    uintptr_t raw;
    std::memcpy(&raw, &comp, sizeof(raw));
    lua_State* L = LuaVM::GetState();
    lua_pushinteger(L, static_cast<lua_Integer>(raw));
    lua_setglobal(L, "_dh4");

    ASSERT_TRUE(DoString("uz.vfx:destroy_decal(_dh4)"));
    EXPECT_FALSE(comp->isActive);
}

// ─────────────────────────────────────────────────────────────────────────────
// Graceful degradation — no DecalManager registered
// ─────────────────────────────────────────────────────────────────────────────

/// When the DecalManager is unregistered, get_decal_stats() returns a table
/// with all-zero fields rather than nil.
TEST_F(VFXLuaDecalLodCompositeTest,
       GetDecalStats_AfterUnregister_ReturnsZeroTable) {
    UnregisterDecalLuaBindings(LuaVM::GetState());
    ASSERT_TRUE(DoString("_s = uz.vfx:get_decal_stats()"));
    EXPECT_TRUE(EvalBool("type(_s) == 'table'"));
    EXPECT_DOUBLE_EQ(EvalNumber("_s.active_count"),   0.0);
    EXPECT_DOUBLE_EQ(EvalNumber("_s.pool_available"), 0.0);
    // Re-register so TearDown can call UnregisterDecalLuaBindings safely.
    RegisterDecalLuaBindings(LuaVM::GetState(), &decalManager_);
}

/// When the DecalManager is unregistered, spawn_decal() returns nil without
/// crashing.
TEST_F(VFXLuaDecalLodCompositeTest,
       SpawnDecal_AfterUnregister_ReturnsNil) {
    UnregisterDecalLuaBindings(LuaVM::GetState());
    ASSERT_TRUE(DoString(
        "_h = uz.vfx:spawn_decal('mat/x.uzmat', {0,0,0}, {0,0,1})"));
    EXPECT_TRUE(EvalBool("_h == nil"));
    RegisterDecalLuaBindings(LuaVM::GetState(), &decalManager_);
}

// =============================================================================
// 7.6 — LOD and budget query bindings
// =============================================================================

// ─────────────────────────────────────────────────────────────────────────────
// Helpers for LOD tests: create an initialized emitter in vfxSystem_
// ─────────────────────────────────────────────────────────────────────────────

namespace {

/// Initializes a ParticleEmitter from vfxSystem_'s budget and registers it.
/// Uses a CPU-only budget (no GPU device) — Initialize needs a real device for
/// GPU buffer creation, so we push the handle directly without GPU init by
/// registering the emitter without Initialize().
///
/// For LOD / budget tests we only need the emitter to appear in GetActiveEmitters(),
/// which does NOT require GPU buffer allocation.
void RegisterEmitterNaked(VFXSystem& sys, ParticleEmitter& emitter,
                          const std::string& graphPath = "vfx/fire.uzvfx")
{
    emitter.graphPath  = graphPath;
    emitter.maxParticles = 1000u;
    emitter.isEmitting   = true;
    emitter.initialized  = true; // pretend GPU init succeeded (CPU test only)
    sys.RegisterEmitter(&emitter);
}

} // namespace

// ─────────────────────────────────────────────────────────────────────────────
// set_lod_distances — writes to emitter->lodSettings
// ─────────────────────────────────────────────────────────────────────────────

/// set_lod_distances(handle, 30, 80, 150) must write those exact values into
/// the emitter's lodSettings.distances[0/1/2].
TEST_F(VFXLuaDecalLodCompositeTest,
       SetLodDistances_ValidDistances_WritesCppLodSettings) {
    ParticleEmitter emitter;
    RegisterEmitterNaked(vfxSystem_, emitter);

    lua_State* L = LuaVM::GetState();
    lua_pushinteger(L, EmitterHandleFor(&emitter));
    lua_setglobal(L, "_eh");

    ASSERT_TRUE(DoString("uz.vfx:set_lod_distances(_eh, 30, 80, 150)"));

    EXPECT_NEAR(emitter.lodSettings.distances[0], 30.0f,  1e-4f);
    EXPECT_NEAR(emitter.lodSettings.distances[1], 80.0f,  1e-4f);
    EXPECT_NEAR(emitter.lodSettings.distances[2], 150.0f, 1e-4f);

    vfxSystem_.UnregisterEmitter(&emitter);
}

/// set_lod_distances with non-increasing values must raise a Lua error.
TEST_F(VFXLuaDecalLodCompositeTest,
       SetLodDistances_NonIncreasing_RaisesLuaError) {
    ParticleEmitter emitter;
    RegisterEmitterNaked(vfxSystem_, emitter);

    lua_State* L = LuaVM::GetState();
    lua_pushinteger(L, EmitterHandleFor(&emitter));
    lua_setglobal(L, "_eh2");

    // d1 < d0 — violates the strictly increasing requirement.
    ASSERT_TRUE(DoString(
        "_ok, _err = pcall(function() "
        "  uz.vfx:set_lod_distances(_eh2, 100, 50, 200) "
        "end)"));
    EXPECT_FALSE(EvalBool("_ok"));

    vfxSystem_.UnregisterEmitter(&emitter);
}

/// set_lod_distances with d0 <= 0 must raise a Lua error.
TEST_F(VFXLuaDecalLodCompositeTest,
       SetLodDistances_ZeroFirstDistance_RaisesLuaError) {
    ParticleEmitter emitter;
    RegisterEmitterNaked(vfxSystem_, emitter);

    lua_State* L = LuaVM::GetState();
    lua_pushinteger(L, EmitterHandleFor(&emitter));
    lua_setglobal(L, "_eh3");

    ASSERT_TRUE(DoString(
        "_ok, _err = pcall(function() "
        "  uz.vfx:set_lod_distances(_eh3, 0, 50, 100) "
        "end)"));
    EXPECT_FALSE(EvalBool("_ok"));

    vfxSystem_.UnregisterEmitter(&emitter);
}

// ─────────────────────────────────────────────────────────────────────────────
// get_lod_level — returns VFXLodLevel as integer
// ─────────────────────────────────────────────────────────────────────────────

/// A freshly constructed emitter has currentLodLevel == Full (0).
/// get_lod_level must return 0.
TEST_F(VFXLuaDecalLodCompositeTest,
       GetLodLevel_FreshEmitter_ReturnsFull_Zero) {
    ParticleEmitter emitter;
    RegisterEmitterNaked(vfxSystem_, emitter);
    ASSERT_EQ(emitter.currentLodLevel, VFXLodLevel::Full);

    lua_State* L = LuaVM::GetState();
    lua_pushinteger(L, EmitterHandleFor(&emitter));
    lua_setglobal(L, "_lod_eh");

    const double level = EvalNumber("uz.vfx:get_lod_level(_lod_eh)");
    EXPECT_DOUBLE_EQ(level, 0.0);

    vfxSystem_.UnregisterEmitter(&emitter);
}

/// After manually setting currentLodLevel = Minimal (2) on the C++ side,
/// get_lod_level must return 2.
TEST_F(VFXLuaDecalLodCompositeTest,
       GetLodLevel_MinimalLevel_ReturnsTwo) {
    ParticleEmitter emitter;
    RegisterEmitterNaked(vfxSystem_, emitter);
    emitter.currentLodLevel = VFXLodLevel::Minimal;

    lua_State* L = LuaVM::GetState();
    lua_pushinteger(L, EmitterHandleFor(&emitter));
    lua_setglobal(L, "_lod_eh2");

    EXPECT_DOUBLE_EQ(EvalNumber("uz.vfx:get_lod_level(_lod_eh2)"), 2.0);

    vfxSystem_.UnregisterEmitter(&emitter);
}

/// After manually setting currentLodLevel = Culled (3),
/// get_lod_level must return 3.
TEST_F(VFXLuaDecalLodCompositeTest,
       GetLodLevel_CulledLevel_ReturnsThree) {
    ParticleEmitter emitter;
    RegisterEmitterNaked(vfxSystem_, emitter);
    emitter.currentLodLevel = VFXLodLevel::Culled;

    lua_State* L = LuaVM::GetState();
    lua_pushinteger(L, EmitterHandleFor(&emitter));
    lua_setglobal(L, "_lod_eh3");

    EXPECT_DOUBLE_EQ(EvalNumber("uz.vfx:get_lod_level(_lod_eh3)"), 3.0);

    vfxSystem_.UnregisterEmitter(&emitter);
}

// ─────────────────────────────────────────────────────────────────────────────
// set_lod_enabled — writes to emitter->lodEnabled
// ─────────────────────────────────────────────────────────────────────────────

/// set_lod_enabled(handle, false) writes false to emitter->lodEnabled.
TEST_F(VFXLuaDecalLodCompositeTest,
       SetLodEnabled_False_WritesLodEnabledFalse) {
    ParticleEmitter emitter;
    RegisterEmitterNaked(vfxSystem_, emitter);
    ASSERT_TRUE(emitter.lodEnabled); // default is true

    lua_State* L = LuaVM::GetState();
    lua_pushinteger(L, EmitterHandleFor(&emitter));
    lua_setglobal(L, "_lod_dis");

    ASSERT_TRUE(DoString("uz.vfx:set_lod_enabled(_lod_dis, false)"));
    EXPECT_FALSE(emitter.lodEnabled);

    vfxSystem_.UnregisterEmitter(&emitter);
}

/// set_lod_enabled(handle, true) restores lodEnabled to true after disabling.
TEST_F(VFXLuaDecalLodCompositeTest,
       SetLodEnabled_True_WritesLodEnabledTrue) {
    ParticleEmitter emitter;
    RegisterEmitterNaked(vfxSystem_, emitter);
    emitter.lodEnabled = false;

    lua_State* L = LuaVM::GetState();
    lua_pushinteger(L, EmitterHandleFor(&emitter));
    lua_setglobal(L, "_lod_en");

    ASSERT_TRUE(DoString("uz.vfx:set_lod_enabled(_lod_en, true)"));
    EXPECT_TRUE(emitter.lodEnabled);

    vfxSystem_.UnregisterEmitter(&emitter);
}

/// set_lod_enabled with a non-boolean argument must raise a Lua error.
TEST_F(VFXLuaDecalLodCompositeTest,
       SetLodEnabled_NonBooleanArg_RaisesLuaError) {
    ParticleEmitter emitter;
    RegisterEmitterNaked(vfxSystem_, emitter);

    lua_State* L = LuaVM::GetState();
    lua_pushinteger(L, EmitterHandleFor(&emitter));
    lua_setglobal(L, "_lod_nb");

    ASSERT_TRUE(DoString(
        "_ok2, _err2 = pcall(function() "
        "  uz.vfx:set_lod_enabled(_lod_nb, 1) "
        "end)"));
    EXPECT_FALSE(EvalBool("_ok2"));

    vfxSystem_.UnregisterEmitter(&emitter);
}

// ─────────────────────────────────────────────────────────────────────────────
// get_budget_stats — table fields and values
// ─────────────────────────────────────────────────────────────────────────────

/// get_budget_stats() returns a table with a 'total' field equal to 100000
/// (the value passed to Initialize()).
TEST_F(VFXLuaDecalLodCompositeTest,
       GetBudgetStats_TotalMatchesInitializedBudget) {
    ASSERT_TRUE(DoString("_bs = uz.vfx:get_budget_stats()"));
    EXPECT_DOUBLE_EQ(EvalNumber("_bs.total"), 100000.0);
}

/// get_budget_stats() returns a 'used' field of 0 when no emitters have
/// allocated particles.
TEST_F(VFXLuaDecalLodCompositeTest,
       GetBudgetStats_Used_IsZero_WhenNoParticlesAllocated) {
    ASSERT_TRUE(DoString("_bs2 = uz.vfx:get_budget_stats()"));
    EXPECT_DOUBLE_EQ(EvalNumber("_bs2.used"), 0.0);
}

/// get_budget_stats() returns an 'available' field equal to total when used == 0.
TEST_F(VFXLuaDecalLodCompositeTest,
       GetBudgetStats_Available_EqualsTotalWhenNoAllocations) {
    ASSERT_TRUE(DoString("_bs3 = uz.vfx:get_budget_stats()"));
    EXPECT_DOUBLE_EQ(EvalNumber("_bs3.available"), 100000.0);
}

/// utilization_pct is 0.0 when no particles are allocated.
TEST_F(VFXLuaDecalLodCompositeTest,
       GetBudgetStats_UtilizationPct_IsZeroWhenIdle) {
    ASSERT_TRUE(DoString("_bs4 = uz.vfx:get_budget_stats()"));
    EXPECT_DOUBLE_EQ(EvalNumber("_bs4.utilization_pct"), 0.0);
}

// ─────────────────────────────────────────────────────────────────────────────
// get_total_alive — initial state is zero
// ─────────────────────────────────────────────────────────────────────────────

/// Before any Tick(), GetTotalAliveParticles is 0.
/// get_total_alive() must return 0.
TEST_F(VFXLuaDecalLodCompositeTest,
       GetTotalAlive_NoTickYet_ReturnsZero) {
    EXPECT_DOUBLE_EQ(EvalNumber("uz.vfx:get_total_alive()"), 0.0);
}

// ─────────────────────────────────────────────────────────────────────────────
// get_emitter_count — tracks registered emitters
// ─────────────────────────────────────────────────────────────────────────────

/// With no emitters registered, get_emitter_count() returns 0.
TEST_F(VFXLuaDecalLodCompositeTest,
       GetEmitterCount_NoEmitters_ReturnsZero) {
    EXPECT_DOUBLE_EQ(EvalNumber("uz.vfx:get_emitter_count()"), 0.0);
}

/// After registering 2 emitters, get_emitter_count() returns 2.
TEST_F(VFXLuaDecalLodCompositeTest,
       GetEmitterCount_TwoRegisteredEmitters_ReturnsTwo) {
    ParticleEmitter e1, e2;
    RegisterEmitterNaked(vfxSystem_, e1, "vfx/fire.uzvfx");
    RegisterEmitterNaked(vfxSystem_, e2, "vfx/smoke.uzvfx");

    EXPECT_DOUBLE_EQ(EvalNumber("uz.vfx:get_emitter_count()"), 2.0);

    vfxSystem_.UnregisterEmitter(&e1);
    vfxSystem_.UnregisterEmitter(&e2);
}

/// After unregistering both emitters, get_emitter_count() drops back to 0.
TEST_F(VFXLuaDecalLodCompositeTest,
       GetEmitterCount_AfterUnregisterAll_ReturnsZero) {
    ParticleEmitter e1, e2;
    RegisterEmitterNaked(vfxSystem_, e1, "vfx/fire.uzvfx");
    RegisterEmitterNaked(vfxSystem_, e2, "vfx/smoke.uzvfx");

    vfxSystem_.UnregisterEmitter(&e1);
    vfxSystem_.UnregisterEmitter(&e2);

    EXPECT_DOUBLE_EQ(EvalNumber("uz.vfx:get_emitter_count()"), 0.0);
}

// =============================================================================
// 7.7 — Composite spawn/stop
// =============================================================================

// ─────────────────────────────────────────────────────────────────────────────
// spawn_composite — nonexistent file returns empty table
// ─────────────────────────────────────────────────────────────────────────────

/// spawn_composite with a path that doesn't exist on disk must return an empty
/// table (not nil) and must not crash.
TEST_F(VFXLuaDecalLodCompositeTest,
       SpawnComposite_NonexistentFile_ReturnsEmptyTable) {
    ASSERT_TRUE(DoString(
        "_handles = uz.vfx:spawn_composite("
        "  'vfx/nonexistent.uzvfx_composite', {0,0,0})"));
    EXPECT_TRUE(EvalBool("type(_handles) == 'table'"));
    EXPECT_DOUBLE_EQ(EvalNumber("#_handles"), 0.0);
}

// ─────────────────────────────────────────────────────────────────────────────
// spawn_composite — valid file with matching registered emitters
// ─────────────────────────────────────────────────────────────────────────────

/// spawn_composite loaded from a temp .uzvfx_composite file returns a table
/// of handles for each matched emitter and sets startDelay correctly.
TEST_F(VFXLuaDecalLodCompositeTest,
       SpawnComposite_ValidFile_MatchingEmitters_ReturnsHandlesAndSetsDelay) {
    // Register two emitters whose graphPaths match the composite entries.
    ParticleEmitter fire, smoke;
    RegisterEmitterNaked(vfxSystem_, fire,  "vfx/fire.uzvfx");
    RegisterEmitterNaked(vfxSystem_, smoke, "vfx/smoke.uzvfx");

    // Write a temp .uzvfx_composite file into a directory served by a VFS:
    // scripts may only read composites through the registered VFS.
    const auto vfsRoot = std::filesystem::temp_directory_path() / "uz_test_composite_vfs";
    std::filesystem::create_directories(vfsRoot);
    const auto tmpPath = vfsRoot / "uz_test_composite.uzvfx_composite";
    {
        std::ofstream f(tmpPath);
        ASSERT_TRUE(f.is_open()) << "Could not write temp composite file";
        f << R"({
  "format": "uzvfx_composite",
  "version": 1,
  "name": "explosion",
  "emitters": [
    { "name": "fire",  "graph": "vfx/fire.uzvfx",  "offset": [0,0,0],   "delay": 0.0 },
    { "name": "smoke", "graph": "vfx/smoke.uzvfx", "offset": [0,0.5,0], "delay": 0.25 }
  ]
})";
    }

    LooseFileVFS vfs(vfsRoot.string());
    lua_State* L = LuaVM::GetState();
    LuaSetVFS(L, &vfs);

    ASSERT_TRUE(DoString(
        "_comp_handles = uz.vfx:spawn_composite('uz_test_composite.uzvfx_composite', {10, 20, 30})"));
    LuaSetVFS(L, nullptr);

    // Both emitters must be in the returned handles table.
    EXPECT_TRUE(EvalBool("type(_comp_handles) == 'table'"));
    EXPECT_DOUBLE_EQ(EvalNumber("#_comp_handles"), 2.0);

    // 'fire' has delay 0 — startDelay must be 0.
    EXPECT_NEAR(fire.startDelay,   0.0f,   1e-4f);
    // 'smoke' has delay 0.25 — startDelay must be 0.25.
    EXPECT_NEAR(smoke.startDelay,  0.25f,  1e-4f);

    // timeSinceRegistration is reset to 0 for both.
    EXPECT_NEAR(fire.timeSinceRegistration,  0.0f, 1e-4f);
    EXPECT_NEAR(smoke.timeSinceRegistration, 0.0f, 1e-4f);

    // isEmitting is set to true for both.
    EXPECT_TRUE(fire.isEmitting);
    EXPECT_TRUE(smoke.isEmitting);

    std::error_code ec;
    std::filesystem::remove_all(vfsRoot, ec);
    vfxSystem_.UnregisterEmitter(&fire);
    vfxSystem_.UnregisterEmitter(&smoke);
}

/// spawn_composite with a composite containing only unmatched entries returns
/// an empty table (not nil).
TEST_F(VFXLuaDecalLodCompositeTest,
       SpawnComposite_NoMatchingEmitters_ReturnsEmptyTable) {
    // No emitters registered — all entries will be unmatched.
    const auto vfsRoot = std::filesystem::temp_directory_path() / "uz_test_composite_nomatch_vfs";
    std::filesystem::create_directories(vfsRoot);
    const auto tmpPath = vfsRoot / "uz_test_composite_nomatch.uzvfx_composite";
    {
        std::ofstream f(tmpPath);
        ASSERT_TRUE(f.is_open());
        f << R"({
  "format": "uzvfx_composite",
  "version": 1,
  "name": "orphan",
  "emitters": [
    { "name": "debris", "graph": "vfx/debris.uzvfx", "offset": [0,0,0], "delay": 0.0 }
  ]
})";
    }

    LooseFileVFS vfs(vfsRoot.string());
    lua_State* L = LuaVM::GetState();
    LuaSetVFS(L, &vfs);

    ASSERT_TRUE(DoString(
        "_empty_handles = uz.vfx:spawn_composite('uz_test_composite_nomatch.uzvfx_composite', {0,0,0})"));
    LuaSetVFS(L, nullptr);
    EXPECT_TRUE(EvalBool("type(_empty_handles) == 'table'"));
    EXPECT_DOUBLE_EQ(EvalNumber("#_empty_handles"), 0.0);

    std::error_code ec;
    std::filesystem::remove_all(vfsRoot, ec);
}

// ─────────────────────────────────────────────────────────────────────────────
// stop_composite — empty table is a no-op
// ─────────────────────────────────────────────────────────────────────────────

/// stop_composite({}) with an empty table must not raise an error and must
/// not modify any emitter state.
TEST_F(VFXLuaDecalLodCompositeTest,
       StopComposite_EmptyTable_IsNoOpAndDoesNotCrash) {
    ParticleEmitter emitter;
    RegisterEmitterNaked(vfxSystem_, emitter);
    emitter.isEmitting = true;

    EXPECT_TRUE(DoString("uz.vfx:stop_composite({})"));
    EXPECT_TRUE(emitter.isEmitting); // unchanged

    vfxSystem_.UnregisterEmitter(&emitter);
}

// ─────────────────────────────────────────────────────────────────────────────
// stop_composite — sets isEmitting = false on matched emitters
// ─────────────────────────────────────────────────────────────────────────────

/// stop_composite with valid handles sets isEmitting = false on each resolved
/// emitter without affecting unrelated emitters.
TEST_F(VFXLuaDecalLodCompositeTest,
       StopComposite_ValidHandles_SetsIsEmittingFalse) {
    ParticleEmitter e1, e2, e3;
    RegisterEmitterNaked(vfxSystem_, e1, "vfx/a.uzvfx");
    RegisterEmitterNaked(vfxSystem_, e2, "vfx/b.uzvfx");
    RegisterEmitterNaked(vfxSystem_, e3, "vfx/c.uzvfx");

    e1.isEmitting = true;
    e2.isEmitting = true;
    e3.isEmitting = true; // will NOT be stopped

    lua_State* L = LuaVM::GetState();
    // Build the handles table in Lua from C++ pointers directly.
    lua_newtable(L);
    lua_pushinteger(L, EmitterHandleFor(&e1));
    lua_rawseti(L, -2, 1);
    lua_pushinteger(L, EmitterHandleFor(&e2));
    lua_rawseti(L, -2, 2);
    lua_setglobal(L, "_stop_handles");

    ASSERT_TRUE(DoString("uz.vfx:stop_composite(_stop_handles)"));

    EXPECT_FALSE(e1.isEmitting);
    EXPECT_FALSE(e2.isEmitting);
    EXPECT_TRUE(e3.isEmitting); // unaffected

    vfxSystem_.UnregisterEmitter(&e1);
    vfxSystem_.UnregisterEmitter(&e2);
    vfxSystem_.UnregisterEmitter(&e3);
}

/// stop_composite is idempotent: calling it twice on the same handles leaves
/// isEmitting = false.
TEST_F(VFXLuaDecalLodCompositeTest,
       StopComposite_CalledTwice_IdempotentResult) {
    ParticleEmitter emitter;
    RegisterEmitterNaked(vfxSystem_, emitter);
    emitter.isEmitting = true;

    lua_State* L = LuaVM::GetState();
    lua_newtable(L);
    lua_pushinteger(L, EmitterHandleFor(&emitter));
    lua_rawseti(L, -2, 1);
    lua_setglobal(L, "_idem_handles");

    ASSERT_TRUE(DoString("uz.vfx:stop_composite(_idem_handles)"));
    ASSERT_FALSE(emitter.isEmitting);
    ASSERT_TRUE(DoString("uz.vfx:stop_composite(_idem_handles)"));
    EXPECT_FALSE(emitter.isEmitting);

    vfxSystem_.UnregisterEmitter(&emitter);
}
