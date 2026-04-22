/// Unit tests for phase 11.7.0: `ParticleEmitter::spawnRateCurve` behaviour.
///
/// Covers:
///   - Constant-rate regression: empty curve → `CalculateSpawnCount` uses
///     `emissionRate` exactly as before.
///   - Single-key curve: samples the single value regardless of time.
///   - Two-key linear interp: half-way time yields the midpoint rate.
///   - Upper-bound clamp: a curve value beyond `maxParticles/lifetime`
///     clamps so a runaway curve can't over-fill the buffer.
///   - Multiplier still applies on top of the curve-sampled base rate.
///   - JSON round-trip of the spawn rate curve through
///     `SerializeEmitter` / `DeserializeEmitter`.
///
/// `CalculateSpawnCount` needs `initialized == true` + `isEmitting == true`.
/// We force both flags without allocating GPU resources so the tests stay
/// CPU-only.

#include <gtest/gtest.h>

#include "engine/vfx/components/emitter_serializer.h"
#include "engine/vfx/components/particle_emitter.h"

using namespace uz;

namespace {

ParticleEmitter MakeTestEmitter() {
    ParticleEmitter e;
    e.emissionRate = 100.0f;
    e.maxParticles = 10000;
    e.lifetime     = 2.0f;
    // Bypass Initialize() — these tests cover the rate math, not GPU
    // allocation. CalculateSpawnCount only consults `initialized`,
    // `isEmitting`, `emissionRate`, `dt`, and (optionally) the curves.
    e.initialized = true;
    e.isEmitting  = true;
    return e;
}

} // namespace

// ─────────────────────────────────────────────────────────────────────────────
// Constant-rate regression
// ─────────────────────────────────────────────────────────────────────────────

TEST(SpawnRateCurve, EmptyCurve_UsesConstantRate) {
    ParticleEmitter e = MakeTestEmitter();
    e.emissionRate = 60.0f;
    // No keys → legacy path applies.
    EXPECT_EQ(e.spawnRateCurve.GetKeyCount(), 0u);

    // 1s at 60/s → 60 spawns.
    uint32_t total = 0;
    for (int i = 0; i < 60; ++i) {
        total += e.CalculateSpawnCount(1.0f / 60.0f);
    }
    EXPECT_EQ(total, 60u);
}

// ─────────────────────────────────────────────────────────────────────────────
// Single key
// ─────────────────────────────────────────────────────────────────────────────

TEST(SpawnRateCurve, SingleKey_OverridesConstantRate) {
    ParticleEmitter e = MakeTestEmitter();
    e.emissionRate = 60.0f;          // would normally apply
    e.spawnRateCurve.SetKeys({ {0.5f, 30.0f} });

    // Single key = constant 30/s regardless of sample time. 1s → 30 spawns.
    uint32_t total = 0;
    for (int i = 0; i < 60; ++i) {
        e.timeSinceRegistration = static_cast<float>(i) / 60.0f;
        total += e.CalculateSpawnCount(1.0f / 60.0f);
    }
    EXPECT_EQ(total, 30u);
}

// ─────────────────────────────────────────────────────────────────────────────
// Multi-key linear interpolation
// ─────────────────────────────────────────────────────────────────────────────

TEST(SpawnRateCurve, TwoKeys_LinearInterp_RampUp) {
    ParticleEmitter e = MakeTestEmitter();
    e.lifetime     = 100.0f;   // large so clamp doesn't bite
    e.maxParticles = 1000000;  // large so clamp doesn't bite
    // Ramp from 0 at t=0 to 200 at t=1. Sampled at the emitter's
    // absolute time, so we set `timeSinceRegistration` manually.
    e.spawnRateCurve.SetKeys({ {0.0f, 0.0f}, {1.0f, 200.0f} });
    e.spawnRateCurve.SetInterpolation(InterpolationMode::Linear);

    // At t=0.5 the rate should be ~100. Over 1 second we should spawn ~100
    // particles if we held the rate steady at 100 — but the rate itself
    // ramps, so instead we sample a single frame at t=0.5 with dt=1.0
    // (unrealistic but isolates the sampled rate).
    e.timeSinceRegistration = 0.5f;
    const uint32_t burst = e.CalculateSpawnCount(1.0f);
    EXPECT_EQ(burst, 100u);

    // Reset accumulator + sample at the curve's max.
    e.spawnAccumulator = 0.0f;
    e.timeSinceRegistration = 1.0f;
    const uint32_t maxBurst = e.CalculateSpawnCount(1.0f);
    EXPECT_EQ(maxBurst, 200u);
}

// ─────────────────────────────────────────────────────────────────────────────
// Upper-bound clamp
// ─────────────────────────────────────────────────────────────────────────────

TEST(SpawnRateCurve, UpperBoundClamp_ProtectsBuffer) {
    ParticleEmitter e = MakeTestEmitter();
    e.maxParticles = 100;
    e.lifetime     = 1.0f;
    // maxParticles / lifetime = 100 particles/sec upper bound.
    // Curve tries to demand 10000 → should clamp to 100.
    e.spawnRateCurve.SetKeys({ {0.0f, 10000.0f}, {1.0f, 10000.0f} });
    e.timeSinceRegistration = 0.0f;

    const uint32_t count = e.CalculateSpawnCount(1.0f);
    EXPECT_EQ(count, 100u);
}

// ─────────────────────────────────────────────────────────────────────────────
// Multiplier composition
// ─────────────────────────────────────────────────────────────────────────────

TEST(SpawnRateCurve, MultiplierStillAppliesOnTopOfCurve) {
    ParticleEmitter e = MakeTestEmitter();
    e.lifetime     = 1.0f;
    e.maxParticles = 1000000;  // avoid clamp
    e.spawnRateCurve.SetKeys({ {0.0f, 200.0f}, {1.0f, 200.0f} });
    // 4-point multiplier: everywhere 0.5× (but `SetKeys` manually).
    e.spawnRateMultiplier.keys[0] = {0.0f, 0.5f};
    e.spawnRateMultiplier.keys[1] = {0.33f, 0.5f};
    e.spawnRateMultiplier.keys[2] = {0.67f, 0.5f};
    e.spawnRateMultiplier.keys[3] = {1.0f, 0.5f};
    e.spawnRateMultiplier.enabled = true;

    e.timeSinceRegistration = 0.5f;
    const uint32_t count = e.CalculateSpawnCount(1.0f);
    // 200/s * 0.5 = 100/s over 1s = 100 spawns.
    EXPECT_EQ(count, 100u);
}

// ─────────────────────────────────────────────────────────────────────────────
// Not emitting short-circuits
// ─────────────────────────────────────────────────────────────────────────────

TEST(SpawnRateCurve, NotEmitting_ReturnsZero) {
    ParticleEmitter e = MakeTestEmitter();
    e.isEmitting = false;
    e.spawnRateCurve.SetKeys({ {0.0f, 500.0f}, {1.0f, 500.0f} });

    EXPECT_EQ(e.CalculateSpawnCount(1.0f), 0u);
}

TEST(SpawnRateCurve, ZeroRateFromCurve_ReturnsZero) {
    ParticleEmitter e = MakeTestEmitter();
    e.spawnRateCurve.SetKeys({ {0.0f, 0.0f}, {1.0f, 0.0f} });
    EXPECT_EQ(e.CalculateSpawnCount(1.0f), 0u);
}

// ─────────────────────────────────────────────────────────────────────────────
// Serializer round-trip
// ─────────────────────────────────────────────────────────────────────────────

TEST(SpawnRateCurve, Serializer_EmptyCurveOmitted) {
    ParticleEmitter e;
    const std::string out = SerializeEmitter(e);
    EXPECT_EQ(out.find("spawnRateCurve"), std::string::npos);
}

TEST(SpawnRateCurve, Serializer_PopulatedRoundTrip) {
    ParticleEmitter src;
    src.spawnRateCurve.SetKeys({
        {0.0f, 10.0f},
        {0.5f, 100.0f},
        {1.0f, 0.0f}
    });
    src.spawnRateCurve.SetInterpolation(InterpolationMode::Cubic);

    const std::string json = SerializeEmitter(src);

    ParticleEmitter dst;
    ASSERT_TRUE(DeserializeEmitter(json, dst));
    ASSERT_EQ(dst.spawnRateCurve.GetKeyCount(), 3u);
    const auto& keys = dst.spawnRateCurve.GetFloatKeys();
    EXPECT_FLOAT_EQ(keys[0].time,  0.0f);
    EXPECT_FLOAT_EQ(keys[0].value, 10.0f);
    EXPECT_FLOAT_EQ(keys[1].time,  0.5f);
    EXPECT_FLOAT_EQ(keys[1].value, 100.0f);
    EXPECT_FLOAT_EQ(keys[2].time,  1.0f);
    EXPECT_FLOAT_EQ(keys[2].value, 0.0f);
    EXPECT_EQ(dst.spawnRateCurve.GetInterpolation(), InterpolationMode::Cubic);
}

TEST(SpawnRateCurve, Serializer_TolerantToMalformedKeys) {
    const std::string json = R"({
        "spawnRateCurve": {
            "keys": [
                {"t": 0.0, "v": 5.0},
                "garbage",
                {"t": 1.0, "v": 15.0}
            ],
            "interp": "Linear"
        }
    })";
    ParticleEmitter e;
    ASSERT_TRUE(DeserializeEmitter(json, e));
    EXPECT_EQ(e.spawnRateCurve.GetKeyCount(), 2u);
    const auto& keys = e.spawnRateCurve.GetFloatKeys();
    EXPECT_FLOAT_EQ(keys[0].time,  0.0f);
    EXPECT_FLOAT_EQ(keys[1].time,  1.0f);
}
