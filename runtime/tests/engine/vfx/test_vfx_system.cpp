/// Unit tests for VFXSystem.
///
/// Covers:
///   - Initialize/Shutdown lifecycle
///   - RegisterEmitter/UnregisterEmitter — add, duplicate check, remove,
///     remove non-existent, uninitialized emitter
///   - Tick() — calls CalculateSpawnCount, accumulates alive counts
///   - GetActiveEmitters() returns correct list
///   - Shutdown() with emitters still registered (warning path)
///   - GetTotalAliveParticles / GetActiveEmitterCount / GetBudget
///
/// All tests are CPU-only — no GPU hardware required.

#include <gtest/gtest.h>

#include "vfx_system.h"
#include "particle_emitter.h"
#include "mock_rhi_device.h"

using namespace uz;
using uz::test::MockRHIDevice;

// ─────────────────────────────────────────────────────────────────────────────
// Helpers
// ─────────────────────────────────────────────────────────────────────────────

/// Fully initializes a ParticleEmitter using the provided VFXSystem's budget.
static bool InitEmitterViaSystem(VFXSystem& sys, MockRHIDevice& device,
                                  ParticleEmitter& e, uint32_t cap = 500u)
{
    e.maxParticles = cap;
    e.emissionRate = 100.0f;
    e.isEmitting   = true;
    return e.Initialize(&device, sys.GetBudget());
}

// ─────────────────────────────────────────────────────────────────────────────
// Initialize / Shutdown lifecycle
// ─────────────────────────────────────────────────────────────────────────────

/// VFXSystem must not be initialized before Initialize() is called.
TEST(VFXSystem, DefaultConstruct_NotInitialized) {
    VFXSystem sys;
    EXPECT_FALSE(sys.IsInitialized());
}

/// After Initialize(), IsInitialized() returns true.
TEST(VFXSystem, Initialize_SetsIsInitializedTrue) {
    MockRHIDevice device;
    VFXSystem sys;

    sys.Initialize(&device, 100000u);

    EXPECT_TRUE(sys.IsInitialized());

    sys.Shutdown(&device);
}

/// Initialize() sets the budget max to the provided value.
TEST(VFXSystem, Initialize_SetsBudgetMax) {
    MockRHIDevice device;
    VFXSystem sys;

    sys.Initialize(&device, 250000u);

    EXPECT_EQ(sys.GetBudget().GetMax(), 250000u);

    sys.Shutdown(&device);
}

/// After Shutdown(), IsInitialized() returns false.
TEST(VFXSystem, Shutdown_SetsIsInitializedFalse) {
    MockRHIDevice device;
    VFXSystem sys;
    sys.Initialize(&device, 100000u);

    sys.Shutdown(&device);

    EXPECT_FALSE(sys.IsInitialized());
}

/// Shutdown() on a never-initialized system is a no-op (does not crash).
TEST(VFXSystem, Shutdown_NeverInitialized_NoOp) {
    MockRHIDevice device;
    VFXSystem sys;
    sys.Shutdown(&device);  // must not crash
    EXPECT_FALSE(sys.IsInitialized());
}

/// After Shutdown(), active emitter count is zero.
TEST(VFXSystem, Shutdown_ClearsEmitterList) {
    MockRHIDevice device;
    VFXSystem sys;
    sys.Initialize(&device, 100000u);

    ParticleEmitter e;
    InitEmitterViaSystem(sys, device, e);
    sys.RegisterEmitter(&e);
    sys.Shutdown(&device);  // emitter still registered — warning path

    EXPECT_EQ(sys.GetActiveEmitterCount(), 0u);

    // Clean up emitter budget (Shutdown cleared it from system but emitter holds budget).
    // Budget is reset to zero-usage when system shuts down its internal budget instance.
    e.Shutdown(&device, sys.GetBudget());
}

// ─────────────────────────────────────────────────────────────────────────────
// RegisterEmitter
// ─────────────────────────────────────────────────────────────────────────────

/// RegisterEmitter() adds the emitter; GetActiveEmitterCount reflects this.
TEST(VFXSystem, RegisterEmitter_Adds_IncreasesCount) {
    MockRHIDevice device;
    VFXSystem sys;
    sys.Initialize(&device, 100000u);

    ParticleEmitter e;
    InitEmitterViaSystem(sys, device, e);

    sys.RegisterEmitter(&e);

    EXPECT_EQ(sys.GetActiveEmitterCount(), 1u);

    e.Shutdown(&device, sys.GetBudget());
    sys.Shutdown(&device);
}

/// Registering the same emitter twice does not add a duplicate.
TEST(VFXSystem, RegisterEmitter_Duplicate_CountStaysOne) {
    MockRHIDevice device;
    VFXSystem sys;
    sys.Initialize(&device, 100000u);

    ParticleEmitter e;
    InitEmitterViaSystem(sys, device, e);

    sys.RegisterEmitter(&e);
    sys.RegisterEmitter(&e);  // duplicate — must be ignored

    EXPECT_EQ(sys.GetActiveEmitterCount(), 1u)
        << "Duplicate RegisterEmitter must not add a second entry";

    e.Shutdown(&device, sys.GetBudget());
    sys.Shutdown(&device);
}

/// An uninitialized emitter is silently skipped by RegisterEmitter().
TEST(VFXSystem, RegisterEmitter_UninitializedEmitter_Skipped) {
    MockRHIDevice device;
    VFXSystem sys;
    sys.Initialize(&device, 100000u);

    ParticleEmitter e;  // not initialized

    sys.RegisterEmitter(&e);

    EXPECT_EQ(sys.GetActiveEmitterCount(), 0u)
        << "Uninitialized emitter must not be registered";

    sys.Shutdown(&device);
}

/// Multiple distinct emitters can be registered.
TEST(VFXSystem, RegisterEmitter_MultipleDistinct_AllPresent) {
    MockRHIDevice device;
    VFXSystem sys;
    sys.Initialize(&device, 100000u);

    ParticleEmitter e1, e2, e3;
    InitEmitterViaSystem(sys, device, e1, 100u);
    InitEmitterViaSystem(sys, device, e2, 100u);
    InitEmitterViaSystem(sys, device, e3, 100u);

    sys.RegisterEmitter(&e1);
    sys.RegisterEmitter(&e2);
    sys.RegisterEmitter(&e3);

    EXPECT_EQ(sys.GetActiveEmitterCount(), 3u);

    const auto& list = sys.GetActiveEmitters();
    EXPECT_NE(std::find(list.begin(), list.end(), &e1), list.end());
    EXPECT_NE(std::find(list.begin(), list.end(), &e2), list.end());
    EXPECT_NE(std::find(list.begin(), list.end(), &e3), list.end());

    e1.Shutdown(&device, sys.GetBudget());
    e2.Shutdown(&device, sys.GetBudget());
    e3.Shutdown(&device, sys.GetBudget());
    sys.Shutdown(&device);
}

// ─────────────────────────────────────────────────────────────────────────────
// UnregisterEmitter
// ─────────────────────────────────────────────────────────────────────────────

/// UnregisterEmitter() removes the emitter; count drops to zero.
TEST(VFXSystem, UnregisterEmitter_RemovesEmitter) {
    MockRHIDevice device;
    VFXSystem sys;
    sys.Initialize(&device, 100000u);

    ParticleEmitter e;
    InitEmitterViaSystem(sys, device, e);
    sys.RegisterEmitter(&e);

    sys.UnregisterEmitter(&e);

    EXPECT_EQ(sys.GetActiveEmitterCount(), 0u);

    e.Shutdown(&device, sys.GetBudget());
    sys.Shutdown(&device);
}

/// Unregistering a non-registered emitter is a no-op (warning, no crash, count unchanged).
TEST(VFXSystem, UnregisterEmitter_NonRegistered_IsNoOp) {
    MockRHIDevice device;
    VFXSystem sys;
    sys.Initialize(&device, 100000u);

    ParticleEmitter e1, e2;
    InitEmitterViaSystem(sys, device, e1, 100u);
    InitEmitterViaSystem(sys, device, e2, 100u);
    sys.RegisterEmitter(&e1);

    sys.UnregisterEmitter(&e2);  // e2 was never registered

    EXPECT_EQ(sys.GetActiveEmitterCount(), 1u)
        << "Unregistering a non-registered emitter must leave count unchanged";

    e1.Shutdown(&device, sys.GetBudget());
    e2.Shutdown(&device, sys.GetBudget());
    sys.Shutdown(&device);
}

/// After unregistering one emitter from three, only that emitter is removed.
TEST(VFXSystem, UnregisterEmitter_OneOfThree_OthersTwoRemain) {
    MockRHIDevice device;
    VFXSystem sys;
    sys.Initialize(&device, 100000u);

    ParticleEmitter e1, e2, e3;
    InitEmitterViaSystem(sys, device, e1, 100u);
    InitEmitterViaSystem(sys, device, e2, 100u);
    InitEmitterViaSystem(sys, device, e3, 100u);

    sys.RegisterEmitter(&e1);
    sys.RegisterEmitter(&e2);
    sys.RegisterEmitter(&e3);

    sys.UnregisterEmitter(&e2);

    EXPECT_EQ(sys.GetActiveEmitterCount(), 2u);
    const auto& list = sys.GetActiveEmitters();
    EXPECT_NE(std::find(list.begin(), list.end(), &e1), list.end());
    EXPECT_EQ(std::find(list.begin(), list.end(), &e2), list.end())
        << "e2 must no longer be in the active list after unregister";
    EXPECT_NE(std::find(list.begin(), list.end(), &e3), list.end());

    e1.Shutdown(&device, sys.GetBudget());
    e2.Shutdown(&device, sys.GetBudget());
    e3.Shutdown(&device, sys.GetBudget());
    sys.Shutdown(&device);
}

// ─────────────────────────────────────────────────────────────────────────────
// GetActiveEmitters
// ─────────────────────────────────────────────────────────────────────────────

/// GetActiveEmitters() returns an empty vector on a freshly initialized system.
TEST(VFXSystem, GetActiveEmitters_Fresh_IsEmpty) {
    MockRHIDevice device;
    VFXSystem sys;
    sys.Initialize(&device, 100000u);

    EXPECT_TRUE(sys.GetActiveEmitters().empty());

    sys.Shutdown(&device);
}

// ─────────────────────────────────────────────────────────────────────────────
// Tick — spawn count dispatch and alive count accumulation
// ─────────────────────────────────────────────────────────────────────────────

/// Tick() on a system with no emitters is a safe no-op.
TEST(VFXSystem, Tick_NoEmitters_NoOp) {
    MockRHIDevice device;
    VFXSystem sys;
    sys.Initialize(&device, 100000u);

    sys.Tick(0.016f);  // must not crash

    EXPECT_EQ(sys.GetTotalAliveParticles(), 0u);

    sys.Shutdown(&device);
}

/// After Tick() on a non-initialized system (no Initialize() called), nothing crashes.
TEST(VFXSystem, Tick_NotInitialized_NoOp) {
    VFXSystem sys;
    sys.Tick(0.016f);  // must not crash — early return
    EXPECT_EQ(sys.GetTotalAliveParticles(), 0u);
}

/// Tick() must NOT call CalculateSpawnCount(): the frame's VFX simulation
/// dispatch (frame_vfx_inline.cpp) is the sole consumer of the spawn
/// accumulator. Commit 9fc06229 removed the Tick() call because consuming the
/// accumulator in both places double-counted spawns. Tick() only advances
/// the per-emitter registration clock (used for start delays / bursts).
TEST(VFXSystem, Tick_DoesNotConsumeSpawnAccumulator) {
    MockRHIDevice device;
    VFXSystem sys;
    sys.Initialize(&device, 100000u);

    ParticleEmitter e1, e2;
    InitEmitterViaSystem(sys, device, e1, 1000u);
    InitEmitterViaSystem(sys, device, e2, 1000u);

    // Assign a known emission rate and clear accumulators.
    e1.emissionRate = 100.0f; e1.spawnAccumulator = 0.0f;
    e2.emissionRate = 50.0f;  e2.spawnAccumulator = 0.0f;

    sys.RegisterEmitter(&e1);
    sys.RegisterEmitter(&e2);

    const float t1Before = e1.timeSinceRegistration;
    const float t2Before = e2.timeSinceRegistration;

    sys.Tick(0.016f);

    // Accumulators untouched: no spawn was consumed by Tick().
    EXPECT_FLOAT_EQ(e1.spawnAccumulator, 0.0f)
        << "Tick() must not accumulate spawns (double-counting regression)";
    EXPECT_FLOAT_EQ(e2.spawnAccumulator, 0.0f)
        << "Tick() must not accumulate spawns (double-counting regression)";

    // Registration clock advanced on every registered emitter.
    EXPECT_NEAR(e1.timeSinceRegistration, t1Before + 0.016f, 1e-6f);
    EXPECT_NEAR(e2.timeSinceRegistration, t2Before + 0.016f, 1e-6f);

    // The sole consumer still sees the full, un-consumed frame of spawns:
    // 100 * 0.016 = 1.6 → spawn 1, carry 0.6; 50 * 0.016 = 0.8 → spawn 0, carry 0.8.
    EXPECT_EQ(e1.CalculateSpawnCount(0.016f), 1u);
    EXPECT_EQ(e2.CalculateSpawnCount(0.016f), 0u);
    EXPECT_NEAR(e1.spawnAccumulator, 0.6f, 1e-5f);
    EXPECT_NEAR(e2.spawnAccumulator, 0.8f, 1e-5f);

    e1.Shutdown(&device, sys.GetBudget());
    e2.Shutdown(&device, sys.GetBudget());
    sys.Shutdown(&device);
}

/// Tick() accumulates cpuAliveCount from all emitters into GetTotalAliveParticles().
TEST(VFXSystem, Tick_AccumulatesTotalAliveParticles) {
    MockRHIDevice device;
    VFXSystem sys;
    sys.Initialize(&device, 100000u);

    ParticleEmitter e1, e2;
    InitEmitterViaSystem(sys, device, e1, 1000u);
    InitEmitterViaSystem(sys, device, e2, 1000u);

    e1.cpuAliveCount = 300u;
    e2.cpuAliveCount = 700u;

    sys.RegisterEmitter(&e1);
    sys.RegisterEmitter(&e2);

    sys.Tick(0.016f);

    EXPECT_EQ(sys.GetTotalAliveParticles(), 1000u)
        << "GetTotalAliveParticles must sum cpuAliveCount across all emitters";

    e1.Shutdown(&device, sys.GetBudget());
    e2.Shutdown(&device, sys.GetBudget());
    sys.Shutdown(&device);
}

/// After an emitter is unregistered, Tick() no longer counts its alive particles.
TEST(VFXSystem, Tick_AfterUnregister_DoesNotCountRemovedEmitter) {
    MockRHIDevice device;
    VFXSystem sys;
    sys.Initialize(&device, 100000u);

    ParticleEmitter e1, e2;
    InitEmitterViaSystem(sys, device, e1, 1000u);
    InitEmitterViaSystem(sys, device, e2, 1000u);

    e1.cpuAliveCount = 500u;
    e2.cpuAliveCount = 500u;

    sys.RegisterEmitter(&e1);
    sys.RegisterEmitter(&e2);
    sys.UnregisterEmitter(&e2);

    sys.Tick(0.016f);

    EXPECT_EQ(sys.GetTotalAliveParticles(), 500u)
        << "After unregistering e2, only e1's alive count should be summed";

    e1.Shutdown(&device, sys.GetBudget());
    e2.Shutdown(&device, sys.GetBudget());
    sys.Shutdown(&device);
}

/// Multiple Tick() calls update GetTotalAliveParticles() to the current
/// cpuAliveCount each frame (not accumulated across frames).
TEST(VFXSystem, Tick_MultipleFrames_TotalAliveReflectsCurrentCounts) {
    MockRHIDevice device;
    VFXSystem sys;
    sys.Initialize(&device, 100000u);

    ParticleEmitter e;
    InitEmitterViaSystem(sys, device, e, 1000u);
    sys.RegisterEmitter(&e);

    e.cpuAliveCount = 100u;
    sys.Tick(0.016f);
    EXPECT_EQ(sys.GetTotalAliveParticles(), 100u);

    e.cpuAliveCount = 250u;
    sys.Tick(0.016f);
    EXPECT_EQ(sys.GetTotalAliveParticles(), 250u);

    e.cpuAliveCount = 0u;
    sys.Tick(0.016f);
    EXPECT_EQ(sys.GetTotalAliveParticles(), 0u);

    e.Shutdown(&device, sys.GetBudget());
    sys.Shutdown(&device);
}

// ─────────────────────────────────────────────────────────────────────────────
// GetBudget
// ─────────────────────────────────────────────────────────────────────────────

/// Const GetBudget() returns the same budget as non-const.
TEST(VFXSystem, GetBudget_ConstAndNonConst_SameMax) {
    MockRHIDevice device;
    VFXSystem sys;
    sys.Initialize(&device, 77777u);

    const VFXSystem& constSys = sys;
    EXPECT_EQ(constSys.GetBudget().GetMax(), 77777u);
    EXPECT_EQ(sys.GetBudget().GetMax(), 77777u);

    sys.Shutdown(&device);
}
