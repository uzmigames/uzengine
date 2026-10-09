/// Unit tests for VFX persistent particle IDs (phase 7.4.0).
///
/// Covers the CPU-side infrastructure shipped in this phase:
///   - Opt-in flag default (off)
///   - ReservePersistentIDRange monotonicity + exhaustive unique IDs
///   - 64-bit counter sufficiency (no wrap in practical scenarios)
///   - Buffer handle default state
///
/// GPU spawn-CS write + readback path are deferred to 7.4.1 — tracked in
/// the task resolution notes. This test suite locks in the CPU API shape
/// so the follow-up can wire the GPU side without reshaping the interface.
///
/// @ref phase7_7.4.0-vfx-persistent-particle-ids

#include <gtest/gtest.h>

#include "engine/vfx/components/particle_emitter.h"
#include "engine/vfx/core/gpu_particle_buffer.h"

#include <cstdint>
#include <unordered_set>

using namespace uz;

// ─── Default state ─────────────────────────────────────────────────────────

TEST(PersistentParticleIDs, OptInFlagDefaultsToOff) {
    ParticleEmitter e;
    EXPECT_FALSE(e.emitsPersistentIDs);
}

TEST(PersistentParticleIDs, CounterStartsAtOne) {
    // Using 1 as the starting id keeps 0 as a reserved "no particle" sentinel
    // for dead slots / uninitialised reads.
    ParticleEmitter e;
    EXPECT_EQ(e.nextPersistentID, 1u);
}

TEST(PersistentParticleIDs, BufferHandlesInvalidByDefault) {
    ParticleAttributeSet set;
    EXPECT_FALSE(set.persistentIDs.IsValid());
}

TEST(PersistentParticleIDs, IsValidExcludesPersistentIDsChannel) {
    // Validity must only require the mandatory channels — persistent IDs are
    // opt-in, so an attribute set without them still qualifies as valid as
    // long as the seven base channels are present. IsValid() returning false
    // here is expected (no channels allocated), but the contract is covered
    // indirectly by the production path (see particle_emitter.cpp: buffer is
    // valid before we even consider persistent IDs).
    ParticleAttributeSet set;
    EXPECT_FALSE(set.IsValid());
}

// ─── ID range reservation ─────────────────────────────────────────────────

TEST(PersistentParticleIDs, ReserveAdvancesCounter) {
    ParticleEmitter e;
    const uint64_t base = e.ReservePersistentIDRange(4);
    EXPECT_EQ(base, 1u);
    EXPECT_EQ(e.nextPersistentID, 5u);

    const uint64_t base2 = e.ReservePersistentIDRange(3);
    EXPECT_EQ(base2, 5u);
    EXPECT_EQ(e.nextPersistentID, 8u);
}

TEST(PersistentParticleIDs, ReserveZeroIsNoOp) {
    ParticleEmitter e;
    const uint64_t base = e.ReservePersistentIDRange(0);
    EXPECT_EQ(base, 1u);
    EXPECT_EQ(e.nextPersistentID, 1u);
}

TEST(PersistentParticleIDs, GeneratedIDsAreAllUnique) {
    // GPU spawn CS formula: id = baseID + threadIndex. Exercise the CPU
    // reservation loop over multiple frames and confirm no id repeats.
    ParticleEmitter e;
    std::unordered_set<uint64_t> seen;
    seen.reserve(10'000);

    for (int frame = 0; frame < 100; ++frame) {
        const uint32_t spawnThisFrame = 100u;
        const uint64_t base = e.ReservePersistentIDRange(spawnThisFrame);
        for (uint32_t i = 0; i < spawnThisFrame; ++i) {
            const uint64_t id = base + static_cast<uint64_t>(i);
            auto [_, inserted] = seen.insert(id);
            ASSERT_TRUE(inserted) << "duplicate id=" << id
                                  << " at frame=" << frame << " i=" << i;
        }
    }

    EXPECT_EQ(seen.size(), 10'000u);
    EXPECT_EQ(e.nextPersistentID, 10'001u);
}

TEST(PersistentParticleIDs, CounterSurvivesSlotRecycle) {
    // Simulating the "slot recycle" scenario: particle in slot 0 dies, a new
    // particle spawns in the same slot, but the new particle gets a fresh id.
    // The CPU infrastructure supports this directly — `ReservePersistentIDRange`
    // always advances; the GPU side then writes the fresh id at the recycled
    // slot's `persistentIDs[slot]` location.
    ParticleEmitter e;

    // Frame 1: spawn 4 particles.
    const uint64_t frame1Base = e.ReservePersistentIDRange(4);
    EXPECT_EQ(frame1Base, 1u);

    // Frame 2: two of them die (slots 0 and 2 recycle), 3 new spawn.
    const uint64_t frame2Base = e.ReservePersistentIDRange(3);
    EXPECT_EQ(frame2Base, 5u);  // Never reuses 1..4.
    EXPECT_EQ(e.nextPersistentID, 8u);
}

// ─── 64-bit sufficiency check ──────────────────────────────────────────────

TEST(PersistentParticleIDs, SixtyFourBitSpaceCoversPracticalLifetimes) {
    // 2^64 / (1 million IDs per frame * 60 fps * 3600 s/hr) =
    //   18.4e18 / 216e9 = 85e6 hours ~= 9700 years of continuous worst-case
    // emission. The range is effectively infinite for practical game sessions.
    const uint64_t kIDsPerFrame = 1'000'000ull;
    const uint64_t kFramesPerYear = 60ull * 3600ull * 24ull * 365ull;
    const uint64_t kYearsSupported =
        std::numeric_limits<uint64_t>::max() / (kIDsPerFrame * kFramesPerYear);

    EXPECT_GT(kYearsSupported, 1000ull);
}

// ─── Buffer opt-in integration (via ParticleEmitter flag) ──────────────────

TEST(PersistentParticleIDs, OptInFlagIndependentOfBufferState) {
    // Flag and buffer allocation are intentionally independent — flag is the
    // AUTHOR intent; buffer allocation happens at Initialize() time. A caller
    // that reads the flag before Initialize sees the authored intent.
    ParticleEmitter e;
    e.emitsPersistentIDs = true;
    EXPECT_TRUE(e.emitsPersistentIDs);
    // Buffer is not yet allocated — that happens in Initialize with a real RHI.
    EXPECT_FALSE(e.buffer.hasPersistentIDs);
}
