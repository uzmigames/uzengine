/// Unit tests for phase 7.20.0 - velocity inheritance + spawn-rate curves.
///
/// Covers the CPU-side foundation shipped in this phase:
///   - Default-off `velocityInheritance` on ParticleEmitter.
///   - `UpdateFrameVelocity` priming (first call produces 0 velocity).
///   - `UpdateFrameVelocity` derivative accuracy.
///   - Zero/negative dt rejected without clobbering previous velocity.
///   - Spawn-rate ramp defaults (disabled -> 1.0 passthrough).
///   - Ramp evaluation: clamping, piecewise linear interpolation, step keys.
///   - `SetFadeInOut` helper shape.
///
/// GPU spawn CS integration is deferred to the follow-up; these tests lock
/// in the CPU contract.
///
/// @ref phase7_7.20.0-vfx-velocity-inheritance-spawn-curves

#include <gtest/gtest.h>

#include "engine/vfx/components/particle_emitter.h"
#include "engine/vfx/core/vfx_spawn_rate_curve.h"

using namespace uz;

// ─── ParticleEmitter defaults ──────────────────────────────────────────────

TEST(VfxVelocityInheritance, DefaultsToZero) {
    ParticleEmitter e;
    EXPECT_FLOAT_EQ(e.velocityInheritance, 0.0f);
    EXPECT_FLOAT_EQ(e.emitterFrameVelocity.x, 0.0f);
    EXPECT_FLOAT_EQ(e.emitterFrameVelocity.y, 0.0f);
    EXPECT_FLOAT_EQ(e.emitterFrameVelocity.z, 0.0f);
    EXPECT_FALSE(e.emitterFrameVelocityPrimed);
}

// ─── UpdateFrameVelocity ───────────────────────────────────────────────────

TEST(VfxVelocityInheritance, FirstCallPrimesWithoutProducingVelocity) {
    ParticleEmitter e;
    e.UpdateFrameVelocity({100.0f, 0.0f, 0.0f}, 1.0f / 60.0f);
    EXPECT_TRUE(e.emitterFrameVelocityPrimed);
    EXPECT_FLOAT_EQ(e.emitterFrameVelocity.x, 0.0f);
    EXPECT_FLOAT_EQ(e.lastEmitterPosition.x, 100.0f);
}

TEST(VfxVelocityInheritance, SubsequentCallsComputeDelta) {
    ParticleEmitter e;
    e.UpdateFrameVelocity({0.0f, 0.0f, 0.0f}, 1.0f / 60.0f);   // prime
    e.UpdateFrameVelocity({1.0f, 0.0f, 0.0f}, 1.0f / 60.0f);   // 1 cm in 1/60 s -> 60 cm/s
    EXPECT_NEAR(e.emitterFrameVelocity.x, 60.0f, 1e-3f);
}

TEST(VfxVelocityInheritance, ZeroOrNegativeDtKeepsPreviousVelocity) {
    ParticleEmitter e;
    e.UpdateFrameVelocity({0.0f, 0.0f, 0.0f}, 1.0f / 60.0f);
    e.UpdateFrameVelocity({1.0f, 0.0f, 0.0f}, 1.0f / 60.0f);
    const Vec3 prev = e.emitterFrameVelocity;

    e.UpdateFrameVelocity({100.0f, 0.0f, 0.0f}, 0.0f);
    EXPECT_FLOAT_EQ(e.emitterFrameVelocity.x, prev.x);

    e.UpdateFrameVelocity({200.0f, 0.0f, 0.0f}, -0.01f);
    EXPECT_FLOAT_EQ(e.emitterFrameVelocity.x, prev.x);
}

TEST(VfxVelocityInheritance, TracksAcrossFrames) {
    ParticleEmitter e;
    const float dt = 1.0f / 60.0f;
    e.UpdateFrameVelocity({0.0f, 0.0f, 0.0f}, dt);
    // Move +1 cm each frame for 10 frames; expect stable 60 cm/s.
    for (int i = 1; i <= 10; ++i) {
        e.UpdateFrameVelocity({static_cast<float>(i), 0.0f, 0.0f}, dt);
    }
    EXPECT_NEAR(e.emitterFrameVelocity.x, 60.0f, 1e-3f);
}

// ─── VfxSpawnRateCurve defaults ────────────────────────────────────────────

TEST(VfxSpawnRateCurve, DefaultDisabledPassesThrough) {
    VfxSpawnRateCurve ramp;
    EXPECT_FALSE(ramp.enabled);
    for (float t : {0.0f, 0.25f, 0.5f, 1.0f, 1.5f, -0.5f}) {
        EXPECT_FLOAT_EQ(ramp.Evaluate(t), 1.0f) << "t=" << t;
    }
}

TEST(VfxSpawnRateCurve, EnabledLinearInterpolatesBetweenKeys) {
    VfxSpawnRateCurve ramp;
    ramp.keys[0] = {0.0f, 0.0f};
    ramp.keys[1] = {0.5f, 1.0f};
    ramp.keys[2] = {0.5f, 1.0f};
    ramp.keys[3] = {1.0f, 0.0f};
    ramp.enabled = true;
    EXPECT_FLOAT_EQ(ramp.Evaluate(0.0f), 0.0f);
    EXPECT_NEAR(ramp.Evaluate(0.25f), 0.5f, 1e-4f);
    EXPECT_FLOAT_EQ(ramp.Evaluate(0.5f),  1.0f);
    EXPECT_NEAR(ramp.Evaluate(0.75f), 0.5f, 1e-4f);
    EXPECT_FLOAT_EQ(ramp.Evaluate(1.0f),  0.0f);
}

TEST(VfxSpawnRateCurve, ClampsOutOfRangeInputs) {
    VfxSpawnRateCurve ramp;
    ramp.keys[0] = {0.1f, 0.2f};
    ramp.keys[1] = {0.3f, 0.5f};
    ramp.keys[2] = {0.7f, 0.5f};
    ramp.keys[3] = {0.9f, 0.2f};
    ramp.enabled = true;
    EXPECT_FLOAT_EQ(ramp.Evaluate(-1.0f), 0.2f);  // clamped to first key
    EXPECT_FLOAT_EQ(ramp.Evaluate(2.0f),  0.2f);  // clamped to last key
}

TEST(VfxSpawnRateCurve, SetFadeInOutProducesExpectedShape) {
    VfxSpawnRateCurve ramp;
    ramp.SetFadeInOut(0.2f, 0.8f);
    EXPECT_TRUE(ramp.enabled);
    EXPECT_FLOAT_EQ(ramp.Evaluate(0.0f), 0.0f);
    EXPECT_FLOAT_EQ(ramp.Evaluate(0.2f), 1.0f);
    EXPECT_FLOAT_EQ(ramp.Evaluate(0.5f), 1.0f);   // plateau
    EXPECT_FLOAT_EQ(ramp.Evaluate(0.8f), 1.0f);
    EXPECT_FLOAT_EQ(ramp.Evaluate(1.0f), 0.0f);
}

TEST(VfxSpawnRateCurve, SetFadeInOutClampsBounds) {
    VfxSpawnRateCurve ramp;
    ramp.SetFadeInOut(-0.5f, 1.5f);
    // Clamped to [0, 1] and [0, 1]; curve remains valid.
    EXPECT_GE(ramp.keys[1].time, 0.0f);
    EXPECT_LE(ramp.keys[2].time, 1.0f);
}
