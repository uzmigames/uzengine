/// Unit tests for phase 7.5.0 - stateless emitters closed-form motion.
///
/// Locks the CPU reference implementation that the HLSL VS port (deferred
/// to 7.5.1) must match byte-for-byte. Without these tests a subtle
/// math divergence between CPU and GPU would only surface as visual bugs.
///
/// Covers:
///   - Emitter mode enum default (Stateful) + HLSL uint convention.
///   - Zero-damping ballistic position + velocity formulas.
///   - Exponential damping convergence (asymptotic position, velocity decay).
///   - Gravity contribution at `t > 0`.
///   - Negative-t clamp (particle not yet born).
///   - Normalised age math including lifetime = 0 fallback.
///   - Combined realistic trajectory sanity check.
///
/// @ref phase7_7.5.0-vfx-stateless-emitters

#include <gtest/gtest.h>

#include "engine/vfx/components/particle_emitter.h"
#include "engine/vfx/core/vfx_stateless_motion.h"

#include <cmath>

using namespace uz;

// ─── Emitter mode enum ─────────────────────────────────────────────────────

TEST(VfxStatelessEmitter, DefaultsToStateful) {
    ParticleEmitter e;
    EXPECT_EQ(e.emitterMode, 0u);
    EXPECT_EQ(static_cast<VfxEmitterMode>(e.emitterMode), VfxEmitterMode::Stateful);
}

TEST(VfxStatelessEmitter, EnumMatchesHLSLUintConvention) {
    EXPECT_EQ(static_cast<uint8_t>(VfxEmitterMode::Stateful),  0u);
    EXPECT_EQ(static_cast<uint8_t>(VfxEmitterMode::Stateless), 1u);
}

TEST(VfxStatelessEmitter, ModeToString) {
    EXPECT_STREQ(VfxEmitterModeToString(VfxEmitterMode::Stateful),  "Stateful");
    EXPECT_STREQ(VfxEmitterModeToString(VfxEmitterMode::Stateless), "Stateless");
}

// ─── Zero-damping ballistic motion ─────────────────────────────────────────

TEST(VfxStatelessMotion, ZeroDampingBallisticPosition) {
    VfxStatelessParticle p;
    p.spawnPosition = {0.0f, 0.0f, 0.0f};
    p.spawnVelocity = {100.0f, 0.0f, 0.0f};
    p.gravity       = {0.0f, 0.0f, 0.0f};
    p.damping       = 0.0f;

    const Vec3 pos = VfxStatelessEvaluatePosition(p, 2.0f);
    EXPECT_NEAR(pos.x, 200.0f, 1e-3f);
    EXPECT_NEAR(pos.y,   0.0f, 1e-3f);
    EXPECT_NEAR(pos.z,   0.0f, 1e-3f);
}

TEST(VfxStatelessMotion, ZeroDampingPreservesInitialVelocity) {
    VfxStatelessParticle p;
    p.spawnVelocity = {50.0f, 30.0f, 20.0f};
    p.damping       = 0.0f;
    for (float t : {0.1f, 0.5f, 1.0f, 5.0f}) {
        const Vec3 v = VfxStatelessEvaluateVelocity(p, t);
        EXPECT_NEAR(v.x, 50.0f, 1e-3f);
        EXPECT_NEAR(v.y, 30.0f, 1e-3f);
        EXPECT_NEAR(v.z, 20.0f, 1e-3f);
    }
}

TEST(VfxStatelessMotion, GravityContributesAtTSquared) {
    // Pure gravity drop: p(t) = 0.5 * g * t^2.
    VfxStatelessParticle p;
    p.gravity = {0.0f, 0.0f, -980.0f};
    const Vec3 pos = VfxStatelessEvaluatePosition(p, 1.0f);
    EXPECT_NEAR(pos.z, -490.0f, 1e-2f);

    // Velocity term: g * t.
    const Vec3 vel = VfxStatelessEvaluateVelocity(p, 1.0f);
    EXPECT_NEAR(vel.z, -980.0f, 1e-2f);
}

// ─── Damping ───────────────────────────────────────────────────────────────

TEST(VfxStatelessMotion, DampingDecaysVelocityExponentially) {
    VfxStatelessParticle p;
    p.spawnVelocity = {100.0f, 0.0f, 0.0f};
    p.damping       = 2.0f;

    const Vec3 v0 = VfxStatelessEvaluateVelocity(p, 0.0f);
    const Vec3 v1 = VfxStatelessEvaluateVelocity(p, 1.0f);
    const Vec3 v2 = VfxStatelessEvaluateVelocity(p, 2.0f);

    // Ratios must follow exp(-k*t).
    EXPECT_NEAR(v0.x, 100.0f, 1e-3f);
    EXPECT_NEAR(v1.x, 100.0f * std::exp(-2.0f), 1e-3f);
    EXPECT_NEAR(v2.x, 100.0f * std::exp(-4.0f), 1e-3f);
}

TEST(VfxStatelessMotion, DampingPositionConvergesToAsymptote) {
    // With damping k and no gravity, position asymptotes to p0 + v0 / k.
    VfxStatelessParticle p;
    p.spawnPosition = {0.0f, 0.0f, 0.0f};
    p.spawnVelocity = {100.0f, 0.0f, 0.0f};
    p.damping       = 5.0f;
    const float asymptoteX = 100.0f / 5.0f;     // = 20

    const Vec3 farFuture = VfxStatelessEvaluatePosition(p, 1000.0f);
    EXPECT_NEAR(farFuture.x, asymptoteX, 1e-2f);
}

// ─── Edge cases ────────────────────────────────────────────────────────────

TEST(VfxStatelessMotion, NegativeTClampsToZero) {
    VfxStatelessParticle p;
    p.spawnPosition = {10.0f, 20.0f, 30.0f};
    p.spawnVelocity = {50.0f, 0.0f, 0.0f};

    const Vec3 pos = VfxStatelessEvaluatePosition(p, -0.5f);
    EXPECT_FLOAT_EQ(pos.x, 10.0f);
    EXPECT_FLOAT_EQ(pos.y, 20.0f);
    EXPECT_FLOAT_EQ(pos.z, 30.0f);
}

TEST(VfxStatelessMotion, NormalizedAgeProgressesLinearly) {
    VfxStatelessParticle p;
    p.lifetime = 2.0f;
    EXPECT_FLOAT_EQ(VfxStatelessNormalizedAge(p, 0.0f), 0.0f);
    EXPECT_FLOAT_EQ(VfxStatelessNormalizedAge(p, 1.0f), 0.5f);
    EXPECT_FLOAT_EQ(VfxStatelessNormalizedAge(p, 2.0f), 1.0f);
    // Overflow tolerated; shaders discard when > 1.
    EXPECT_FLOAT_EQ(VfxStatelessNormalizedAge(p, 3.0f), 1.5f);
}

TEST(VfxStatelessMotion, NormalizedAgeZeroLifetimeReturnsOne) {
    VfxStatelessParticle p;
    p.lifetime = 0.0f;
    EXPECT_FLOAT_EQ(VfxStatelessNormalizedAge(p, 0.5f), 1.0f);
}

// ─── Integrated trajectory sanity ──────────────────────────────────────────

TEST(VfxStatelessMotion, RealisticMuzzleFlashAt100ms) {
    // Muzzle flash: 10 m/s along +X, no gravity, mild damping, 0.1s life.
    VfxStatelessParticle p;
    p.spawnVelocity = {1000.0f, 0.0f, 0.0f};
    p.damping       = 8.0f;
    p.lifetime      = 0.1f;

    const Vec3 atEnd = VfxStatelessEvaluatePosition(p, 0.1f);
    // expected: (1000 / 8) * (1 - exp(-0.8))  = 125 * 0.5507 = ~68.8 cm.
    const float expectedX = (1000.0f / 8.0f) * (1.0f - std::exp(-0.8f));
    EXPECT_NEAR(atEnd.x, expectedX, 1e-2f);
}
