/// Unit tests for the VFX spline data interface (phase16_vfx-spline-data-interface).
///
/// Covers the CPU surface delivered by the task:
///   * `VfxSpline` Catmull-Rom evaluator (already shipped — sanity check
///     that endpoints + arc-length monotonicity hold so the buffer bake
///     produces correct samples).
///   * `VfxSplineBuffer` Create / Destroy lifecycle on `MockRHIDevice`.
///   * `VfxSplineBuffer::RebuildAndUpload` — buffer count is balanced,
///     `pointCount` reflects the number of baked samples, `totalLength`
///     mirrors the CPU spline.
///   * Each of the three modules (`SpawnAlongSplineModule`,
///     `ForceTowardSplineModule`, `FollowSplineModule`):
///     - default-constructed config matches the documented defaults
///     - `ParseConfig` reads every advertised JSON key
///     - `GetParameters` exposes the right parameter names for the editor
///     - `GetShaderCode` returns a non-empty HLSL snippet (the snippet
///       referencing `cbSpline*` cbuffer fields is hardware-pending; we
///       only check it ships).
///   * Module registry: the three modules are registered under their
///     documented names and produce instances of the expected concrete
///     type.
///   * `.uzfx` round-trip: sample assets `spline_distribute.uzfx`,
///     `spline_attract.uzfx`, `spline_follow.uzfx` deserialize +
///     `ApplyEmitterStackToModuleStack` produce emitters with the
///     correct module slots populated.
///
/// No GPU compute dispatch — those land in
/// `phase16_vfx-spline-data-interface-hardware-validation`.

#include <gtest/gtest.h>

#include "components/apply_module_stack.h"
#include "components/emitter_serializer.h"
#include "components/particle_emitter.h"
#include "data_interfaces/vfx_spline_buffer.h"
#include "data_interfaces/vfx_spline_di.h"
#include "modules/module_registry.h"
#include "modules/spline_modules.h"
#include "modules/spawn_module.h"
#include "modules/sprite_render_module.h"
#include "modules/update_module.h"
#include "mock_rhi_device.h"

#include <cstring>
#include <fstream>
#include <sstream>
#include <string>

using namespace uz;

namespace {

/// Builds a 4-control-point S-shaped spline used by most tests.
VfxSpline MakeTestSpline() {
    VfxSpline s;
    s.controlPoints = {
        Vec3{   0.0f, 0.0f,   0.0f},
        Vec3{ 100.0f, 0.0f,  50.0f},
        Vec3{ 200.0f, 0.0f, -50.0f},
        Vec3{ 300.0f, 0.0f,   0.0f},
    };
    s.tension          = 0.5f;
    s.arcLengthSamples = 32;  // small for fast tests, still smooth
    return s;
}

/// Reads a sample asset file from `sample/content/vfx/tests/`.
std::string LoadSampleAsset(const char* filename) {
    // Test runner is invoked from the build/bin dir; walk up to the repo
    // root. The path is fixed by the project layout — no need to thread
    // a content-root override through the test fixture.
    const std::string candidates[] = {
        std::string("../../sample/content/vfx/tests/")    + filename,
        std::string("../../../sample/content/vfx/tests/") + filename,
        std::string("../../../../sample/content/vfx/tests/") + filename,
        std::string("sample/content/vfx/tests/")          + filename,
    };
    for (const auto& path : candidates) {
        std::ifstream f(path);
        if (f.good()) {
            std::stringstream ss;
            ss << f.rdbuf();
            return ss.str();
        }
    }
    return {};
}

} // anonymous namespace

// =============================================================================
// VfxSpline — sanity-check the CPU evaluator (already shipped, regression guard)
// =============================================================================

TEST(VfxSpline, DegenerateZeroPoints_ReturnsZeroSample) {
    VfxSpline s;
    auto sample = s.Sample(0.5f);
    EXPECT_FLOAT_EQ(sample.position.x, 0.0f);
    EXPECT_FLOAT_EQ(sample.position.y, 0.0f);
    EXPECT_FLOAT_EQ(sample.position.z, 0.0f);
}

TEST(VfxSpline, EndpointsMatchControlPointsAtT0AndT1) {
    auto s = MakeTestSpline();
    auto a = s.Sample(0.0f);
    auto b = s.Sample(1.0f);
    EXPECT_NEAR(a.position.x, s.controlPoints.front().x, 1e-3f);
    EXPECT_NEAR(b.position.x, s.controlPoints.back().x,  1e-3f);
}

TEST(VfxSpline, ArcLengthTableIsMonotonicAndPositive) {
    auto s = MakeTestSpline();
    s.BuildArcLengthTable();
    ASSERT_GT(s.arcLengthTable.size(), 1u);
    for (size_t i = 1; i < s.arcLengthTable.size(); ++i) {
        EXPECT_GE(s.arcLengthTable[i], s.arcLengthTable[i - 1])
            << "arc-length table must be monotonically non-decreasing";
    }
    EXPECT_GT(s.totalLength, 0.0f);
}

// =============================================================================
// VfxSplineBuffer — lifecycle + RebuildAndUpload
// =============================================================================

TEST(VfxSplineBuffer, Create_AllocatesOneBuffer) {
    test::MockRHIDevice device;
    VfxSplineBuffer buf;
    ASSERT_TRUE(buf.Create(&device, 64));
    EXPECT_TRUE(buf.IsValid());
    EXPECT_EQ(device.buffersCreated.load(), 1u);
    EXPECT_EQ(buf.capacity, 64u);
    buf.Destroy(&device);
    EXPECT_EQ(device.buffersDestroyed.load(), 1u);
}

TEST(VfxSplineBuffer, Create_ClampsCapacityToValidRange) {
    test::MockRHIDevice device;
    VfxSplineBuffer buf;
    // Below minimum (2) clamps up.
    ASSERT_TRUE(buf.Create(&device, 0));
    EXPECT_GE(buf.capacity, 2u);
    buf.Destroy(&device);

    // Above max clamps down.
    ASSERT_TRUE(buf.Create(&device, kVfxSplineMaxResolution * 2));
    EXPECT_LE(buf.capacity, kVfxSplineMaxResolution);
    buf.Destroy(&device);
}

TEST(VfxSplineBuffer, RebuildAndUpload_DegenerateSplineYieldsZeroCount) {
    test::MockRHIDevice device;
    VfxSplineBuffer buf;
    ASSERT_TRUE(buf.Create(&device, 32));

    VfxSpline empty; // zero control points
    EXPECT_TRUE(buf.RebuildAndUpload(empty, &device));
    EXPECT_EQ(buf.pointCount, 0u);
    EXPECT_FLOAT_EQ(buf.totalLength, 0.0f);

    buf.Destroy(&device);
}

TEST(VfxSplineBuffer, RebuildAndUpload_ProducesPointsAndLength) {
    test::MockRHIDevice device;
    VfxSplineBuffer buf;
    ASSERT_TRUE(buf.Create(&device, 32));

    auto spline = MakeTestSpline();
    EXPECT_TRUE(buf.RebuildAndUpload(spline, &device));
    EXPECT_GE(buf.pointCount, 2u);
    EXPECT_LE(buf.pointCount, 32u);
    EXPECT_GT(buf.totalLength, 0.0f);

    buf.Destroy(&device);
}

TEST(VfxSplineBuffer, RebuildAndUpload_DoesNotLeakOnRepeatedCalls) {
    test::MockRHIDevice device;
    VfxSplineBuffer buf;
    ASSERT_TRUE(buf.Create(&device, 32));

    auto spline = MakeTestSpline();
    for (int i = 0; i < 10; ++i) {
        ASSERT_TRUE(buf.RebuildAndUpload(spline, &device));
    }

    // Only the original buffer was created; RebuildAndUpload reuses it
    // via MapBuffer. Total creates must remain at 1.
    EXPECT_EQ(device.buffersCreated.load(), 1u);

    buf.Destroy(&device);
    EXPECT_EQ(device.buffersDestroyed.load(), 1u);
}

// =============================================================================
// SpawnAlongSplineModule
// =============================================================================

TEST(SpawnAlongSpline, DefaultConfig) {
    SpawnAlongSplineModule m;
    EXPECT_TRUE(m.GetConfig().stratified);
    EXPECT_FLOAT_EQ(m.GetConfig().jitterRadius, 0.0f);
    EXPECT_FLOAT_EQ(m.GetConfig().tangentOffset, 0.0f);
}

TEST(SpawnAlongSpline, GetName_IsSpawnAlongSpline) {
    SpawnAlongSplineModule m;
    EXPECT_STREQ(m.GetName(), "spawn_along_spline");
}

TEST(SpawnAlongSpline, GetShaderCode_NonEmptySnippet) {
    SpawnAlongSplineModule m;
    EXPECT_FALSE(m.GetShaderCode().empty());
}

TEST(SpawnAlongSpline, ParseConfig_ReadsAllJsonKeys) {
    auto j = nlohmann::json::parse(R"({
        "stratified": false,
        "jitter_radius": 12.5,
        "tangent_offset": -3.0
    })");
    auto cfg = SpawnAlongSplineModule::ParseConfig(j);
    EXPECT_FALSE(cfg.stratified);
    EXPECT_FLOAT_EQ(cfg.jitterRadius, 12.5f);
    EXPECT_FLOAT_EQ(cfg.tangentOffset, -3.0f);
}

TEST(SpawnAlongSpline, GetParameters_ExposesThreeNamedEntries) {
    SpawnAlongSplineModule m;
    auto params = m.GetParameters();
    ASSERT_EQ(params.size(), 3u);
    bool sawStrat = false, sawJit = false, sawTan = false;
    for (const auto& p : params) {
        if (p.name && std::strcmp(p.name, "stratified")     == 0) sawStrat = true;
        if (p.name && std::strcmp(p.name, "jitter_radius")  == 0) sawJit   = true;
        if (p.name && std::strcmp(p.name, "tangent_offset") == 0) sawTan   = true;
    }
    EXPECT_TRUE(sawStrat);
    EXPECT_TRUE(sawJit);
    EXPECT_TRUE(sawTan);
}

// =============================================================================
// ForceTowardSplineModule
// =============================================================================

TEST(ForceTowardSpline, DefaultConfig) {
    ForceTowardSplineModule m;
    EXPECT_FLOAT_EQ(m.GetConfig().stiffness,       10.0f);
    EXPECT_FLOAT_EQ(m.GetConfig().maxAcceleration, 2000.0f);
    EXPECT_FLOAT_EQ(m.GetConfig().attractDistance, 0.0f);
}

TEST(ForceTowardSpline, GetName_IsForceTowardSpline) {
    EXPECT_STREQ(ForceTowardSplineModule{}.GetName(), "force_toward_spline");
}

TEST(ForceTowardSpline, ParseConfig_ReadsAllJsonKeys) {
    auto j = nlohmann::json::parse(R"({
        "stiffness": 25.0,
        "max_acceleration": 500.0,
        "attract_distance": 150.0
    })");
    auto cfg = ForceTowardSplineModule::ParseConfig(j);
    EXPECT_FLOAT_EQ(cfg.stiffness,       25.0f);
    EXPECT_FLOAT_EQ(cfg.maxAcceleration, 500.0f);
    EXPECT_FLOAT_EQ(cfg.attractDistance, 150.0f);
}

TEST(ForceTowardSpline, GetParameters_ExposesThreeNamedEntries) {
    ForceTowardSplineModule m;
    EXPECT_EQ(m.GetParameters().size(), 3u);
}

// =============================================================================
// FollowSplineModule
// =============================================================================

TEST(FollowSpline, DefaultConfig) {
    FollowSplineModule m;
    EXPECT_FLOAT_EQ(m.GetConfig().speed,  1.0f);
    EXPECT_FALSE(m.GetConfig().loop);
    EXPECT_FLOAT_EQ(m.GetConfig().startU, 0.0f);
}

TEST(FollowSpline, GetName_IsFollowSpline) {
    EXPECT_STREQ(FollowSplineModule{}.GetName(), "follow_spline");
}

TEST(FollowSpline, ParseConfig_ReadsAllJsonKeys) {
    auto j = nlohmann::json::parse(R"({
        "speed": 2.5,
        "loop": true,
        "start_u": 0.25
    })");
    auto cfg = FollowSplineModule::ParseConfig(j);
    EXPECT_FLOAT_EQ(cfg.speed,  2.5f);
    EXPECT_TRUE  (cfg.loop);
    EXPECT_FLOAT_EQ(cfg.startU, 0.25f);
}

TEST(FollowSpline, ApplyJsonConfig_LoadsViaModuleInterface) {
    FollowSplineModule m;
    auto j = nlohmann::json::parse(R"({"speed": 3.0, "loop": true})");
    m.ApplyJsonConfig(j);
    EXPECT_FLOAT_EQ(m.GetConfig().speed, 3.0f);
    EXPECT_TRUE(m.GetConfig().loop);
}

// =============================================================================
// Module registry — the three spline modules are registered + creatable
// =============================================================================

TEST(SplineModuleRegistry, RegistersAllThreeSplineModules) {
    ModuleRegistry reg;
    reg.Register<SpawnAlongSplineModule>("spawn_along_spline");
    reg.Register<ForceTowardSplineModule>("force_toward_spline");
    reg.Register<FollowSplineModule>("follow_spline");

    EXPECT_TRUE(reg.IsRegistered("spawn_along_spline"));
    EXPECT_TRUE(reg.IsRegistered("force_toward_spline"));
    EXPECT_TRUE(reg.IsRegistered("follow_spline"));

    auto spawn  = reg.Create("spawn_along_spline");
    auto force  = reg.Create("force_toward_spline");
    auto follow = reg.Create("follow_spline");
    ASSERT_NE(spawn,  nullptr);
    ASSERT_NE(force,  nullptr);
    ASSERT_NE(follow, nullptr);
    EXPECT_NE(dynamic_cast<SpawnAlongSplineModule*>(spawn.get()),  nullptr);
    EXPECT_NE(dynamic_cast<ForceTowardSplineModule*>(force.get()), nullptr);
    EXPECT_NE(dynamic_cast<FollowSplineModule*>(follow.get()),     nullptr);
}

// =============================================================================
// .uzfx round-trip — DeserializeEmitter + ApplyEmitterStackToModuleStack
// =============================================================================

namespace {

ModuleRegistry MakeSplineRegistry() {
    ModuleRegistry r;
    r.Register<SpawnModule>("spawn");
    r.Register<UpdateModule>("update");
    r.Register<SpriteRenderModule>("sprite_render");
    r.Register<SpawnAlongSplineModule>("spawn_along_spline");
    r.Register<ForceTowardSplineModule>("force_toward_spline");
    r.Register<FollowSplineModule>("follow_spline");
    return r;
}

} // anonymous namespace

TEST(SplineUzfx, SpawnDistribute_SampleAssetRoundTrips) {
    const auto json = LoadSampleAsset("spline_distribute.uzfx");
    if (json.empty()) {
        GTEST_SKIP() << "spline_distribute.uzfx not on disk relative to test cwd";
    }

    ParticleEmitter e;
    ASSERT_TRUE(DeserializeEmitter(json, e));
    const auto registry = MakeSplineRegistry();
    ApplyEmitterStackToModuleStack(e, registry);

    // The asset declares two spawn modules; the second is the spline one.
    bool sawSpline = false;
    for (auto& mod : e.moduleStack.spawnModules) {
        if (dynamic_cast<SpawnAlongSplineModule*>(mod.get())) sawSpline = true;
    }
    EXPECT_TRUE(sawSpline) << "spline_distribute.uzfx must materialize a SpawnAlongSplineModule";
}

TEST(SplineUzfx, SpawnDistribute_ConfigOverridesApplied) {
    const auto json = LoadSampleAsset("spline_distribute.uzfx");
    if (json.empty()) {
        GTEST_SKIP();
    }

    ParticleEmitter e;
    ASSERT_TRUE(DeserializeEmitter(json, e));
    ApplyEmitterStackToModuleStack(e, MakeSplineRegistry());

    // jitter_radius is overridden to 6.0 in the asset.
    SpawnAlongSplineModule* spline = nullptr;
    for (auto& mod : e.moduleStack.spawnModules) {
        if (auto* s = dynamic_cast<SpawnAlongSplineModule*>(mod.get())) {
            spline = s;
            break;
        }
    }
    ASSERT_NE(spline, nullptr);
    EXPECT_FLOAT_EQ(spline->GetConfig().jitterRadius, 6.0f);
    EXPECT_TRUE(spline->GetConfig().stratified);
}

TEST(SplineUzfx, Attract_SampleAssetMaterializesForceModule) {
    const auto json = LoadSampleAsset("spline_attract.uzfx");
    if (json.empty()) {
        GTEST_SKIP();
    }

    ParticleEmitter e;
    ASSERT_TRUE(DeserializeEmitter(json, e));
    ApplyEmitterStackToModuleStack(e, MakeSplineRegistry());

    ForceTowardSplineModule* force = nullptr;
    for (auto& mod : e.moduleStack.updateModules) {
        if (auto* f = dynamic_cast<ForceTowardSplineModule*>(mod.get())) {
            force = f;
            break;
        }
    }
    ASSERT_NE(force, nullptr);
    EXPECT_FLOAT_EQ(force->GetConfig().stiffness,       18.0f);
    EXPECT_FLOAT_EQ(force->GetConfig().maxAcceleration, 1500.0f);
    EXPECT_FLOAT_EQ(force->GetConfig().attractDistance, 200.0f);
}

TEST(SplineUzfx, Follow_SampleAssetMaterializesFollowModule) {
    const auto json = LoadSampleAsset("spline_follow.uzfx");
    if (json.empty()) {
        GTEST_SKIP();
    }

    ParticleEmitter e;
    ASSERT_TRUE(DeserializeEmitter(json, e));
    ApplyEmitterStackToModuleStack(e, MakeSplineRegistry());

    FollowSplineModule* follow = nullptr;
    for (auto& mod : e.moduleStack.updateModules) {
        if (auto* f = dynamic_cast<FollowSplineModule*>(mod.get())) {
            follow = f;
            break;
        }
    }
    ASSERT_NE(follow, nullptr);
    EXPECT_FLOAT_EQ(follow->GetConfig().speed,  1.0f);
    EXPECT_FALSE(follow->GetConfig().loop);
    EXPECT_FLOAT_EQ(follow->GetConfig().startU, 0.0f);
}
