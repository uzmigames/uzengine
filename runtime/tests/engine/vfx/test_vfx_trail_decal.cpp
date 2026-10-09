/// Unit tests for phase 8.13.0 — VFX trail-decal emitter.
///
/// Validates the cadence predicate, per-particle state machine, emission
/// orchestration, and determinism guarantee.
///
/// @ref engine/vfx/decals/vfx_trail_decal.h
/// @ref docs/engine/25-vfx/30-trail-decal-streaks.md

#include <gtest/gtest.h>

#include "engine/vfx/decals/vfx_trail_decal.h"

using namespace uz;

namespace {

VfxTrailDecalConfig MakeConfig(VfxTrailDecalCadence cadence, float value) {
    VfxTrailDecalConfig cfg;
    cfg.sourceEmitter = "src";
    cfg.decalMaterial = "materials/tire.uzmat";
    cfg.cadence       = cadence;
    cfg.cadenceValue  = value;
    cfg.width         = 0.5f;
    cfg.length        = 0.5f;
    cfg.lifetime      = 10.0f;
    return cfg;
}

} // namespace

// ─────────────────────────────────────────────────────────────────────────────
// VfxShouldEmitTrailDecal — cadence predicate
// ─────────────────────────────────────────────────────────────────────────────

TEST(VfxTrailDecalPredicate, UnprimedStateNeverEmits) {
    auto cfg = MakeConfig(VfxTrailDecalCadence::Distance, 1.0f);
    VfxTrailDecalParticleState state;
    EXPECT_FALSE(VfxShouldEmitTrailDecal(cfg, state, 0.0f));
}

TEST(VfxTrailDecalPredicate, ZeroCadenceValueNeverEmits) {
    auto cfg = MakeConfig(VfxTrailDecalCadence::Distance, 0.0f);
    VfxTrailDecalParticleState state;
    state.primed = true;
    state.accumulatedDistance = 999.0f;
    EXPECT_FALSE(VfxShouldEmitTrailDecal(cfg, state, 0.0f));
}

TEST(VfxTrailDecalPredicate, DistanceMode_EmitsOnceThresholdMet) {
    auto cfg = MakeConfig(VfxTrailDecalCadence::Distance, 1.0f);
    VfxTrailDecalParticleState state;
    state.primed = true;
    state.accumulatedDistance = 0.5f;
    EXPECT_FALSE(VfxShouldEmitTrailDecal(cfg, state, 0.0f));
    state.accumulatedDistance = 1.0f;
    EXPECT_TRUE(VfxShouldEmitTrailDecal(cfg, state, 0.0f));
    state.accumulatedDistance = 2.5f;
    EXPECT_TRUE(VfxShouldEmitTrailDecal(cfg, state, 0.0f));
}

TEST(VfxTrailDecalPredicate, TimeMode_UsesDeltaFromLastEmit) {
    auto cfg = MakeConfig(VfxTrailDecalCadence::Time, 0.1f);
    VfxTrailDecalParticleState state;
    state.primed = true;
    state.lastEmitTime = 1.0f;
    EXPECT_FALSE(VfxShouldEmitTrailDecal(cfg, state, 1.05f));
    EXPECT_TRUE(VfxShouldEmitTrailDecal(cfg, state, 1.10f));
    EXPECT_TRUE(VfxShouldEmitTrailDecal(cfg, state, 2.00f));
}

// ─────────────────────────────────────────────────────────────────────────────
// VfxTrailDecalEmitter — first sample primes, no emission
// ─────────────────────────────────────────────────────────────────────────────

TEST(VfxTrailDecalEmitter, FirstSample_PrimesAndSkips) {
    VfxTrailDecalEmitter em(MakeConfig(VfxTrailDecalCadence::Distance, 1.0f));
    VfxTrailDecalEvent ev;
    EXPECT_FALSE(em.TryEmit(1, Vec3{0,0,0}, 0.0f, ev));
    EXPECT_EQ(em.TrackedParticleCount(), 1u);
}

// ─────────────────────────────────────────────────────────────────────────────
// Distance cadence emission
// ─────────────────────────────────────────────────────────────────────────────

TEST(VfxTrailDecalEmitter, Distance_EmitsEveryCadenceMetres) {
    VfxTrailDecalEmitter em(MakeConfig(VfxTrailDecalCadence::Distance, 1.0f));
    VfxTrailDecalEvent ev;

    // Prime at origin.
    em.TryEmit(1, Vec3{0,0,0}, 0.0f, ev);
    // Travel 0.5m — no emit yet.
    EXPECT_FALSE(em.TryEmit(1, Vec3{0.5f, 0, 0}, 0.0f, ev));
    // Travel 0.5m more → accumulated 1.0m → emit.
    EXPECT_TRUE(em.TryEmit(1, Vec3{1.0f, 0, 0}, 0.0f, ev));
    // Event center = midpoint of (0,0,0) and (1,0,0).
    EXPECT_FLOAT_EQ(ev.centerPos.x, 0.5f);
    EXPECT_FLOAT_EQ(ev.forward.x, 1.0f);
    // After emit, accumulator resets — travel 0.5m should NOT emit.
    EXPECT_FALSE(em.TryEmit(1, Vec3{1.5f, 0, 0}, 0.0f, ev));
    // Another 0.5m → second emit.
    EXPECT_TRUE(em.TryEmit(1, Vec3{2.0f, 0, 0}, 0.0f, ev));
    EXPECT_FLOAT_EQ(ev.centerPos.x, 1.5f);
}

// ─────────────────────────────────────────────────────────────────────────────
// Time cadence emission
// ─────────────────────────────────────────────────────────────────────────────

TEST(VfxTrailDecalEmitter, Time_EmitsOnIntervalRegardlessOfMotion) {
    VfxTrailDecalEmitter em(MakeConfig(VfxTrailDecalCadence::Time, 1.0f));
    VfxTrailDecalEvent ev;
    em.TryEmit(1, Vec3{0,0,0}, 0.0f, ev);            // prime
    EXPECT_FALSE(em.TryEmit(1, Vec3{0.1f,0,0}, 0.5f, ev));
    EXPECT_TRUE (em.TryEmit(1, Vec3{0.2f,0,0}, 1.0f, ev));
    EXPECT_FALSE(em.TryEmit(1, Vec3{0.3f,0,0}, 1.5f, ev));
    EXPECT_TRUE (em.TryEmit(1, Vec3{0.4f,0,0}, 2.0f, ev));
}

// ─────────────────────────────────────────────────────────────────────────────
// Event orientation
// ─────────────────────────────────────────────────────────────────────────────

TEST(VfxTrailDecalEmitter, Event_ForwardMatchesPathDirection) {
    VfxTrailDecalEmitter em(MakeConfig(VfxTrailDecalCadence::Distance, 1.0f));
    VfxTrailDecalEvent ev;
    em.TryEmit(7, Vec3{0,0,0}, 0.0f, ev);
    // Move along Y axis.
    ASSERT_TRUE(em.TryEmit(7, Vec3{0, 1.0f, 0}, 0.0f, ev));
    EXPECT_FLOAT_EQ(ev.forward.x, 0.0f);
    EXPECT_FLOAT_EQ(ev.forward.y, 1.0f);
    EXPECT_FLOAT_EQ(ev.forward.z, 0.0f);
}

// ─────────────────────────────────────────────────────────────────────────────
// Particle isolation
// ─────────────────────────────────────────────────────────────────────────────

TEST(VfxTrailDecalEmitter, DifferentParticlesTrackedIndependently) {
    VfxTrailDecalEmitter em(MakeConfig(VfxTrailDecalCadence::Distance, 1.0f));
    VfxTrailDecalEvent ev;
    em.TryEmit(1, Vec3{0,0,0}, 0.0f, ev);
    em.TryEmit(2, Vec3{100,0,0}, 0.0f, ev);
    EXPECT_EQ(em.TrackedParticleCount(), 2u);
    // Move particle 1 only.
    EXPECT_TRUE(em.TryEmit(1, Vec3{1.0f,0,0}, 0.0f, ev));
    EXPECT_FLOAT_EQ(ev.centerPos.x, 0.5f);
    // Particle 2 still at its spawn — no emit.
    EXPECT_FALSE(em.TryEmit(2, Vec3{100,0,0}, 0.0f, ev));
}

// ─────────────────────────────────────────────────────────────────────────────
// ForgetParticle / Reset
// ─────────────────────────────────────────────────────────────────────────────

TEST(VfxTrailDecalEmitter, ForgetParticleDropsState) {
    VfxTrailDecalEmitter em(MakeConfig(VfxTrailDecalCadence::Distance, 1.0f));
    VfxTrailDecalEvent ev;
    em.TryEmit(42, Vec3{0,0,0}, 0.0f, ev);
    em.ForgetParticle(42);
    EXPECT_EQ(em.TrackedParticleCount(), 0u);
}

TEST(VfxTrailDecalEmitter, Reset_ClearsAll) {
    VfxTrailDecalEmitter em(MakeConfig(VfxTrailDecalCadence::Distance, 1.0f));
    VfxTrailDecalEvent ev;
    em.TryEmit(1, Vec3{0,0,0}, 0.0f, ev);
    em.TryEmit(2, Vec3{0,0,0}, 0.0f, ev);
    em.Reset();
    EXPECT_EQ(em.TrackedParticleCount(), 0u);
}

// ─────────────────────────────────────────────────────────────────────────────
// Event payload carries config
// ─────────────────────────────────────────────────────────────────────────────

TEST(VfxTrailDecalEmitter, EventCopiesConfigMaterialAndDimensions) {
    auto cfg = MakeConfig(VfxTrailDecalCadence::Distance, 1.0f);
    cfg.decalMaterial = "materials/blood.uzmat";
    cfg.width = 0.75f;
    cfg.length = 1.25f;
    cfg.lifetime = 5.0f;
    VfxTrailDecalEmitter em(cfg);
    VfxTrailDecalEvent ev;
    em.TryEmit(1, Vec3{0,0,0}, 0.0f, ev);
    ASSERT_TRUE(em.TryEmit(1, Vec3{1.0f,0,0}, 0.0f, ev));
    EXPECT_EQ(ev.material, "materials/blood.uzmat");
    EXPECT_FLOAT_EQ(ev.width,    0.75f);
    EXPECT_FLOAT_EQ(ev.length,   1.25f);
    EXPECT_FLOAT_EQ(ev.lifetime, 5.0f);
}

// ─────────────────────────────────────────────────────────────────────────────
// Determinism — same input → identical decal stream
// ─────────────────────────────────────────────────────────────────────────────

TEST(VfxTrailDecalEmitter, IdenticalInputsProduceIdenticalStreams) {
    VfxTrailDecalEmitter a(MakeConfig(VfxTrailDecalCadence::Distance, 0.5f));
    VfxTrailDecalEmitter b(MakeConfig(VfxTrailDecalCadence::Distance, 0.5f));

    const Vec3 path[] = {
        {0,0,0}, {0.25f,0,0}, {0.5f,0,0}, {0.75f,0,0},
        {1.0f,0,0}, {1.5f,0,0}, {2.0f,0,0}
    };
    uint32_t emitCountA = 0, emitCountB = 0;
    for (const auto& p : path) {
        VfxTrailDecalEvent evA, evB;
        const bool hA = a.TryEmit(9, p, 0.0f, evA);
        const bool hB = b.TryEmit(9, p, 0.0f, evB);
        EXPECT_EQ(hA, hB);
        if (hA && hB) {
            ++emitCountA; ++emitCountB;
            EXPECT_FLOAT_EQ(evA.centerPos.x, evB.centerPos.x);
        }
    }
    EXPECT_EQ(emitCountA, emitCountB);
    EXPECT_GT(emitCountA, 0u); // at least one emission along this path
}
