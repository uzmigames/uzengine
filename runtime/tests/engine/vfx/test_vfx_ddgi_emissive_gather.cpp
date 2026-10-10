/// Unit tests for the VFX -> DDGI emissive gather path
/// (phase16_vfx-lumen-gi-contribution).
///
/// Covers the headless-implementable surface:
///   * `VfxDDGIBuildSampleForEmitter` — pure helper that converts a
///     single emitter's CPU state into a `VfxEmissiveSample`. Verifies
///     the influence-radius floor, the alive-fraction scaling, the
///     `emissive == 0` early-out, and that color channels stay
///     proportional to the input.
///   * The two CVars (`r.VFX.DDGIInjection`,
///     `r.VFX.DDGIInjection.MaxEmitters`) are registered when the VFX
///     system initialises and respond to runtime sets.
///
/// The full GPU-side gather + DDGI probe-update integration is gated by
/// `phase16_vfx-lumen-gi-hardware-validation` — those tests need a DX12
/// device.

#include <gtest/gtest.h>

#include "engine/core/cvar.h"
#include "engine/vfx/lighting/vfx_ddgi_injection.h"
#include "engine/vfx/vfx_system.h"
#include "mock_rhi_device.h"

using namespace uz;

// =============================================================================
// VfxDDGIBuildSampleForEmitter — pure helper
// =============================================================================

namespace {

/// Tightly-controlled fixture so individual tests express intent in one
/// or two lines without repeating the same arguments.
constexpr Vec3  kAtOrigin{0.0f, 0.0f, 0.0f};
constexpr Vec3  kWhite   {1.0f, 1.0f, 1.0f};
constexpr Vec3  kRed     {1.0f, 0.0f, 0.0f};

} // anonymous namespace

TEST(VfxDDGIBuildSample, ZeroEmissive_ProducesZeroRadiance) {
    auto s = VfxDDGIBuildSampleForEmitter(
        kAtOrigin, kWhite, /*emissive=*/0.0f,
        /*alive=*/100u, /*max=*/100u, /*spawnRadius=*/50.0f);
    EXPECT_FLOAT_EQ(s.emissive.x, 0.0f);
    EXPECT_FLOAT_EQ(s.emissive.y, 0.0f);
    EXPECT_FLOAT_EQ(s.emissive.z, 0.0f);
}

TEST(VfxDDGIBuildSample, ZeroAlive_ProducesZeroRadiance) {
    auto s = VfxDDGIBuildSampleForEmitter(
        kAtOrigin, kWhite, /*emissive=*/5.0f,
        /*alive=*/0u, /*max=*/100u, /*spawnRadius=*/50.0f);
    EXPECT_FLOAT_EQ(s.emissive.x, 0.0f);
    EXPECT_FLOAT_EQ(s.emissive.y, 0.0f);
    EXPECT_FLOAT_EQ(s.emissive.z, 0.0f);
}

TEST(VfxDDGIBuildSample, FullEmitter_FullIntensity) {
    // alive == max => alive fraction is 1.0, so radiance == color * intensity.
    auto s = VfxDDGIBuildSampleForEmitter(
        kAtOrigin, kRed, /*emissive=*/8.0f,
        /*alive=*/200u, /*max=*/200u, /*spawnRadius=*/50.0f);
    EXPECT_FLOAT_EQ(s.emissive.x, 8.0f);
    EXPECT_FLOAT_EQ(s.emissive.y, 0.0f);
    EXPECT_FLOAT_EQ(s.emissive.z, 0.0f);
}

TEST(VfxDDGIBuildSample, HalfAlive_HalfIntensity) {
    auto s = VfxDDGIBuildSampleForEmitter(
        kAtOrigin, kWhite, /*emissive=*/4.0f,
        /*alive=*/50u, /*max=*/100u, /*spawnRadius=*/50.0f);
    EXPECT_FLOAT_EQ(s.emissive.x, 2.0f);
    EXPECT_FLOAT_EQ(s.emissive.y, 2.0f);
    EXPECT_FLOAT_EQ(s.emissive.z, 2.0f);
}

TEST(VfxDDGIBuildSample, OverpopulatedEmitter_ClampsToFullIntensity) {
    // alive > max is an authoring bug elsewhere, but we still want the
    // DDGI feed to behave sanely — clamp the alive fraction to 1.
    auto s = VfxDDGIBuildSampleForEmitter(
        kAtOrigin, kWhite, /*emissive=*/3.0f,
        /*alive=*/500u, /*max=*/100u, /*spawnRadius=*/50.0f);
    EXPECT_FLOAT_EQ(s.emissive.x, 3.0f);
}

TEST(VfxDDGIBuildSample, InfluenceRadius_FloorsAtOneMeter) {
    auto s = VfxDDGIBuildSampleForEmitter(
        kAtOrigin, kWhite, /*emissive=*/1.0f,
        /*alive=*/10u, /*max=*/10u, /*spawnRadius=*/0.0f);
    EXPECT_FLOAT_EQ(s.influenceRadius, kVfxDDGIMinInfluenceRadiusCm);
    EXPECT_FLOAT_EQ(s.influenceRadius, 100.0f);  // 1 metre in cm
}

TEST(VfxDDGIBuildSample, InfluenceRadius_FourTimesSpawnRadius) {
    auto s = VfxDDGIBuildSampleForEmitter(
        kAtOrigin, kWhite, /*emissive=*/1.0f,
        /*alive=*/10u, /*max=*/10u, /*spawnRadius=*/200.0f);
    // 200 * 4 = 800 cm, well above the 100 cm floor.
    EXPECT_FLOAT_EQ(s.influenceRadius, 800.0f);
}

TEST(VfxDDGIBuildSample, PositionRoundtrips) {
    Vec3 p{120.0f, -50.0f, 7.5f};
    auto s = VfxDDGIBuildSampleForEmitter(
        p, kWhite, /*emissive=*/1.0f,
        /*alive=*/1u, /*max=*/1u, /*spawnRadius=*/50.0f);
    EXPECT_FLOAT_EQ(s.position.x, p.x);
    EXPECT_FLOAT_EQ(s.position.y, p.y);
    EXPECT_FLOAT_EQ(s.position.z, p.z);
}

// =============================================================================
// CVar registration via VFXSystem::Initialize
// =============================================================================

TEST(VfxDDGIInjectionCVars, Initialize_RegistersBothCVars) {
    // Reset before init so a previous test's Set doesn't leak.
    auto& cvars = CVarRegistry::Get();
    cvars.Reset("r.VFX.DDGIInjection");
    cvars.Reset("r.VFX.DDGIInjection.MaxEmitters");

    test::MockRHIDevice device;
    VFXSystem system;
    system.Initialize(&device, /*budget=*/100'000);

    EXPECT_NE(cvars.Find("r.VFX.DDGIInjection"),             nullptr);
    EXPECT_NE(cvars.Find("r.VFX.DDGIInjection.MaxEmitters"), nullptr);
    EXPECT_TRUE(cvars.GetBool("r.VFX.DDGIInjection", false));

    // Default is 32 — matches VfxDDGIEmissiveFeed::kMaxSamples.
    EXPECT_EQ(cvars.GetInt("r.VFX.DDGIInjection.MaxEmitters", 0), 32);

    system.Shutdown(&device);
}

TEST(VfxDDGIInjectionCVars, MasterSwitch_TogglesAtRuntime) {
    auto& cvars = CVarRegistry::Get();
    cvars.Reset("r.VFX.DDGIInjection");

    test::MockRHIDevice device;
    VFXSystem system;
    system.Initialize(&device, /*budget=*/100'000);

    cvars.Set("r.VFX.DDGIInjection", 0.0f);
    EXPECT_FALSE(cvars.GetBool("r.VFX.DDGIInjection", true));

    cvars.Set("r.VFX.DDGIInjection", 1.0f);
    EXPECT_TRUE(cvars.GetBool("r.VFX.DDGIInjection", false));

    system.Shutdown(&device);
}

TEST(VfxDDGIInjectionCVars, MaxEmitters_AcceptsRuntimeValues) {
    auto& cvars = CVarRegistry::Get();
    cvars.Reset("r.VFX.DDGIInjection.MaxEmitters");

    test::MockRHIDevice device;
    VFXSystem system;
    system.Initialize(&device, /*budget=*/100'000);

    cvars.Set("r.VFX.DDGIInjection.MaxEmitters", 8.f);
    EXPECT_EQ(cvars.GetInt("r.VFX.DDGIInjection.MaxEmitters", 0), 8);

    cvars.Set("r.VFX.DDGIInjection.MaxEmitters", 64.f);
    EXPECT_EQ(cvars.GetInt("r.VFX.DDGIInjection.MaxEmitters", 0), 64);

    system.Shutdown(&device);
}
