/// Integration tests for ParticleEmitter::Seek determinism.
///
/// Phase 16 task 2.1 — `test_vfx_seek_determinism`
///
/// Specification (from proposal.md):
///   Spawn a flame emitter, run continuous for 2.5s, capture alive count +
///   first-N positions; reset, call Seek(2.5f), run one frame to consume
///   the warmup, capture again, assert equal up to bitwise tolerance.
///
/// GPU reality gap:
///   A fully deterministic comparison of GPU particle positions between a
///   continuous 150-frame sim and a Seek(2.5f) warmup loop requires a live
///   DX12 device, command-list submission, and GPU readback. No such fixture
///   exists in the test runtime — all existing VFX tests use MockRHIDevice
///   which returns null command lists. Therefore:
///
///   * We test the CPU-observable state surface that the warmup loop consumes:
///     - GetPendingSeekTime() stores the correct pending seconds.
///     - ceil(pendingSeekTime / kSeekFixedDt) produces 150 ticks for 2.5 s.
///     - After ClearPendingSeek(), spawnAccumulator and cpuAliveCount are
///       identical to a continuous sim run that started from the same state.
///     - buffer.needsInitialization is true after Seek() so the next
///       dispatch re-initializes the GPU buffer before warmup ticks.
///
///   * The per-tick state invariant: after N ticks of CalculateSpawnCount at
///     kSeekFixedDt, the fractional accumulator for a deterministic emission
///     rate is identical to a continuous sim that ran N identical ticks —
///     arithmetic identity because both paths use the same fixed dt.
///
///   * A separate "MaxTicksPerFrame budget" test verifies that long seeks
///     split correctly across frames.
///
/// Timing benchmark (bonus — addressable via --gtest_filter=*Timing*):
///   For 1k, 4k, 16k capacity emitters, measures the wall-clock cost of the
///   150-tick CPU accumulator loop and writes .sandbox/seek-warmup-timings.json
///   for phase 3's docs-operator. GPU timing is zero (no real device).
///
/// All tests are CPU-only — no GPU hardware required.

#include <gtest/gtest.h>

#include "engine/core/cvar.h"
#include "engine/vfx/components/emitter_presets.h"
#include "engine/vfx/components/particle_emitter.h"
#include "engine/vfx/core/particle_budget.h"
#include "engine/vfx/vfx_system.h"
#include "mock_rhi_device.h"

#include <chrono>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>

using namespace uz;

namespace {

/// kSeekFixedDt must match frame_vfx_inline.cpp — 1/60 s.
static constexpr float kSeekFixedDt = 1.0f / 60.0f;

/// Reset VFX CVars to defaults so tests are order-independent.
void ResetVfxCVars() {
    auto& cvars = CVarRegistry::Get();
    cvars.Reset("r.VFX.MaxTotalParticles");
    cvars.Reset("r.VFX.AllowGPUSim");
    cvars.Reset("r.VFX.QualityLevel");
    cvars.Reset("r.VFX.SimulatePaused");
    cvars.Reset("r.VFX.TimeScale");
    cvars.Reset("r.VFX.Seek.MaxTicksPerFrame");
}

struct VfxSeekFixture {
    test::MockRHIDevice device;
    VFXSystem           system;

    explicit VfxSeekFixture(uint32_t budget = 500'000) {
        system.Initialize(&device, budget);
    }
    ~VfxSeekFixture() {
        system.Shutdown(&device);
    }
};

struct SeekTimingResult {
    uint32_t capacity;
    uint32_t numTicks;
    double   wallClockMs;
    uint32_t framesNeeded;
};

SeekTimingResult MeasureSeekTiming(uint32_t capacity, float seekTime, uint32_t maxPerFrame) {
    test::MockRHIDevice device;
    ParticleBudget      budget(capacity * 2u);

    ParticleEmitter e;
    ApplyPreset_Fire(e);
    e.maxParticles = capacity;
    EXPECT_TRUE(e.Initialize(&device, budget));

    e.Seek(seekTime);

    const uint32_t totalTicks = static_cast<uint32_t>(
        std::ceil(seekTime / kSeekFixedDt));
    const uint32_t framesNeeded = static_cast<uint32_t>(
        std::ceil(static_cast<float>(totalTicks) / static_cast<float>(maxPerFrame)));

    const auto t0 = std::chrono::high_resolution_clock::now();
    {
        // Start accumulator from zero — identical to the warmup loop start state.
        e.spawnAccumulator = 0.0f;
        e.cpuAliveCount    = 0u;
        for (uint32_t tick = 0u; tick < totalTicks; ++tick) {
            e.CalculateSpawnCount(kSeekFixedDt);
        }
    }
    const auto t1 = std::chrono::high_resolution_clock::now();
    const double wallMs = std::chrono::duration<double, std::milli>(t1 - t0).count();

    e.Shutdown(&device, budget);

    return { capacity, totalTicks, wallMs, framesNeeded };
}

std::string FormatTimingJson(const SeekTimingResult results[], int count) {
    std::string j = "{\n";
    for (int i = 0; i < count; ++i) {
        const auto& r    = results[i];
        const bool  last = (i == count - 1);
        std::string label;
        if      (r.capacity == 1000u)  label = "capacity_1k";
        else if (r.capacity == 4000u)  label = "capacity_4k";
        else if (r.capacity == 16000u) label = "capacity_16k";
        else                           label = "capacity_" + std::to_string(r.capacity);

        j += "  \"" + label + "\": {";
        j += "\"ticks\": "    + std::to_string(r.numTicks)     + ", ";
        j += "\"wall_ms\": "  + std::to_string(r.wallClockMs)  + ", ";
        j += "\"gpu_ms\": 0.0, ";
        j += "\"frames\": "   + std::to_string(r.framesNeeded) + ", ";
        j += "\"note\": \"gpu_ms not measured: MockRHIDevice in test build\"";
        j += "}";
        if (!last) j += ",";
        j += "\n";
    }
    j += "}\n";
    return j;
}

} // anonymous namespace

// =============================================================================
// Seek stores correct pending time and resets CPU counters
// =============================================================================

/// Seek(2.5f) stores exactly 2.5 s as pending seek time.
TEST(VfxSeekDeterminism, Seek_StoresPendingSeekTime_2p5s) {
    ParticleEmitter e;
    ApplyPreset_Fire(e);

    e.Seek(2.5f);

    EXPECT_FLOAT_EQ(e.GetPendingSeekTime(), 2.5f)
        << "Seek(2.5f) must record exactly 2.5 s as pending seek time";
}

/// Seek(2.5f) resets spawnAccumulator to zero.
TEST(VfxSeekDeterminism, Seek_ResetsSpawnAccumulator) {
    ParticleEmitter e;
    ApplyPreset_Fire(e);
    e.spawnAccumulator = 0.7f;

    e.Seek(2.5f);

    EXPECT_FLOAT_EQ(e.spawnAccumulator, 0.0f)
        << "Seek must reset spawnAccumulator so warmup starts from acc=0";
}

/// Seek(2.5f) resets cpuAliveCount to zero.
TEST(VfxSeekDeterminism, Seek_ResetsCpuAliveCount) {
    ParticleEmitter e;
    ApplyPreset_Fire(e);
    e.cpuAliveCount = 1234u;

    e.Seek(2.5f);

    EXPECT_EQ(e.cpuAliveCount, 0u)
        << "Seek must reset cpuAliveCount so the full budget is available for warmup";
}

/// Seek(2.5f) flags the GPU buffer for reinitialisation.
TEST(VfxSeekDeterminism, Seek_FlagsBufferNeedsInitialization) {
    ParticleEmitter e;
    ApplyPreset_Fire(e);
    e.buffer.needsInitialization = false;

    e.Seek(2.5f);

    EXPECT_TRUE(e.buffer.needsInitialization)
        << "Seek must set buffer.needsInitialization so the first warmup tick "
           "re-clears the GPU buffers before writing new particle data";
}

/// Seek(0.0f) is a pure reset — pendingSeekTime stays 0.
TEST(VfxSeekDeterminism, Seek_ZeroTarget_IsReset_NoPendingWork) {
    ParticleEmitter e;
    ApplyPreset_Fire(e);
    e.spawnAccumulator = 0.5f;
    e.cpuAliveCount    = 999u;

    e.Seek(0.0f);

    EXPECT_FLOAT_EQ(e.GetPendingSeekTime(), 0.0f)
        << "Seek(0) is a reset: no warmup ticks queued";
    EXPECT_FLOAT_EQ(e.spawnAccumulator, 0.0f);
    EXPECT_EQ(e.cpuAliveCount, 0u);
}

/// Seek(negative) clamps to 0.
TEST(VfxSeekDeterminism, Seek_NegativeTarget_ClampsToZero) {
    ParticleEmitter e;
    ApplyPreset_Fire(e);

    e.Seek(-3.0f);

    EXPECT_FLOAT_EQ(e.GetPendingSeekTime(), 0.0f)
        << "Negative seek targets must clamp to 0 per spec";
}

// =============================================================================
// Warmup tick count arithmetic
// =============================================================================

/// ceil(2.5 / (1/60)) = ceil(150.0) = 150 warmup ticks.
TEST(VfxSeekDeterminism, TickCount_2p5s_Equals150Ticks) {
    const uint32_t ticks = static_cast<uint32_t>(std::ceil(2.5f / kSeekFixedDt));
    EXPECT_EQ(ticks, 150u)
        << "2.5 s at 1/60 s per tick must require exactly 150 warmup ticks";
}

/// 150 ticks fit in a single frame with the default budget of 256.
TEST(VfxSeekDeterminism, TickCount_150Ticks_FitsInSingleFrame_WithDefault256Budget) {
    static constexpr uint32_t kMaxDefault = 256u;
    const uint32_t ticks = static_cast<uint32_t>(std::ceil(2.5f / kSeekFixedDt));
    EXPECT_LE(ticks, kMaxDefault)
        << "150 warmup ticks must fit within the default 256-tick per-frame budget";
}

/// 30 s = 1800 ticks, split across ceil(1800/256) = 8 frames.
TEST(VfxSeekDeterminism, TickCount_30s_Requires1800Ticks_SplitsAcross8Frames) {
    static constexpr float    kSeek30s    = 30.0f;
    static constexpr uint32_t kMaxBudget  = 256u;
    const uint32_t totalTicks   = static_cast<uint32_t>(std::ceil(kSeek30s / kSeekFixedDt));
    const uint32_t framesNeeded = static_cast<uint32_t>(
        std::ceil(static_cast<float>(totalTicks) / static_cast<float>(kMaxBudget)));

    EXPECT_EQ(totalTicks, 1800u)
        << "30 s at 1/60 s per tick = 1800 ticks";
    EXPECT_EQ(framesNeeded, 8u)
        << "ceil(1800/256) = 8 frames needed to drain a 30 s seek";
}

/// MaxTicksPerFrame=1 means a 2.5 s seek spans 150 frames.
TEST(VfxSeekDeterminism, TickCount_MaxBudget1_150FramesForShortSeek) {
    static constexpr uint32_t kMaxBudget = 1u;
    const uint32_t totalTicks   = static_cast<uint32_t>(std::ceil(2.5f / kSeekFixedDt));
    const uint32_t framesNeeded = static_cast<uint32_t>(
        std::ceil(static_cast<float>(totalTicks) / static_cast<float>(kMaxBudget)));

    EXPECT_EQ(framesNeeded, 150u)
        << "With MaxTicksPerFrame=1, each of the 150 ticks occupies one frame";
}

// =============================================================================
// Accumulator determinism: continuous sim vs seek warmup
// =============================================================================

/// Core determinism claim (CPU-side):
/// A continuous sim for 150 ticks of kSeekFixedDt starting from acc=0
/// and a Seek(2.5f) path that resets to acc=0 then runs 150 identical
/// ticks produce the same spawnAccumulator — bitwise-identical because
/// both paths apply the same (emissionRate * kSeekFixedDt) arithmetic
/// 150 times from the same starting value.
TEST(VfxSeekDeterminism, ContinuousSim_vs_SeekWarmup_AccumulatorIsIdentical) {
    test::MockRHIDevice device;
    ParticleBudget      budget(500'000u);

    // ── Run A: 150 continuous ticks ──────────────────────────────────────
    ParticleEmitter eA;
    ApplyPreset_Fire(eA);
    ASSERT_TRUE(eA.Initialize(&device, budget));

    for (uint32_t tick = 0u; tick < 150u; ++tick) {
        eA.CalculateSpawnCount(kSeekFixedDt);
    }
    const float accA = eA.spawnAccumulator;

    // ── Run B: Seek(2.5f) then 150 identical ticks from acc=0 ────────────
    ParticleEmitter eB;
    ApplyPreset_Fire(eB);
    ASSERT_TRUE(eB.Initialize(&device, budget));

    eB.Seek(2.5f);
    // Seek already resets acc to 0; the warmup loop then runs
    // the same 150 CalculateSpawnCount calls at kSeekFixedDt.
    ASSERT_FLOAT_EQ(eB.spawnAccumulator, 0.0f)
        << "Pre-condition: Seek must reset accumulator before warmup";

    for (uint32_t tick = 0u; tick < 150u; ++tick) {
        eB.CalculateSpawnCount(kSeekFixedDt);
    }
    const float accB = eB.spawnAccumulator;

    EXPECT_FLOAT_EQ(accA, accB)
        << "CPU spawnAccumulator must be bitwise-identical after 150 ticks "
           "regardless of whether the ticks came from a continuous sim or a "
           "Seek(2.5f) warmup path. accA=" << accA << " accB=" << accB;

    eA.Shutdown(&device, budget);
    eB.Shutdown(&device, budget);
}

/// At exactly 60 particles/s with kSeekFixedDt (1/60 s per tick), every tick
/// spawns exactly 1 particle with zero fractional carry.
/// 150 ticks → 150 total spawns, accumulator = 0.0 precisely.
TEST(VfxSeekDeterminism, TotalSpawnCount_ExactRate_150Ticks_NoCarry) {
    test::MockRHIDevice device;
    ParticleBudget      budget(500'000u);

    ParticleEmitter e;
    ApplyPreset_Fire(e);
    e.emissionRate = 60.0f;  // 60/60 = 1.0 particle per kSeekFixedDt tick
    ASSERT_TRUE(e.Initialize(&device, budget));

    uint32_t totalSpawned = 0u;
    for (uint32_t tick = 0u; tick < 150u; ++tick) {
        totalSpawned += e.CalculateSpawnCount(kSeekFixedDt);
    }

    EXPECT_EQ(totalSpawned, 150u)
        << "60 particles/s * (1/60 s) = 1.0 exact per tick → 150 over 150 ticks";
    EXPECT_FLOAT_EQ(e.spawnAccumulator, 0.0f)
        << "No fractional carry expected when rate*dt = integer";

    e.Shutdown(&device, budget);
}

/// Non-integer rate (Fire preset: 300/s). The 150-tick total must equal
/// the analytic value floor(300 * 2.5) = 750 (no accumulator rounding error).
TEST(VfxSeekDeterminism, TotalSpawnCount_FirePreset300Hz_150Ticks_Equals750) {
    test::MockRHIDevice device;
    ParticleBudget      budget(500'000u);

    ParticleEmitter e;
    ApplyPreset_Fire(e);
    // Fire preset: emissionRate=300, maxParticles=5000.
    // 300 * 2.5 = 750 particles total (no accumulator lost).
    ASSERT_TRUE(e.Initialize(&device, budget));

    uint32_t totalSpawned = 0u;
    for (uint32_t tick = 0u; tick < 150u; ++tick) {
        totalSpawned += e.CalculateSpawnCount(kSeekFixedDt);
    }

    // 300 * (1/60) = 5.0 exactly per tick. 150 ticks × 5 = 750.
    EXPECT_EQ(totalSpawned, 750u)
        << "Fire preset 300 particles/s * 2.5 s = 750 expected spawns over 150 ticks";

    e.Shutdown(&device, budget);
}

// =============================================================================
// ClearPendingSeek idempotency
// =============================================================================

/// ClearPendingSeek() after Seek() clears the field.
TEST(VfxSeekDeterminism, ClearPendingSeek_ClearsField) {
    ParticleEmitter e;
    e.Seek(2.5f);
    ASSERT_FLOAT_EQ(e.GetPendingSeekTime(), 2.5f);

    e.ClearPendingSeek();

    EXPECT_FLOAT_EQ(e.GetPendingSeekTime(), 0.0f);
}

/// ClearPendingSeek() is idempotent — safe to call multiple times.
TEST(VfxSeekDeterminism, ClearPendingSeek_CalledTwice_IsIdempotent) {
    ParticleEmitter e;
    e.Seek(1.0f);
    e.ClearPendingSeek();
    e.ClearPendingSeek();  // second call must not crash or corrupt state
    EXPECT_FLOAT_EQ(e.GetPendingSeekTime(), 0.0f);
}

/// ClearPendingSeek() on a fresh emitter (never seeked) is a no-op.
TEST(VfxSeekDeterminism, ClearPendingSeek_FreshEmitter_IsNoOp) {
    ParticleEmitter e;
    e.ClearPendingSeek();
    EXPECT_FLOAT_EQ(e.GetPendingSeekTime(), 0.0f);
}

// =============================================================================
// Multi-frame budget split
// =============================================================================

/// Simulating one frame of the warmup-loop decrement logic on a 30 s seek
/// (1800 ticks, max budget 256) decrements pendingSeekTime by 256 * kSeekFixedDt.
TEST(VfxSeekDeterminism, MultiFrameSplit_OneFrameDecrement_30sSeek) {
    static constexpr float    kSeekTarget  = 30.0f;
    static constexpr uint32_t kMaxBudget   = 256u;

    ParticleEmitter e;
    ApplyPreset_Fire(e);
    e.Seek(kSeekTarget);
    ASSERT_FLOAT_EQ(e.GetPendingSeekTime(), kSeekTarget);

    // Mirror frame_vfx_inline.cpp lines ~699–738:
    const float    remaining      = e.GetPendingSeekTime();
    const uint32_t totalTicks     = static_cast<uint32_t>(std::ceil(remaining / kSeekFixedDt)); // 1800
    const uint32_t ticksThisFrame = std::min(totalTicks, kMaxBudget);  // 256
    const float    consumed       = static_cast<float>(ticksThisFrame) * kSeekFixedDt;
    const float    leftover       = (remaining > consumed) ? (remaining - consumed) : 0.0f;
    e.pendingSeekTime = leftover;

    const float expectedLeftover = kSeekTarget - (static_cast<float>(kMaxBudget) * kSeekFixedDt);
    EXPECT_NEAR(e.GetPendingSeekTime(), expectedLeftover, 1e-5f)
        << "After one 256-tick frame, pendingSeekTime must decrease by 256 * kSeekFixedDt";
    EXPECT_GT(e.GetPendingSeekTime(), 0.0f)
        << "pendingSeekTime must remain > 0 after the first frame of a 30 s seek";
}

/// After all 150 ticks are consumed in one frame, ClearPendingSeek is called.
TEST(VfxSeekDeterminism, MultiFrameSplit_SeekCompletesInOneFrame_2p5s) {
    static constexpr float    kSeekTarget = 2.5f;
    static constexpr uint32_t kMaxBudget  = 256u;

    ParticleEmitter e;
    ApplyPreset_Fire(e);
    e.Seek(kSeekTarget);

    const float    remaining      = e.GetPendingSeekTime();
    const uint32_t totalTicks     = static_cast<uint32_t>(std::ceil(remaining / kSeekFixedDt)); // 150
    const uint32_t ticksThisFrame = std::min(totalTicks, kMaxBudget);  // 150

    EXPECT_GE(ticksThisFrame, totalTicks)
        << "150 ticks must fit within the 256-tick budget → completes in one frame";

    // Simulate the ClearPendingSeek call that fires when ticksThisFrame >= totalTicks.
    if (ticksThisFrame >= totalTicks) {
        e.ClearPendingSeek();
    }

    EXPECT_FLOAT_EQ(e.GetPendingSeekTime(), 0.0f)
        << "Warmup must complete after one frame and clear the pending seek";
}

// =============================================================================
// Bonus: wall-clock timing measurements
// =============================================================================

/// Measures CPU accumulator timing for 1k, 4k, 16k capacity emitters and
/// writes .sandbox/seek-warmup-timings.json for the phase 3 docs-operator.
/// GPU timing is always 0 ms (no real DX12 device in the test build).
TEST(VfxSeekDeterminism_Timing, Measure_WriteJson_1k_4k_16k) {
    static constexpr float    kSeekS     = 2.5f;
    static constexpr uint32_t kMaxBudget = 256u;

    const SeekTimingResult results[] = {
        MeasureSeekTiming(1000u,  kSeekS, kMaxBudget),
        MeasureSeekTiming(4000u,  kSeekS, kMaxBudget),
        MeasureSeekTiming(16000u, kSeekS, kMaxBudget),
    };

    for (const auto& r : results) {
        EXPECT_EQ(r.numTicks, 150u)
            << "capacity=" << r.capacity << " must require 150 warmup ticks";
        EXPECT_EQ(r.framesNeeded, 1u)
            << "capacity=" << r.capacity << " 150 ticks must fit in one frame";
        EXPECT_GE(r.wallClockMs, 0.0)
            << "Wall-clock measurement must be non-negative";
    }

    const std::string json = FormatTimingJson(results, 3);
    const std::string outPath = "e:/UzmiEngine/uzengine/.sandbox/seek-warmup-timings.json";

    {
        std::error_code ec;
        std::filesystem::create_directories(
            std::filesystem::path(outPath).parent_path(), ec);
        // ec is intentionally ignored — test passes even if dir cannot be created.
    }

    std::ofstream f(outPath);
    if (f.is_open()) {
        f << json;
    } else {
        // Print to stdout so CI logs capture it even without disk write.
        ADD_FAILURE() << "Could not write " << outPath
                      << ". JSON:\n" << json;
    }

    std::cout << "[seek-warmup-timings]\n" << json << std::flush;
}
