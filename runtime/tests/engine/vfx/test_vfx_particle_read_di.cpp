/// Unit tests for phase 7.12.0 - cross-emitter particle read DI CPU ref.
///
/// Covers:
///   - Unpopulated source returns misses (valid=false) with default values.
///   - RebuildIdIndex lets SampleByID resolve live IDs.
///   - Dead-slot sentinel (id=0) is never resolved.
///   - Slot recycle: rebuilding the index after a dead particle is
///     overwritten by a fresh ID resolves to the new particle.
///   - Explicit VfxParticleReadDefaults fall through on misses.
///   - Direct SampleBySlot returns valid=true for in-range indices
///     and valid=false (with defaults) for out-of-range.
///
/// @ref phase7_7.12.0-vfx-particle-read-di
/// @ref phase7_7.4.0-vfx-persistent-particle-ids (ID source)

#include <gtest/gtest.h>

#include "engine/vfx/data_interfaces/vfx_particle_read_di.h"

using namespace uz;

namespace {

/// Construct a simple source with three live particles.
VfxParticleReadSource MakeThreeParticleSource() {
    VfxParticleReadSource src;
    src.persistentIDs = {10u, 20u, 30u};
    src.positions = {
        Vec3{1.0f, 0.0f, 0.0f},
        Vec3{0.0f, 2.0f, 0.0f},
        Vec3{0.0f, 0.0f, 3.0f},
    };
    src.velocities = {
        Vec3{10.0f, 0.0f, 0.0f},
        Vec3{0.0f, 20.0f, 0.0f},
        Vec3{0.0f, 0.0f, 30.0f},
    };
    src.colors = {
        Vec4{1.0f, 0.0f, 0.0f, 1.0f},
        Vec4{0.0f, 1.0f, 0.0f, 1.0f},
        Vec4{0.0f, 0.0f, 1.0f, 1.0f},
    };
    src.sizes = {5.0f, 10.0f, 15.0f};
    src.ages  = {0.1f, 0.2f, 0.3f};
    return src;
}

} // namespace

// ─── Missing source / unindexed reads ─────────────────────────────────────

TEST(VfxParticleReadDI, ReadBeforeRebuildReturnsMissWithDefaults) {
    VfxParticleReadSource src = MakeThreeParticleSource();
    // idIndex is empty — SampleByID should treat every id as missing.
    VfxParticleReadDefaults defaults;
    defaults.size = 42.0f;
    const auto snap = src.SampleByID(10u, defaults);
    EXPECT_FALSE(snap.valid);
    EXPECT_FLOAT_EQ(snap.size, 42.0f);
}

TEST(VfxParticleReadDI, IdZeroAlwaysMisses) {
    VfxParticleReadSource src = MakeThreeParticleSource();
    src.RebuildIdIndex();
    const auto snap = src.SampleByID(0u);
    EXPECT_FALSE(snap.valid);
}

// ─── Successful reads ──────────────────────────────────────────────────────

TEST(VfxParticleReadDI, SampleByIDReturnsParticleAttributes) {
    VfxParticleReadSource src = MakeThreeParticleSource();
    src.RebuildIdIndex();
    const auto snap = src.SampleByID(20u);
    EXPECT_TRUE(snap.valid);
    EXPECT_FLOAT_EQ(snap.position.y, 2.0f);
    EXPECT_FLOAT_EQ(snap.velocity.y, 20.0f);
    EXPECT_FLOAT_EQ(snap.color.y, 1.0f);
    EXPECT_FLOAT_EQ(snap.size, 10.0f);
    EXPECT_FLOAT_EQ(snap.age, 0.2f);
}

TEST(VfxParticleReadDI, SampleBySlotIndependentOfIndexRebuild) {
    VfxParticleReadSource src = MakeThreeParticleSource();
    // Intentionally skip RebuildIdIndex — slot sampler doesn't need it.
    const auto snap = src.SampleBySlot(2);
    EXPECT_TRUE(snap.valid);
    EXPECT_FLOAT_EQ(snap.position.z, 3.0f);
}

TEST(VfxParticleReadDI, SampleBySlotOutOfRangeReturnsMiss) {
    VfxParticleReadSource src = MakeThreeParticleSource();
    const auto snap = src.SampleBySlot(999);
    EXPECT_FALSE(snap.valid);
}

// ─── Slot recycle safety ───────────────────────────────────────────────────

TEST(VfxParticleReadDI, SlotRecycleResolvesFreshIDAfterRebuild) {
    // Simulate: particle with id=20 dies (slot zeroed), new particle
    // spawns in the same slot with id=40. A stale index would still
    // resolve id=20 to the live particle; the sampler must not.
    VfxParticleReadSource src = MakeThreeParticleSource();
    src.RebuildIdIndex();

    // Kill slot 1 (id=20 -> 0), then spawn fresh id=40 in that slot.
    src.persistentIDs[1] = 40u;
    src.positions[1] = Vec3{9.9f, 9.9f, 9.9f};
    src.RebuildIdIndex();

    EXPECT_FALSE(src.SampleByID(20u).valid);            // old id gone
    const auto fresh = src.SampleByID(40u);
    EXPECT_TRUE(fresh.valid);
    EXPECT_FLOAT_EQ(fresh.position.x, 9.9f);
}

TEST(VfxParticleReadDI, DeadSlotSentinelNeverResolvesToFirstLiveSlot) {
    VfxParticleReadSource src;
    src.persistentIDs = {0u, 0u, 42u};
    src.positions = {Vec3{}, Vec3{}, Vec3{7.0f, 0.0f, 0.0f}};
    src.RebuildIdIndex();

    // A naive map would still contain {0 -> 0}; verify id=0 misses.
    EXPECT_FALSE(src.SampleByID(0u).valid);
    EXPECT_TRUE(src.SampleByID(42u).valid);
    EXPECT_FLOAT_EQ(src.SampleByID(42u).position.x, 7.0f);
}

// ─── Default propagation ───────────────────────────────────────────────────

TEST(VfxParticleReadDI, MissReturnsAllDefaultFields) {
    VfxParticleReadSource src;
    src.RebuildIdIndex();

    VfxParticleReadDefaults defaults;
    defaults.position = Vec3{1.0f, 2.0f, 3.0f};
    defaults.velocity = Vec3{10.0f, 20.0f, 30.0f};
    defaults.color    = Vec4{0.5f, 0.5f, 0.5f, 1.0f};
    defaults.size     = 99.0f;
    defaults.age      = 0.75f;

    const auto snap = src.SampleByID(999u, defaults);
    EXPECT_FALSE(snap.valid);
    EXPECT_FLOAT_EQ(snap.position.x, 1.0f);
    EXPECT_FLOAT_EQ(snap.velocity.z, 30.0f);
    EXPECT_FLOAT_EQ(snap.color.w, 1.0f);
    EXPECT_FLOAT_EQ(snap.size, 99.0f);
    EXPECT_FLOAT_EQ(snap.age, 0.75f);
}

// ─── Parallel-array safety ─────────────────────────────────────────────────

TEST(VfxParticleReadDI, PartialSoAArraysReadUsingDefaultsForGaps) {
    // Intentionally misaligned arrays — sizes[] shorter than positions[].
    // SafeAt should fall through to the caller-supplied default.
    VfxParticleReadSource src;
    src.persistentIDs = {5u, 6u, 7u};
    src.positions = {Vec3{1, 0, 0}, Vec3{0, 1, 0}, Vec3{0, 0, 1}};
    src.sizes     = {10.0f, 20.0f}; // only 2 entries — third slot is gap
    src.RebuildIdIndex();

    VfxParticleReadDefaults defaults;
    defaults.size = 77.0f;
    const auto snap = src.SampleByID(7u, defaults);
    EXPECT_TRUE(snap.valid);
    EXPECT_FLOAT_EQ(snap.size, 77.0f);  // gap => fall back to default
}
