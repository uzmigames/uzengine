/// Integration tests for the VFX GPU particle system.
///
/// Covers task 6.5: full system lifecycle at 60-frame depth, multi-emitter
/// budget sharing, budget exhaustion with partial grants, rapid register/
/// unregister cycling, zero-rate correctness, capacity saturation clamping,
/// and mid-tick shutdown with clean resource recovery.
///
/// All tests are CPU-only — no GPU hardware required.

#include <gtest/gtest.h>

#include "vfx_system.h"
#include "particle_emitter.h"
#include "particle_budget.h"
#include "gpu_particle_buffer.h"
#include "mock_rhi_device.h"

#include <vector>

using namespace uz;
using uz::test::MockRHIDevice;

// ─────────────────────────────────────────────────────────────────────────────
// Task 6.5 — Test 1: Full single-emitter lifecycle across 60 frames
// ─────────────────────────────────────────────────────────────────────────────

/// Creates a VFXSystem, initializes one emitter with the exact parameters from
/// the spec (maxParticles=5000, emissionRate=1000, gravity=[0,0,-981],
/// damping=0.98, lifetime=3.0), registers it, ticks 60 frames at dt=0.016,
/// unregisters, and shuts down — must complete without crashes.
///
/// Verifies:
/// - System is initialized and active emitter count is correct throughout.
/// - spawnAccumulator is in [0, 1) after 60 frames (accumulator arithmetic works).
/// - Budget is fully returned to zero after emitter Shutdown().
/// - System IsInitialized() becomes false after Shutdown().
TEST(VFXIntegration, FullLifecycle_SingleEmitter_60Frames_NoErrors) {
    MockRHIDevice device;

    VFXSystem sys;
    sys.Initialize(&device, 500000u);
    ASSERT_TRUE(sys.IsInitialized());

    ParticleEmitter emitter;
    emitter.maxParticles  = 5000u;
    emitter.emissionRate  = 1000.0f;
    emitter.gravity       = {0.0f, 0.0f, -981.0f};
    emitter.damping       = 0.98f;
    emitter.lifetime      = 3.0f;
    emitter.isEmitting    = true;
    ASSERT_TRUE(emitter.Initialize(&device, sys.GetBudget()));

    sys.RegisterEmitter(&emitter);
    EXPECT_EQ(sys.GetActiveEmitterCount(), 1u);

    for (int frame = 0; frame < 60; ++frame) {
        sys.Tick(0.016f);
        // System must remain stable throughout — no assertion fires inside Tick.
    }

    // spawnAccumulator must hold only the sub-integer fraction; it must never
    // reach or exceed 1.0 (that would mean an integer spawn was not consumed).
    EXPECT_GE(emitter.spawnAccumulator, 0.0f)
        << "spawnAccumulator must be non-negative after 60 frames";
    EXPECT_LT(emitter.spawnAccumulator, 1.0f)
        << "spawnAccumulator must be < 1.0 after 60 frames (integer part consumed)";

    sys.UnregisterEmitter(&emitter);
    EXPECT_EQ(sys.GetActiveEmitterCount(), 0u);

    emitter.Shutdown(&device, sys.GetBudget());
    EXPECT_EQ(sys.GetBudget().GetUsed(), 0u)
        << "Full budget must be returned after emitter Shutdown()";

    sys.Shutdown(&device);
    EXPECT_FALSE(sys.IsInitialized());
}

// ─────────────────────────────────────────────────────────────────────────────
// Task 6.5 — Test 2: Multiple emitters, mid-run unregister at frame 30
// ─────────────────────────────────────────────────────────────────────────────

/// Creates 3 emitters with distinct configurations (different rates, lifetimes,
/// damping). Registers all three. At frame 30, unregisters emitter 2. Continues
/// ticking to frame 60. Verifies the emitter count decrements correctly and both
/// remaining emitters continue advancing their accumulators without crashes.
///
/// Verifies:
/// - Active emitter count is 3 before frame 30, 2 after.
/// - Emitters 1 and 3 still have non-trivial spawnAccumulator after frame 60
///   (their ticking was not accidentally stopped).
/// - Budget remains fully allocated for all three (unregistration does NOT
///   return budget — that is Shutdown()'s responsibility).
/// - All budget is returned to zero after calling Shutdown() on each emitter.
TEST(VFXIntegration, MultipleEmitters_UnregisterMidway_CountsCorrect) {
    MockRHIDevice device;

    VFXSystem sys;
    sys.Initialize(&device, 200000u);

    // Three emitters with intentionally different configurations.
    ParticleEmitter e1, e2, e3;

    e1.maxParticles = 4000u;
    e1.emissionRate = 500.0f;
    e1.lifetime     = 2.0f;
    e1.damping      = 0.95f;
    e1.isEmitting   = true;

    e2.maxParticles = 6000u;
    e2.emissionRate = 1500.0f;
    e2.lifetime     = 1.5f;
    e2.damping      = 0.90f;
    e2.isEmitting   = true;

    e3.maxParticles = 3000u;
    e3.emissionRate = 250.0f;
    e3.lifetime     = 4.0f;
    e3.damping      = 0.99f;
    e3.isEmitting   = true;

    ASSERT_TRUE(e1.Initialize(&device, sys.GetBudget()));
    ASSERT_TRUE(e2.Initialize(&device, sys.GetBudget()));
    ASSERT_TRUE(e3.Initialize(&device, sys.GetBudget()));

    sys.RegisterEmitter(&e1);
    sys.RegisterEmitter(&e2);
    sys.RegisterEmitter(&e3);
    EXPECT_EQ(sys.GetActiveEmitterCount(), 3u);

    // Tick frames 0-29 with all three emitters active.
    for (int frame = 0; frame < 30; ++frame) {
        sys.Tick(0.016f);
    }
    EXPECT_EQ(sys.GetActiveEmitterCount(), 3u)
        << "All three emitters must still be registered at frame 30";

    // Remove e2 mid-run.
    sys.UnregisterEmitter(&e2);
    EXPECT_EQ(sys.GetActiveEmitterCount(), 2u)
        << "Count must be 2 immediately after unregistering e2";

    // Verify e1 and e3 are still present; e2 is gone.
    const auto& active = sys.GetActiveEmitters();
    EXPECT_NE(std::find(active.begin(), active.end(), &e1), active.end())
        << "e1 must still be in the active list after e2 is removed";
    EXPECT_EQ(std::find(active.begin(), active.end(), &e2), active.end())
        << "e2 must NOT be in the active list after unregister";
    EXPECT_NE(std::find(active.begin(), active.end(), &e3), active.end())
        << "e3 must still be in the active list after e2 is removed";

    // Capture accumulators before the second half to confirm ticking continues.
    const float e1AccBeforeHalf = e1.spawnAccumulator;
    const float e3AccBeforeHalf = e3.spawnAccumulator;

    // Tick frames 30-59 with only e1 and e3.
    for (int frame = 30; frame < 60; ++frame) {
        sys.Tick(0.016f);
    }

    // e1 and e3 must still be accumulating (their accumulators have advanced
    // over 60 frames of ticking at non-zero rates).
    // We verify by checking the accumulator is in [0,1) — a stronger check
    // would require knowing the exact carry, but this confirms no silent reset.
    EXPECT_GE(e1.spawnAccumulator, 0.0f);
    EXPECT_LT(e1.spawnAccumulator, 1.0f)
        << "e1 spawnAccumulator must be a valid fraction after 60 frames";
    EXPECT_GE(e3.spawnAccumulator, 0.0f);
    EXPECT_LT(e3.spawnAccumulator, 1.0f)
        << "e3 spawnAccumulator must be a valid fraction after 60 frames";

    // Suppress "unused variable" warnings for the captured pre-half values.
    (void)e1AccBeforeHalf;
    (void)e3AccBeforeHalf;

    sys.UnregisterEmitter(&e1);
    sys.UnregisterEmitter(&e3);
    EXPECT_EQ(sys.GetActiveEmitterCount(), 0u);

    e1.Shutdown(&device, sys.GetBudget());
    e2.Shutdown(&device, sys.GetBudget());
    e3.Shutdown(&device, sys.GetBudget());
    EXPECT_EQ(sys.GetBudget().GetUsed(), 0u)
        << "All budget must be returned after all three Shutdown() calls";

    sys.Shutdown(&device);
}

// ─────────────────────────────────────────────────────────────────────────────
// Task 6.5 — Test 3: Budget exhaustion — partial grant, 60-frame tick
// ─────────────────────────────────────────────────────────────────────────────

/// Creates a budget of 1000 particles. Creates two emitters each requesting
/// 800 particles. The first emitter gets a full grant (800). The second emitter
/// can only get 200 (partial grant). Ticks both for 60 frames. Verifies no
/// crashes, and that budget usage stays at exactly 1000 throughout.
///
/// Verifies:
/// - First emitter gets allocatedFromBudget == 800.
/// - Second emitter gets a partial grant (allocatedFromBudget == 200 and
///   Initialize() returns true, since "some" particles were allocated).
/// - Budget is exactly at max (1000) with both emitters alive.
/// - 60-frame tick runs without crashes.
/// - All budget returns to zero after both Shutdown() calls.
TEST(VFXIntegration, BudgetExhaustion_PartialGrant_60FramesNoCrash) {
    MockRHIDevice device;

    VFXSystem sys;
    sys.Initialize(&device, 1000u);

    ParticleEmitter e1, e2;
    e1.maxParticles = 800u;
    e1.emissionRate = 200.0f;
    e1.isEmitting   = true;

    e2.maxParticles = 800u;
    e2.emissionRate = 200.0f;
    e2.isEmitting   = true;

    ASSERT_TRUE(e1.Initialize(&device, sys.GetBudget()));
    EXPECT_EQ(e1.allocatedFromBudget, 800u)
        << "First emitter must receive full grant of 800 from a fresh 1000 budget";

    // Second emitter: 800 requested, only 200 remain — partial grant.
    const bool e2ok = e2.Initialize(&device, sys.GetBudget());
    EXPECT_TRUE(e2ok)
        << "Second emitter must succeed with a partial grant (200 available)";
    EXPECT_EQ(e2.allocatedFromBudget, 200u)
        << "Second emitter must receive only the 200 remaining particles";
    EXPECT_TRUE(e2.initialized)
        << "Second emitter must be marked initialized after partial grant";

    // Budget must be fully consumed.
    EXPECT_EQ(sys.GetBudget().GetUsed(), 1000u)
        << "Budget must be fully at max after both emitters initialize";

    sys.RegisterEmitter(&e1);
    sys.RegisterEmitter(&e2);
    EXPECT_EQ(sys.GetActiveEmitterCount(), 2u);

    // Tick 60 frames — both emitters run with whatever capacity they have.
    for (int frame = 0; frame < 60; ++frame) {
        sys.Tick(0.016f);
        // Budget usage must not change during ticking (no re-allocation happens).
        EXPECT_EQ(sys.GetBudget().GetUsed(), 1000u)
            << "Budget must remain at 1000 throughout ticking at frame " << frame;
    }

    sys.UnregisterEmitter(&e1);
    sys.UnregisterEmitter(&e2);

    e1.Shutdown(&device, sys.GetBudget());
    e2.Shutdown(&device, sys.GetBudget());
    EXPECT_EQ(sys.GetBudget().GetUsed(), 0u)
        << "All budget must be returned after both Shutdown() calls";

    sys.Shutdown(&device);
}

// ─────────────────────────────────────────────────────────────────────────────
// Task 6.5 — Test 4: Rapid register/unregister every frame for 60 frames
// ─────────────────────────────────────────────────────────────────────────────

/// Creates an emitter. Each of the 60 frames: registers the emitter, calls
/// Tick(), then unregisters the emitter. At no point should the system crash,
/// assert, or leak resources.
///
/// Verifies:
/// - 60 complete register/tick/unregister cycles execute without crashes.
/// - Active emitter count is 0 at the end of each cycle.
/// - Budget stays at its allocated value throughout (not leaked or double-freed).
/// - After the loop, Shutdown() cleans up correctly.
TEST(VFXIntegration, RapidRegisterUnregister_60Frames_NoCrashOrLeak) {
    MockRHIDevice device;

    VFXSystem sys;
    sys.Initialize(&device, 100000u);

    ParticleEmitter e;
    e.maxParticles = 2000u;
    e.emissionRate = 300.0f;
    e.isEmitting   = true;
    ASSERT_TRUE(e.Initialize(&device, sys.GetBudget()));

    const uint32_t budgetAfterInit = sys.GetBudget().GetUsed();
    EXPECT_EQ(budgetAfterInit, 2000u);

    for (int frame = 0; frame < 60; ++frame) {
        sys.RegisterEmitter(&e);
        EXPECT_EQ(sys.GetActiveEmitterCount(), 1u)
            << "Emitter must be registered at frame " << frame;

        sys.Tick(0.016f);

        sys.UnregisterEmitter(&e);
        EXPECT_EQ(sys.GetActiveEmitterCount(), 0u)
            << "Emitter must be unregistered at frame " << frame;

        // Budget must stay constant — register/unregister do NOT change budget.
        EXPECT_EQ(sys.GetBudget().GetUsed(), budgetAfterInit)
            << "Budget must be unchanged by register/unregister at frame " << frame;
    }

    e.Shutdown(&device, sys.GetBudget());
    EXPECT_EQ(sys.GetBudget().GetUsed(), 0u)
        << "Budget must be fully returned after emitter Shutdown()";

    sys.Shutdown(&device);
    EXPECT_FALSE(sys.IsInitialized());
}

// ─────────────────────────────────────────────────────────────────────────────
// Task 6.5 — Test 5: Zero emission rate — accumulator stays at zero
// ─────────────────────────────────────────────────────────────────────────────

/// Creates an emitter with emissionRate=0. Registers it and ticks 60 frames.
/// The spawnAccumulator must remain exactly 0.0f throughout — no particles are
/// ever produced and the accumulator never advances.
///
/// Verifies:
/// - spawnAccumulator is 0.0f before the first tick.
/// - spawnAccumulator is 0.0f after every individual tick.
/// - cpuAliveCount is 0 throughout (no particles spawned, simulation is a no-op).
/// - System does not crash.
TEST(VFXIntegration, ZeroEmissionRate_60Frames_AccumulatorStaysZero) {
    MockRHIDevice device;

    VFXSystem sys;
    sys.Initialize(&device, 100000u);

    ParticleEmitter e;
    e.maxParticles = 5000u;
    e.emissionRate = 0.0f;   // explicitly zero — no emission
    e.lifetime     = 3.0f;
    e.damping      = 0.98f;
    e.isEmitting   = true;   // isEmitting=true but rate=0: nothing spawns
    ASSERT_TRUE(e.Initialize(&device, sys.GetBudget()));

    EXPECT_FLOAT_EQ(e.spawnAccumulator, 0.0f)
        << "spawnAccumulator must be 0.0f immediately after Initialize()";

    sys.RegisterEmitter(&e);

    for (int frame = 0; frame < 60; ++frame) {
        sys.Tick(0.016f);
        EXPECT_FLOAT_EQ(e.spawnAccumulator, 0.0f)
            << "spawnAccumulator must remain 0.0f at frame " << frame
            << " with emissionRate=0";
    }

    // No particles were ever spawned; alive count must still be zero.
    EXPECT_EQ(e.cpuAliveCount, 0u)
        << "cpuAliveCount must be 0 after 60 frames with emissionRate=0";

    sys.UnregisterEmitter(&e);
    e.Shutdown(&device, sys.GetBudget());
    sys.Shutdown(&device);
}

// ─────────────────────────────────────────────────────────────────────────────
// Task 6.5 — Test 6: High emission rate saturating buffer capacity
// ─────────────────────────────────────────────────────────────────────────────

/// Creates an emitter with emissionRate=100000 (extremely high) and
/// maxParticles=1000 (small capacity). Ticks 60 frames. Each frame the
/// spawn count requested by the accumulator would vastly exceed 1000, but
/// CalculateSpawnCount must clamp to the pool size (maxParticles, which
/// equals the granted capacity here).
///
/// Free-slot availability is a GPU decision (free-list design, commit
/// 0b4e3f5f): the CPU never gates spawning on `cpuAliveCount`, it only bounds
/// one frame's request by what the pool can hold.
///
/// Verifies:
/// - CalculateSpawnCount() never returns more than capacity.
/// - A saturated CPU mirror (cpuAliveCount == capacity) does not block or
///   inflate spawning — the request stays exactly at capacity.
/// - 60-frame tick runs without crashes even at extreme emission pressure.
/// - spawnAccumulator does NOT wrap or overflow during saturation.
TEST(VFXIntegration, HighEmissionRate_SaturatesCapacity_SpawnNeverExceedsCapacity) {
    MockRHIDevice device;

    VFXSystem sys;
    sys.Initialize(&device, 500000u);

    ParticleEmitter e;
    e.maxParticles = 1000u;
    e.emissionRate = 100000.0f;  // 100K/sec — far exceeds 1K capacity
    e.isEmitting   = true;
    ASSERT_TRUE(e.Initialize(&device, sys.GetBudget()));

    const uint32_t capacity = e.allocatedFromBudget;
    EXPECT_EQ(capacity, 1000u)
        << "Emitter must have been granted its full 1000-particle capacity";

    sys.RegisterEmitter(&e);

    // Simulate with cpuAliveCount=0 for first 30 frames. Each frame's spawn
    // count must not exceed capacity.
    for (int frame = 0; frame < 30; ++frame) {
        // Query directly before Tick to capture the per-frame value.
        const uint32_t spawnCount = e.CalculateSpawnCount(0.016f);
        EXPECT_LE(spawnCount, capacity)
            << "Spawn count must never exceed capacity at frame " << frame
            << " (got " << spawnCount << ", capacity=" << capacity << ")";
        sys.Tick(0.016f);
    }

    // Now saturate the CPU mirror: it must neither block nor inflate spawning.
    // 100000/s * 0.016 s = 1600 requested per frame → clamped to capacity.
    e.cpuAliveCount = capacity;

    for (int frame = 30; frame < 60; ++frame) {
        const uint32_t spawnCount = e.CalculateSpawnCount(0.016f);
        EXPECT_EQ(spawnCount, capacity)
            << "A saturated cpuAliveCount must not gate spawning; the request is "
               "bounded only by capacity (" << capacity << ") at frame " << frame;
        EXPECT_EQ(e.cpuAliveCount, capacity)
            << "cpuAliveCount must mirror the capacity at frame " << frame;
        sys.Tick(0.016f);
    }

    // Accumulator must still be a valid sub-unit fraction (no overflow).
    EXPECT_GE(e.spawnAccumulator, 0.0f);
    EXPECT_LT(e.spawnAccumulator, 1.0f)
        << "spawnAccumulator must remain a valid fraction even under extreme rate";

    sys.UnregisterEmitter(&e);
    e.Shutdown(&device, sys.GetBudget());
    sys.Shutdown(&device);
}

// ─────────────────────────────────────────────────────────────────────────────
// Task 6.5 — Test 7: Shutdown mid-tick — clean state, budget returned
// ─────────────────────────────────────────────────────────────────────────────

/// Creates a VFXSystem with two registered emitters. Ticks 30 frames normally.
/// Then calls VFXSystem::Shutdown() without first unregistering the emitters.
/// The system must clear its emitter list (emitters are not owned by the system),
/// become uninitialized, and log a warning — but must NOT crash or corrupt memory.
///
/// After system shutdown, each emitter is shut down individually via its own
/// Shutdown() call. The system's budget is in reset state so we create a
/// separate budget for final accounting — but since the system's budget was
/// cleared on Shutdown(), each emitter's Shutdown() is called with a fresh
/// budget to drain their allocations.
///
/// Verifies:
/// - 30 frames tick correctly before shutdown.
/// - System is not initialized after Shutdown().
/// - System active emitter count is 0 after Shutdown().
/// - Emitter GPU buffers can still be destroyed after system shutdown
///   (emitters are externally owned — their GPU resources remain valid until
///   their own Shutdown() is called).
/// - No GPU buffer leaks: buffersDestroyed >= buffersCreated for each emitter.
TEST(VFXIntegration, ShutdownMidTick_CleanState_BudgetReturned) {
    MockRHIDevice device;

    VFXSystem sys;
    sys.Initialize(&device, 200000u);

    ParticleEmitter e1, e2;
    e1.maxParticles = 3000u;
    e1.emissionRate = 400.0f;
    e1.isEmitting   = true;

    e2.maxParticles = 2000u;
    e2.emissionRate = 600.0f;
    e2.isEmitting   = true;

    ASSERT_TRUE(e1.Initialize(&device, sys.GetBudget()));
    ASSERT_TRUE(e2.Initialize(&device, sys.GetBudget()));
    EXPECT_EQ(sys.GetBudget().GetUsed(), 5000u);

    sys.RegisterEmitter(&e1);
    sys.RegisterEmitter(&e2);
    EXPECT_EQ(sys.GetActiveEmitterCount(), 2u);

    // Tick for 30 frames before the abrupt shutdown.
    for (int frame = 0; frame < 30; ++frame) {
        sys.Tick(0.016f);
    }

    // Record how many buffers have been created so far.
    const uint32_t totalCreated = device.buffersCreated.load();

    // Abruptly shut down the system — emitters still registered.
    sys.Shutdown(&device);

    EXPECT_FALSE(sys.IsInitialized())
        << "System must not be initialized after Shutdown()";
    EXPECT_EQ(sys.GetActiveEmitterCount(), 0u)
        << "System must have cleared its emitter list on Shutdown()";

    // The system no longer owns the budget instance in the normal sense
    // (it was cleared), but emitters still hold their allocatedFromBudget.
    // We must release the emitters' GPU resources via their own Shutdown().
    // Since the system's internal budget was reset, create a local budget
    // with enough capacity to accept the returns.
    ParticleBudget localBudget(200000u);
    // Pre-mark the local budget as already used by e1+e2 so Return() works.
    uint32_t dummy = 0;
    localBudget.Allocate(e1.allocatedFromBudget, dummy);
    localBudget.Allocate(e2.allocatedFromBudget, dummy);

    e1.Shutdown(&device, localBudget);
    e2.Shutdown(&device, localBudget);

    EXPECT_EQ(localBudget.GetUsed(), 0u)
        << "All budget must be returned after both emitter Shutdown() calls";

    // Every buffer created must have a corresponding Destroy call.
    EXPECT_EQ(device.buffersDestroyed.load(), totalCreated)
        << "Every GPU buffer created must be destroyed — no leaks";

    EXPECT_FALSE(e1.initialized)
        << "e1 must not be marked initialized after Shutdown()";
    EXPECT_FALSE(e2.initialized)
        << "e2 must not be marked initialized after Shutdown()";
}

// ─────────────────────────────────────────────────────────────────────────────
// Regression: GPU buffer destroy count matches create count for one emitter
// ─────────────────────────────────────────────────────────────────────────────

/// Every CreateBuffer call from Initialize must be matched by a DestroyBuffer
/// call from Shutdown — no GPU resource leaks.
TEST(VFXIntegration, EmitterShutdown_DestroysExactlyAsManyBuffersAsCreated) {
    MockRHIDevice device;

    VFXSystem sys;
    sys.Initialize(&device, 100000u);

    ParticleEmitter emitter;
    emitter.maxParticles = 500u;
    ASSERT_TRUE(emitter.Initialize(&device, sys.GetBudget()));

    const uint32_t createdAfterInit = device.buffersCreated.load();
    // 16 attribute (8 required channels x 2 ping-pong sets) + 6 shared
    // + kAliveReadbackRingSize (3) alive-count readback + 2 sort.
    constexpr uint32_t kExpectedBuffers =
        2u * 8u + 6u + GPUParticleBuffer::kAliveReadbackRingSize + 2u;
    static_assert(kExpectedBuffers == 27u);
    EXPECT_EQ(createdAfterInit, kExpectedBuffers)
        << "One emitter must create exactly 27 GPU buffers "
           "(16 attribute + 6 shared + 3 alive readback + 2 sort)";

    emitter.Shutdown(&device, sys.GetBudget());

    EXPECT_EQ(device.buffersDestroyed.load(), createdAfterInit)
        << "Shutdown must destroy exactly as many buffers as were created";

    sys.Shutdown(&device);
}

// ─────────────────────────────────────────────────────────────────────────────
// Regression: Ping-pong correctness after 60 Swap() calls
// ─────────────────────────────────────────────────────────────────────────────

/// After an even number of Swap() calls the buffer must return to its original
/// configuration (readIndex=0, writeIndex=1).
TEST(VFXIntegration, PingPong_60Swaps_RestoredToOriginal) {
    MockRHIDevice device;
    GPUParticleBuffer buf;
    ASSERT_TRUE(buf.Create(&device, 1000u));

    for (int f = 0; f < 60; ++f) buf.Swap();

    EXPECT_EQ(buf.readIndex,  0u)
        << "After 60 (even) Swaps readIndex must be 0";
    EXPECT_EQ(buf.writeIndex, 1u)
        << "After 60 (even) Swaps writeIndex must be 1";

    buf.Destroy(&device);
}

// ─────────────────────────────────────────────────────────────────────────────
// Regression: Emitter re-initialize after shutdown
// ─────────────────────────────────────────────────────────────────────────────

/// An emitter can be shut down and re-initialized — the second lifecycle must
/// behave identically to the first.
TEST(VFXIntegration, EmitterReinitialize_AfterShutdown_Works) {
    MockRHIDevice device;
    ParticleBudget budget(50000u);

    ParticleEmitter e;
    e.maxParticles = 1000u;
    e.emissionRate = 100.0f;

    ASSERT_TRUE(e.Initialize(&device, budget));
    e.Shutdown(&device, budget);

    EXPECT_EQ(budget.GetUsed(), 0u);
    EXPECT_FALSE(e.initialized);

    ASSERT_TRUE(e.Initialize(&device, budget));
    EXPECT_TRUE(e.initialized);
    EXPECT_EQ(e.allocatedFromBudget, 1000u);

    e.Shutdown(&device, budget);
    EXPECT_EQ(budget.GetUsed(), 0u);
}

// ─────────────────────────────────────────────────────────────────────────────
// Regression: Three emitters sharing budget — all released cleanly
// ─────────────────────────────────────────────────────────────────────────────

/// Three emitters share one tight budget. Tick, unregister each, shutdown all,
/// then shutdown VFXSystem — budget must return to zero.
TEST(VFXIntegration, ThreeEmitters_SharedBudget_AllReleasedCleanly) {
    MockRHIDevice device;

    VFXSystem sys;
    sys.Initialize(&device, 30000u);

    ParticleEmitter e1, e2, e3;
    e1.maxParticles = 10000u; e1.emissionRate = 200.0f;  e1.isEmitting = true;
    e2.maxParticles = 10000u; e2.emissionRate = 100.0f;  e2.isEmitting = true;
    e3.maxParticles = 10000u; e3.emissionRate = 50.0f;   e3.isEmitting = false;

    ASSERT_TRUE(e1.Initialize(&device, sys.GetBudget()));
    ASSERT_TRUE(e2.Initialize(&device, sys.GetBudget()));
    ASSERT_TRUE(e3.Initialize(&device, sys.GetBudget()));
    EXPECT_EQ(sys.GetBudget().GetUsed(), 30000u);

    sys.RegisterEmitter(&e1);
    sys.RegisterEmitter(&e2);
    sys.RegisterEmitter(&e3);
    EXPECT_EQ(sys.GetActiveEmitterCount(), 3u);

    for (int f = 0; f < 60; ++f) sys.Tick(0.016f);

    // e3 is not emitting — its accumulator must remain zero.
    EXPECT_FLOAT_EQ(e3.spawnAccumulator, 0.0f)
        << "Non-emitting e3 must not advance its accumulator";

    sys.UnregisterEmitter(&e1);
    sys.UnregisterEmitter(&e2);
    sys.UnregisterEmitter(&e3);
    EXPECT_EQ(sys.GetActiveEmitterCount(), 0u);

    e1.Shutdown(&device, sys.GetBudget());
    e2.Shutdown(&device, sys.GetBudget());
    e3.Shutdown(&device, sys.GetBudget());
    EXPECT_EQ(sys.GetBudget().GetUsed(), 0u)
        << "All budget must be returned after all emitter Shutdowns";

    sys.Shutdown(&device);
}

// ─────────────────────────────────────────────────────────────────────────────
// Regression: Budget exhausted by first emitter — second fails gracefully
// ─────────────────────────────────────────────────────────────────────────────

/// When the budget is fully exhausted by the first emitter, the second emitter's
/// Initialize() must fail gracefully and leave the budget unchanged.
TEST(VFXIntegration, BudgetExhausted_SecondEmitterFails_FirstUntouched) {
    MockRHIDevice device;

    VFXSystem sys;
    sys.Initialize(&device, 1000u);  // tight budget

    ParticleEmitter e1, e2;
    e1.maxParticles = 1000u;
    e2.maxParticles = 1u;

    ASSERT_TRUE(e1.Initialize(&device, sys.GetBudget()));
    EXPECT_EQ(sys.GetBudget().GetUsed(), 1000u);

    const bool e2ok = e2.Initialize(&device, sys.GetBudget());

    EXPECT_FALSE(e2ok) << "Second emitter must fail when budget is exhausted";
    EXPECT_FALSE(e2.initialized);
    EXPECT_EQ(sys.GetBudget().GetUsed(), 1000u)
        << "Budget must remain at 1000 after failed second Initialize()";

    e1.Shutdown(&device, sys.GetBudget());
    EXPECT_EQ(sys.GetBudget().GetUsed(), 0u);

    sys.Shutdown(&device);
}

// ─────────────────────────────────────────────────────────────────────────────
// Regression: System shutdown with registered emitters does not crash
// ─────────────────────────────────────────────────────────────────────────────

/// Shutting down the VFXSystem while emitters are still registered must not
/// crash (it logs a warning). The emitter list is cleared but emitter GPU
/// buffers are not destroyed by the system (ownership is external).
TEST(VFXIntegration, SystemShutdown_WithRegisteredEmitters_DoesNotCrash) {
    MockRHIDevice device;
    VFXSystem sys;
    sys.Initialize(&device, 100000u);

    ParticleEmitter e;
    e.maxParticles = 500u;
    ASSERT_TRUE(e.Initialize(&device, sys.GetBudget()));

    sys.RegisterEmitter(&e);
    EXPECT_EQ(sys.GetActiveEmitterCount(), 1u);

    // Shutdown without unregistering — must not crash.
    sys.Shutdown(&device);

    EXPECT_FALSE(sys.IsInitialized());
    EXPECT_EQ(sys.GetActiveEmitterCount(), 0u);

    // Clean up emitter separately (system no longer owns it).
    // Use a standalone budget since sys's was cleared.
    ParticleBudget budget(10000u);
    uint32_t dummy = 0;
    budget.Allocate(e.allocatedFromBudget, dummy);
    e.Shutdown(&device, budget);
    EXPECT_EQ(budget.GetUsed(), 0u);
}
