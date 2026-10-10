/// Unit tests for ParticleEmitter.
///
/// Covers:
///   - Initialize(): success path, budget grant, partial grant
///   - Initialize(): failure: exhausted budget, zero maxParticles, null device
///   - Initialize(): already-initialized emitter (idempotent return true)
///   - Shutdown(): returns budget, resets state, safe double-shutdown
///   - CalculateSpawnCount(): basic dt, accumulator carry-over
///   - CalculateSpawnCount(): zero dt, not initialized, not emitting, zero rate
///   - CalculateSpawnCount(): capacity clamping
///
/// All tests are CPU-only — no GPU hardware required.

#include <gtest/gtest.h>

#include "particle_emitter.h"
#include "particle_budget.h"
#include "mock_rhi_device.h"

using namespace uz;
using uz::test::MockRHIDevice;
using uz::test::FailingMockRHIDevice;

// ─────────────────────────────────────────────────────────────────────────────
// Helpers
// ─────────────────────────────────────────────────────────────────────────────

/// Returns an emitter with small defaults suitable for unit tests.
static ParticleEmitter MakeEmitter(uint32_t maxParticles = 1000u,
                                   float emissionRate    = 100.0f)
{
    ParticleEmitter e;
    e.maxParticles  = maxParticles;
    e.emissionRate  = emissionRate;
    e.isEmitting    = true;
    return e;
}

// ─────────────────────────────────────────────────────────────────────────────
// Default state
// ─────────────────────────────────────────────────────────────────────────────

/// A default-constructed ParticleEmitter must not be initialized.
TEST(ParticleEmitter, DefaultConstruct_NotInitialized) {
    ParticleEmitter e;
    EXPECT_FALSE(e.initialized);
}

/// Default allocatedFromBudget must be zero.
TEST(ParticleEmitter, DefaultConstruct_AllocatedFromBudgetIsZero) {
    ParticleEmitter e;
    EXPECT_EQ(e.allocatedFromBudget, 0u);
}

/// Default spawnAccumulator must be zero.
TEST(ParticleEmitter, DefaultConstruct_SpawnAccumulatorIsZero) {
    ParticleEmitter e;
    EXPECT_FLOAT_EQ(e.spawnAccumulator, 0.0f);
}

// ─────────────────────────────────────────────────────────────────────────────
// Initialize — success path
// ─────────────────────────────────────────────────────────────────────────────

/// Initialize() with valid device and ample budget returns true.
TEST(ParticleEmitter, Initialize_ValidArgs_ReturnsTrue) {
    MockRHIDevice device;
    ParticleBudget budget(100000u);
    ParticleEmitter e = MakeEmitter(1000u);

    const bool ok = e.Initialize(&device, budget);

    EXPECT_TRUE(ok) << "Initialize() must return true with valid device and ample budget";

    e.Shutdown(&device, budget);
}

/// After Initialize(), the emitter is marked as initialized.
TEST(ParticleEmitter, Initialize_Success_IsInitializedTrue) {
    MockRHIDevice device;
    ParticleBudget budget(100000u);
    ParticleEmitter e = MakeEmitter(1000u);

    e.Initialize(&device, budget);

    EXPECT_TRUE(e.initialized);

    e.Shutdown(&device, budget);
}

/// After Initialize(), allocatedFromBudget matches the full request when budget
/// has room.
TEST(ParticleEmitter, Initialize_FullGrant_AllocatedFromBudgetEqualsMax) {
    MockRHIDevice device;
    ParticleBudget budget(100000u);
    ParticleEmitter e = MakeEmitter(500u);

    e.Initialize(&device, budget);

    EXPECT_EQ(e.allocatedFromBudget, 500u);

    e.Shutdown(&device, budget);
}

/// Initialize() allocates budget in full — GetUsed reflects the grant.
TEST(ParticleEmitter, Initialize_ConsumesBudget) {
    MockRHIDevice device;
    ParticleBudget budget(5000u);
    ParticleEmitter e = MakeEmitter(2000u);

    e.Initialize(&device, budget);

    EXPECT_EQ(budget.GetUsed(), 2000u);

    e.Shutdown(&device, budget);
}

/// When budget is constrained but not zero, Initialize() succeeds with a
/// partial grant and marks the emitter initialized.
TEST(ParticleEmitter, Initialize_PartialGrant_SucceedsWithFewer) {
    MockRHIDevice device;
    ParticleBudget budget(300u);
    // Pre-consume 200 so only 100 remain.
    uint32_t g = 0;
    budget.Allocate(200u, g);

    ParticleEmitter e = MakeEmitter(500u);  // wants 500, only 100 available

    const bool ok = e.Initialize(&device, budget);

    EXPECT_TRUE(ok) << "Partial budget grant must still succeed";
    EXPECT_EQ(e.allocatedFromBudget, 100u)
        << "allocatedFromBudget must equal the partial grant";
    EXPECT_TRUE(e.initialized);

    e.Shutdown(&device, budget);
    budget.Return(200u);
}

/// Initialize() resets spawnAccumulator and cpuAliveCount on success.
TEST(ParticleEmitter, Initialize_ResetsRuntimeCounters) {
    MockRHIDevice device;
    ParticleBudget budget(10000u);
    ParticleEmitter e = MakeEmitter(100u);
    // Dirty the fields to make the test meaningful.
    e.spawnAccumulator = 99.0f;
    e.cpuAliveCount    = 42u;

    e.Initialize(&device, budget);

    EXPECT_FLOAT_EQ(e.spawnAccumulator, 0.0f);
    EXPECT_EQ(e.cpuAliveCount, 0u);

    e.Shutdown(&device, budget);
}

// ─────────────────────────────────────────────────────────────────────────────
// Initialize — already initialized (idempotent)
// ─────────────────────────────────────────────────────────────────────────────

/// Calling Initialize() on an already-initialized emitter is a no-op that
/// returns true without consuming additional budget.
TEST(ParticleEmitter, Initialize_AlreadyInitialized_ReturnsTrueNoExtraBudget) {
    MockRHIDevice device;
    ParticleBudget budget(10000u);
    ParticleEmitter e = MakeEmitter(100u);

    e.Initialize(&device, budget);
    const uint32_t usedAfterFirst = budget.GetUsed();

    const bool ok = e.Initialize(&device, budget);

    EXPECT_TRUE(ok) << "Second Initialize() must return true";
    EXPECT_EQ(budget.GetUsed(), usedAfterFirst)
        << "Second Initialize() must not consume additional budget";

    e.Shutdown(&device, budget);
}

// ─────────────────────────────────────────────────────────────────────────────
// Initialize — failure paths
// ─────────────────────────────────────────────────────────────────────────────

/// Initialize() returns false when maxParticles is 0.
TEST(ParticleEmitter, Initialize_ZeroMaxParticles_ReturnsFalse) {
    MockRHIDevice device;
    ParticleBudget budget(10000u);
    ParticleEmitter e;
    e.maxParticles = 0u;

    const bool ok = e.Initialize(&device, budget);

    EXPECT_FALSE(ok) << "Initialize() must return false when maxParticles == 0";
    EXPECT_FALSE(e.initialized);
    EXPECT_EQ(budget.GetUsed(), 0u) << "Budget must not be consumed on failure";
}

/// Initialize() returns false when the budget is fully exhausted.
TEST(ParticleEmitter, Initialize_ExhaustedBudget_ReturnsFalse) {
    MockRHIDevice device;
    ParticleBudget budget(100u);
    uint32_t g = 0;
    budget.Allocate(100u, g);  // fully exhausted

    ParticleEmitter e = MakeEmitter(1u);
    const bool ok = e.Initialize(&device, budget);

    EXPECT_FALSE(ok) << "Initialize() must return false when budget is exhausted";
    EXPECT_FALSE(e.initialized);

    budget.Return(100u);
}

/// When GPU buffer creation fails, Initialize() returns false and releases
/// the budget it had just acquired.
TEST(ParticleEmitter, Initialize_GPUBufferFails_ReturnsFalseAndReleasedBudget) {
    FailingMockRHIDevice failDev;
    ParticleBudget budget(10000u);
    ParticleEmitter e = MakeEmitter(500u);

    const bool ok = e.Initialize(&failDev, budget);

    EXPECT_FALSE(ok) << "Initialize() must return false when GPU buffer allocation fails";
    EXPECT_FALSE(e.initialized);
    EXPECT_EQ(budget.GetUsed(), 0u)
        << "Budget must be fully returned after GPU buffer failure";
}

// ─────────────────────────────────────────────────────────────────────────────
// Shutdown
// ─────────────────────────────────────────────────────────────────────────────

/// Shutdown() returns the allocated budget back.
TEST(ParticleEmitter, Shutdown_ReturnsBudget) {
    MockRHIDevice device;
    ParticleBudget budget(10000u);
    ParticleEmitter e = MakeEmitter(800u);
    e.Initialize(&device, budget);

    e.Shutdown(&device, budget);

    EXPECT_EQ(budget.GetUsed(), 0u)
        << "Shutdown() must return all particles to the budget";
}

/// After Shutdown(), initialized is false.
TEST(ParticleEmitter, Shutdown_SetsInitializedFalse) {
    MockRHIDevice device;
    ParticleBudget budget(10000u);
    ParticleEmitter e = MakeEmitter(200u);
    e.Initialize(&device, budget);

    e.Shutdown(&device, budget);

    EXPECT_FALSE(e.initialized);
}

/// After Shutdown(), allocatedFromBudget is reset to 0.
TEST(ParticleEmitter, Shutdown_ResetsAllocatedFromBudget) {
    MockRHIDevice device;
    ParticleBudget budget(10000u);
    ParticleEmitter e = MakeEmitter(300u);
    e.Initialize(&device, budget);

    e.Shutdown(&device, budget);

    EXPECT_EQ(e.allocatedFromBudget, 0u);
}

/// After Shutdown(), runtime counters are reset.
TEST(ParticleEmitter, Shutdown_ResetsRuntimeCounters) {
    MockRHIDevice device;
    ParticleBudget budget(10000u);
    ParticleEmitter e = MakeEmitter(300u);
    e.Initialize(&device, budget);
    e.cpuAliveCount = 55u;
    e.spawnAccumulator = 3.5f;

    e.Shutdown(&device, budget);

    EXPECT_EQ(e.cpuAliveCount, 0u);
    EXPECT_FLOAT_EQ(e.spawnAccumulator, 0.0f);
}

/// Calling Shutdown() on a never-initialized emitter is a safe no-op (no crash,
/// no budget change).
TEST(ParticleEmitter, Shutdown_NeverInitialized_IsNoOp) {
    MockRHIDevice device;
    ParticleBudget budget(10000u);
    ParticleEmitter e;

    e.Shutdown(&device, budget);

    EXPECT_EQ(budget.GetUsed(), 0u);
    EXPECT_FALSE(e.initialized);
}

/// Calling Shutdown() twice after initialization is safe — the second call is
/// a no-op that does not change budget or state.
TEST(ParticleEmitter, Shutdown_CalledTwice_SecondIsNoOp) {
    MockRHIDevice device;
    ParticleBudget budget(10000u);
    ParticleEmitter e = MakeEmitter(400u);
    e.Initialize(&device, budget);

    e.Shutdown(&device, budget);
    e.Shutdown(&device, budget);  // must not crash or corrupt budget

    EXPECT_EQ(budget.GetUsed(), 0u);
    EXPECT_FALSE(e.initialized);
}

// ─────────────────────────────────────────────────────────────────────────────
// CalculateSpawnCount — preconditions that return 0
// ─────────────────────────────────────────────────────────────────────────────

/// CalculateSpawnCount() returns 0 on a never-initialized emitter.
TEST(ParticleEmitter, CalculateSpawnCount_NotInitialized_ReturnsZero) {
    ParticleEmitter e = MakeEmitter(1000u);
    // Do NOT call Initialize() — emitter is not initialized.

    const uint32_t count = e.CalculateSpawnCount(0.016f);

    EXPECT_EQ(count, 0u) << "Uninitialized emitter must return 0 spawn count";
}

/// CalculateSpawnCount() returns 0 when isEmitting is false.
TEST(ParticleEmitter, CalculateSpawnCount_NotEmitting_ReturnsZero) {
    MockRHIDevice device;
    ParticleBudget budget(10000u);
    ParticleEmitter e = MakeEmitter(1000u, 100.0f);
    e.Initialize(&device, budget);
    e.isEmitting = false;

    const uint32_t count = e.CalculateSpawnCount(0.016f);

    EXPECT_EQ(count, 0u) << "Emitter with isEmitting=false must return 0";

    e.Shutdown(&device, budget);
}

/// CalculateSpawnCount() returns 0 when emissionRate is 0.
TEST(ParticleEmitter, CalculateSpawnCount_ZeroRate_ReturnsZero) {
    MockRHIDevice device;
    ParticleBudget budget(10000u);
    ParticleEmitter e = MakeEmitter(1000u, 0.0f);
    e.Initialize(&device, budget);

    const uint32_t count = e.CalculateSpawnCount(0.016f);

    EXPECT_EQ(count, 0u);

    e.Shutdown(&device, budget);
}

/// CalculateSpawnCount() returns 0 when dt <= 0.
TEST(ParticleEmitter, CalculateSpawnCount_ZeroDt_ReturnsZero) {
    MockRHIDevice device;
    ParticleBudget budget(10000u);
    ParticleEmitter e = MakeEmitter(1000u, 100.0f);
    e.Initialize(&device, budget);

    EXPECT_EQ(e.CalculateSpawnCount(0.0f), 0u)
        << "dt=0.0 must return 0";
    EXPECT_EQ(e.CalculateSpawnCount(-0.001f), 0u)
        << "Negative dt must return 0";

    e.Shutdown(&device, budget);
}

// ─────────────────────────────────────────────────────────────────────────────
// CalculateSpawnCount — basic accumulator arithmetic
// ─────────────────────────────────────────────────────────────────────────────

/// At 100 particles/sec with dt=0.016, rate*dt = 1.6, so 1 particle spawns.
TEST(ParticleEmitter, CalculateSpawnCount_100RateAt16ms_SpawnsOne) {
    MockRHIDevice device;
    ParticleBudget budget(10000u);
    ParticleEmitter e = MakeEmitter(1000u, 100.0f);
    e.Initialize(&device, budget);

    const uint32_t count = e.CalculateSpawnCount(0.016f);

    EXPECT_EQ(count, 1u)
        << "100 particles/sec * 0.016s = 1.6 → floor = 1 particle";

    e.Shutdown(&device, budget);
}

/// The fractional part carries over: two frames at 100 particles/s 0.016s
/// should produce 1 + 2 = 3 total (1.6 + 1.6 = 3.2 → 3 particles).
TEST(ParticleEmitter, CalculateSpawnCount_AccumulatorCarryOver) {
    MockRHIDevice device;
    ParticleBudget budget(10000u);
    ParticleEmitter e = MakeEmitter(1000u, 100.0f);
    e.Initialize(&device, budget);

    const uint32_t frame1 = e.CalculateSpawnCount(0.016f);  // 1.6 → 1, carry 0.6
    const uint32_t frame2 = e.CalculateSpawnCount(0.016f);  // 0.6+1.6=2.2 → 2, carry 0.2
    const uint32_t frame3 = e.CalculateSpawnCount(0.016f);  // 0.2+1.6=1.8 → 1, carry 0.8

    EXPECT_EQ(frame1, 1u) << "Frame 1 must spawn 1 particle (floor of 1.6)";
    EXPECT_EQ(frame2, 2u) << "Frame 2 must spawn 2 particles (floor of 2.2, with carry)";
    EXPECT_EQ(frame3, 1u) << "Frame 3 must spawn 1 particle (floor of 1.8, with carry)";

    e.Shutdown(&device, budget);
}

/// At exactly 1000 particles/sec with dt=1.0, exactly 1000 particles spawn.
TEST(ParticleEmitter, CalculateSpawnCount_1000RateAt1Sec_Spawns1000) {
    MockRHIDevice device;
    ParticleBudget budget(100000u);
    ParticleEmitter e = MakeEmitter(50000u, 1000.0f);
    e.Initialize(&device, budget);

    const uint32_t count = e.CalculateSpawnCount(1.0f);

    EXPECT_EQ(count, 1000u);

    e.Shutdown(&device, budget);
}

/// After calling CalculateSpawnCount(), the fractional remainder must remain
/// in spawnAccumulator (between 0 and 1).
TEST(ParticleEmitter, CalculateSpawnCount_AccumulatorRemainder_IsSubOne) {
    MockRHIDevice device;
    ParticleBudget budget(10000u);
    ParticleEmitter e = MakeEmitter(1000u, 100.0f);
    e.Initialize(&device, budget);

    e.CalculateSpawnCount(0.016f);

    EXPECT_GE(e.spawnAccumulator, 0.0f);
    EXPECT_LT(e.spawnAccumulator, 1.0f)
        << "Accumulator must hold only the sub-integer fraction after spawn";

    e.Shutdown(&device, budget);
}

// ─────────────────────────────────────────────────────────────────────────────
// CalculateSpawnCount — capacity clamping
// ─────────────────────────────────────────────────────────────────────────────

// The CPU does not track live particles (GPU free-list design, commit
// 0b4e3f5f "VFX alive count — Godot pattern"): free-slot availability is
// resolved by the spawn CS, which discards spawns once the free list is
// exhausted. A CPU-side (capacity - cpuAliveCount) clamp starved spawning
// permanently and was removed. What the CPU does guarantee is that a single
// frame never requests more spawns than the pool can ever hold.

/// A rate that wants more particles than the pool holds is clamped to the
/// pool bound (maxParticles == granted capacity here), and the fractional
/// remainder stays in the accumulator.
TEST(ParticleEmitter, CalculateSpawnCount_ExceedsCapacity_ClampedToCapacity) {
    MockRHIDevice device;
    ParticleBudget budget(10000u);
    // capacity = 100, rate = 10000, dt = 1.0 → wants 10000, pool holds 100
    ParticleEmitter e = MakeEmitter(100u, 10000.0f);
    ASSERT_TRUE(e.Initialize(&device, budget));
    ASSERT_EQ(e.buffer.capacity, e.maxParticles) << "Budget must grant in full";

    const uint32_t count = e.CalculateSpawnCount(1.0f);

    EXPECT_EQ(count, 100u)
        << "Spawn count must be clamped to the 100-particle pool";
    EXPECT_GE(e.spawnAccumulator, 0.0f);
    EXPECT_LT(e.spawnAccumulator, 1.0f)
        << "Clamping must not leave a backlog in the accumulator";

    e.Shutdown(&device, budget);
}

/// cpuAliveCount is not an input to spawning: even a "full" CPU mirror must
/// not block spawns (the GPU free list decides), and the call re-publishes
/// cpuAliveCount as the capacity used for the update dispatch.
TEST(ParticleEmitter, CalculateSpawnCount_CpuAliveCountDoesNotGateSpawning) {
    MockRHIDevice device;
    ParticleBudget budget(10000u);
    ParticleEmitter e = MakeEmitter(1000u, 100.0f);
    ASSERT_TRUE(e.Initialize(&device, budget));
    e.cpuAliveCount = 1000u;  // CPU mirror claims every slot is occupied

    const uint32_t count = e.CalculateSpawnCount(1.0f);

    EXPECT_EQ(count, 100u)
        << "100/s * 1s must spawn 100 regardless of cpuAliveCount";
    EXPECT_EQ(e.cpuAliveCount, e.buffer.capacity)
        << "cpuAliveCount must mirror the capacity (dispatch = capacity)";

    e.Shutdown(&device, budget);
}

/// A stale cpuAliveCount larger than capacity must neither underflow nor gate
/// spawning; it is reset to the capacity.
TEST(ParticleEmitter, CalculateSpawnCount_AliveExceedsCapacity_NoUnderflow) {
    MockRHIDevice device;
    ParticleBudget budget(10000u);
    ParticleEmitter e = MakeEmitter(100u, 100.0f);
    ASSERT_TRUE(e.Initialize(&device, budget));
    e.cpuAliveCount = 200u;  // stale / corrupt mirror above capacity

    const uint32_t count = e.CalculateSpawnCount(1.0f);

    EXPECT_EQ(count, 100u)
        << "100/s * 1s = 100 fits the 100-slot pool exactly";
    EXPECT_EQ(e.cpuAliveCount, 100u)
        << "cpuAliveCount must be reset to the capacity, not left at 200";

    e.Shutdown(&device, budget);
}

/// A pathological rate * dt far beyond UINT32_MAX must clamp to capacity
/// instead of performing an undefined float -> uint32 conversion.
TEST(ParticleEmitter, CalculateSpawnCount_HugeRate_ClampedWithoutOverflow) {
    MockRHIDevice device;
    ParticleBudget budget(10000u);
    ParticleEmitter e = MakeEmitter(100u, 1.0e12f);
    ASSERT_TRUE(e.Initialize(&device, budget));

    const uint32_t count = e.CalculateSpawnCount(1.0f);

    EXPECT_EQ(count, 100u)
        << "1e12 particles requested in one frame must clamp to the 100-slot pool";

    e.Shutdown(&device, budget);
}
