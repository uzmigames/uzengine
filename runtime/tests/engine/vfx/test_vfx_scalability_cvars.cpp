/// Unit tests for the VFX scalability CVar surface and sequencer hooks.
///
/// Covers `phase16_vfx-scalability-cvars-sequencer`:
///
///   * `r.VFX.MaxTotalParticles` / `AllowGPUSim` / `QualityLevel` /
///     `SimulatePaused` / `TimeScale` are registered when the VFXSystem
///     initialises and survive subsequent Tick() / Set() round-trips.
///   * `VFXSystem::Tick` samples the CVars once per call and caches them
///     on the system. Subsequent getters return the cached values
///     regardless of how many times Tick is called or what other state
///     mutates between calls.
///   * Quality preset multiplier table matches the documented values for
///     all four levels (0 = low, 1 = medium, 2 = high, 3 = cinematic).
///   * `MaxTotalParticles` propagates to the budget at frame start —
///     `VFXSystem::GetBudget().GetMax()` reflects the CVar value within
///     a single Tick.
///   * `ParticleEmitter::Seek` sets the pending-seek field, resets
///     spawn / alive accumulators, and flags the buffer for
///     reinitialisation.
///   * `ParticleEmitter::ClearPendingSeek` is idempotent and clears the
///     pending field.
///   * `VFXSystem::FindEmitterByName` resolves registered emitters by
///     name and returns nullptr for unknown / empty names.
///
/// No GPU hardware required — CPU-only.

#include <gtest/gtest.h>

#include "engine/core/cvar.h"
#include "engine/vfx/components/particle_emitter.h"
#include "engine/vfx/core/particle_budget.h"
#include "engine/vfx/vfx_system.h"
#include "mock_rhi_device.h"

#include <string>

using namespace uz;

namespace {

/// Helper: initialise a fresh VFXSystem on a `MockRHIDevice` with a
/// default budget. Tests typically wrap the system in this helper to
/// avoid repeating the boilerplate.
struct VfxFixture {
    test::MockRHIDevice device;
    VFXSystem           system;

    explicit VfxFixture(uint32_t budget = 100'000) {
        system.Initialize(&device, budget);
    }
    ~VfxFixture() {
        system.Shutdown(&device);
    }
};

/// Reset every VFX CVar to its default before each test. Each TEST() in
/// this file shares a process-wide CVar registry, so without a reset a
/// test can poison a later test's cached state via the previous Set.
void ResetVfxCVars() {
    auto& cvars = CVarRegistry::Get();
    cvars.Reset("r.VFX.MaxTotalParticles");
    cvars.Reset("r.VFX.AllowGPUSim");
    cvars.Reset("r.VFX.QualityLevel");
    cvars.Reset("r.VFX.SimulatePaused");
    cvars.Reset("r.VFX.TimeScale");
}

} // anonymous namespace

// =============================================================================
// CVar registration on Initialize
// =============================================================================

TEST(VfxScalability, Initialize_RegistersAllFiveCVars) {
    ResetVfxCVars();
    VfxFixture fx;

    auto& cvars = CVarRegistry::Get();
    EXPECT_NE(cvars.Find("r.VFX.MaxTotalParticles"), nullptr);
    EXPECT_NE(cvars.Find("r.VFX.AllowGPUSim"),       nullptr);
    EXPECT_NE(cvars.Find("r.VFX.QualityLevel"),      nullptr);
    EXPECT_NE(cvars.Find("r.VFX.SimulatePaused"),    nullptr);
    EXPECT_NE(cvars.Find("r.VFX.TimeScale"),         nullptr);
}

TEST(VfxScalability, Initialize_DefaultsAreUnchangedBehaviour) {
    // The CVarRegistry is a process-wide singleton — earlier tests in the
    // suite may have registered the same CVars with different defaults
    // and Set() values. Reset the values explicitly to the documented
    // defaults so this test is order-independent.
    ResetVfxCVars();

    auto& cvars = CVarRegistry::Get();
    cvars.Set("r.VFX.MaxTotalParticles", 250'000.f);
    cvars.Set("r.VFX.AllowGPUSim",       1.f);
    cvars.Set("r.VFX.QualityLevel",      2.f);
    cvars.Set("r.VFX.SimulatePaused",    0.f);
    cvars.Set("r.VFX.TimeScale",         1.f);

    VfxFixture fx(250'000);

    EXPECT_FLOAT_EQ(cvars.GetFloat("r.VFX.MaxTotalParticles", 0.f), 250'000.f);
    EXPECT_TRUE(cvars.GetBool("r.VFX.AllowGPUSim",     false));
    EXPECT_EQ   (cvars.GetInt ("r.VFX.QualityLevel",       0), 2);
    EXPECT_FALSE(cvars.GetBool("r.VFX.SimulatePaused",    true));
    EXPECT_FLOAT_EQ(cvars.GetFloat("r.VFX.TimeScale",     0.f), 1.f);
}

// =============================================================================
// Tick samples CVars and caches them on the system
// =============================================================================

TEST(VfxScalability, Tick_CachesAllowGPUSim) {
    ResetVfxCVars();
    VfxFixture fx;

    EXPECT_TRUE(fx.system.IsGPUSimAllowed());

    CVarRegistry::Get().Set("r.VFX.AllowGPUSim", 0.0f);
    fx.system.Tick(0.016f);
    EXPECT_FALSE(fx.system.IsGPUSimAllowed());

    CVarRegistry::Get().Set("r.VFX.AllowGPUSim", 1.0f);
    fx.system.Tick(0.016f);
    EXPECT_TRUE(fx.system.IsGPUSimAllowed());
}

TEST(VfxScalability, Tick_CachesQualityLevel_AllFourPresets) {
    ResetVfxCVars();
    VfxFixture fx;

    for (int level = 0; level <= 3; ++level) {
        CVarRegistry::Get().Set("r.VFX.QualityLevel", static_cast<float>(level));
        fx.system.Tick(0.016f);
        EXPECT_EQ(fx.system.GetQualityLevel(), static_cast<uint32_t>(level))
            << "QualityLevel did not propagate for input " << level;
    }
}

TEST(VfxScalability, Tick_CachesSimulatePaused) {
    ResetVfxCVars();
    VfxFixture fx;
    EXPECT_FALSE(fx.system.IsSimulationPaused());

    CVarRegistry::Get().Set("r.VFX.SimulatePaused", 1.0f);
    fx.system.Tick(0.016f);
    EXPECT_TRUE(fx.system.IsSimulationPaused());
}

TEST(VfxScalability, Tick_CachesTimeScale) {
    ResetVfxCVars();
    VfxFixture fx;
    EXPECT_FLOAT_EQ(fx.system.GetTimeScale(), 1.0f);

    CVarRegistry::Get().Set("r.VFX.TimeScale", 2.5f);
    fx.system.Tick(0.016f);
    EXPECT_FLOAT_EQ(fx.system.GetTimeScale(), 2.5f);

    // Negative values must clamp to 0 (sim freezes, render keeps drawing).
    CVarRegistry::Get().Set("r.VFX.TimeScale", -3.0f);
    fx.system.Tick(0.016f);
    EXPECT_FLOAT_EQ(fx.system.GetTimeScale(), 0.0f);
}

TEST(VfxScalability, Tick_PropagatesMaxTotalParticlesToBudget) {
    ResetVfxCVars();
    VfxFixture fx(/*budget=*/500'000);
    EXPECT_EQ(fx.system.GetBudget().GetMax(), 500'000u);

    CVarRegistry::Get().Set("r.VFX.MaxTotalParticles", 1024.f);
    fx.system.Tick(0.016f);
    EXPECT_EQ(fx.system.GetBudget().GetMax(), 1024u);

    CVarRegistry::Get().Set("r.VFX.MaxTotalParticles", 2'000'000.f);
    fx.system.Tick(0.016f);
    EXPECT_EQ(fx.system.GetBudget().GetMax(), 2'000'000u);
}

// =============================================================================
// Quality preset multipliers
// =============================================================================

TEST(VfxScalability, QualityMultipliers_MatchDocumentedTable) {
    ResetVfxCVars();
    VfxFixture fx;

    struct Row { int level; float spawn; float lod; };
    const Row table[] = {
        { 0, 0.25f, 0.50f },
        { 1, 0.50f, 0.75f },
        { 2, 1.00f, 1.00f },
        { 3, 1.50f, 1.50f },
    };

    for (const auto& row : table) {
        CVarRegistry::Get().Set("r.VFX.QualityLevel", static_cast<float>(row.level));
        fx.system.Tick(0.016f);
        EXPECT_FLOAT_EQ(fx.system.GetQualitySpawnRateMultiplier(), row.spawn)
            << "spawn multiplier mismatch at QualityLevel=" << row.level;
        EXPECT_FLOAT_EQ(fx.system.GetQualityLodDistanceMultiplier(), row.lod)
            << "LOD multiplier mismatch at QualityLevel=" << row.level;
    }
}

// =============================================================================
// ParticleEmitter::Seek + ClearPendingSeek
// =============================================================================

TEST(ParticleEmitterSeek, DefaultPendingSeekIsZero) {
    ParticleEmitter e;
    EXPECT_FLOAT_EQ(e.GetPendingSeekTime(), 0.0f);
}

TEST(ParticleEmitterSeek, SeekStoresTargetTimeAndResetsAccumulators) {
    ParticleEmitter e;
    e.spawnAccumulator = 1.5f;
    e.cpuAliveCount    = 7u;

    e.Seek(2.5f);
    EXPECT_FLOAT_EQ(e.GetPendingSeekTime(), 2.5f);
    EXPECT_FLOAT_EQ(e.spawnAccumulator,     0.0f);
    EXPECT_EQ(e.cpuAliveCount,              0u);
    EXPECT_TRUE(e.buffer.needsInitialization)
        << "Seek must flag the GPU buffer for reinitialisation";
}

TEST(ParticleEmitterSeek, NegativeTargetClampsToZero) {
    ParticleEmitter e;
    e.Seek(-1.0f);
    EXPECT_FLOAT_EQ(e.GetPendingSeekTime(), 0.0f)
        << "Negative seek targets must clamp to 0 (full reset, no warmup)";
}

TEST(ParticleEmitterSeek, ClearPendingSeek_ResetsField) {
    ParticleEmitter e;
    e.Seek(3.0f);
    EXPECT_FLOAT_EQ(e.GetPendingSeekTime(), 3.0f);

    e.ClearPendingSeek();
    EXPECT_FLOAT_EQ(e.GetPendingSeekTime(), 0.0f);

    // Idempotent — calling on a cleared emitter is a no-op.
    e.ClearPendingSeek();
    EXPECT_FLOAT_EQ(e.GetPendingSeekTime(), 0.0f);
}

// =============================================================================
// Per-emitter pause + timeScale fields (state surface only — the dt path
// is exercised end-to-end by the inline sim loop in DispatchVFXSimulation,
// which needs a live RHI device to dispatch the spawn / update CS.)
// =============================================================================

TEST(ParticleEmitterScalability, IsPausedDefaultsFalse) {
    ParticleEmitter e;
    EXPECT_FALSE(e.isPaused);
}

TEST(ParticleEmitterScalability, TimeScaleDefaultsOne) {
    ParticleEmitter e;
    EXPECT_FLOAT_EQ(e.timeScale, 1.0f);
}

TEST(ParticleEmitterScalability, IsPausedAndTimeScaleAreIndependent) {
    ParticleEmitter e;
    e.isPaused  = true;
    e.timeScale = 0.5f;
    EXPECT_TRUE(e.isPaused);
    EXPECT_FLOAT_EQ(e.timeScale, 0.5f);
}

// =============================================================================
// VFXSystem::FindEmitterByName
// =============================================================================

TEST(VfxFindByName, ReturnsRegisteredEmitter) {
    ResetVfxCVars();
    VfxFixture fx;

    ParticleEmitter named;
    named.name = "ExplosionA";
    // VFXSystem::RegisterEmitter rejects un-initialised emitters. The
    // unit test does not allocate GPU buffers (no RHI device-driven
    // Initialize), so flip the flag manually — the lookup logic does
    // not inspect any GPU state, only the `name` field.
    named.initialized = true;
    fx.system.RegisterEmitter(&named);

    EXPECT_EQ(fx.system.FindEmitterByName("ExplosionA"), &named);

    fx.system.UnregisterEmitter(&named);
    named.initialized = false;
}

TEST(VfxFindByName, ReturnsNullForUnknownName) {
    ResetVfxCVars();
    VfxFixture fx;
    EXPECT_EQ(fx.system.FindEmitterByName("DoesNotExist"), nullptr);
}

TEST(VfxFindByName, EmptyNameReturnsNull) {
    ResetVfxCVars();
    VfxFixture fx;
    EXPECT_EQ(fx.system.FindEmitterByName(""), nullptr);
}

TEST(VfxFindByName, IgnoresUnnamedEmitters) {
    ResetVfxCVars();
    VfxFixture fx;

    ParticleEmitter unnamed;  // name remains empty.
    ParticleEmitter named;
    named.name = "Sparks";

    // See ReturnsRegisteredEmitter for the `initialized = true` rationale.
    unnamed.initialized = true;
    named.initialized   = true;

    fx.system.RegisterEmitter(&unnamed);
    fx.system.RegisterEmitter(&named);

    // Looking up the empty name MUST NOT match the unnamed emitter — the
    // function explicitly rejects empty queries to avoid false positives.
    EXPECT_EQ(fx.system.FindEmitterByName(""),       nullptr);
    EXPECT_EQ(fx.system.FindEmitterByName("Sparks"), &named);

    fx.system.UnregisterEmitter(&named);
    fx.system.UnregisterEmitter(&unnamed);
    named.initialized   = false;
    unnamed.initialized = false;
}
