/// Unit tests for phase 7.19.0 - .uzvfxtype asset + LRU tracker.
///
/// Covers:
///   - JSON round-trip: required format tag, baseline fields, platform overrides.
///   - Malformed JSON / unknown format rejected.
///   - Resolve() baseline vs platform override merge (unset -1 sentinels).
///   - LRU tracker admits until cap, evicts lowest-priority + oldest.
///   - Release removes entries by user handle.
///   - Zero cap = no limit.
///   - Rejection when every live instance outranks the newcomer.
///
/// @ref phase7_7.19.0-vfx-effect-type-scalability-asset

#include <gtest/gtest.h>

#include "engine/vfx/budget/vfx_effect_type.h"

#include <memory>
#include <string>

using namespace uz;

// ─── Asset load/save round-trip ─────────────────────────────────────────────

TEST(VfxEffectTypeAsset, LoadsBaselineFields) {
    const char* json = R"({
        "format": "uzvfxtype",
        "version": 1,
        "name": "Weapon",
        "max_concurrent_instances": 16,
        "cull_distance_multiplier": 0.75,
        "lod_bias": 1,
        "priority": 2
    })";
    auto asset = VfxEffectType::LoadFromString(json);
    ASSERT_NE(asset, nullptr);
    EXPECT_EQ(asset->name, "Weapon");
    EXPECT_EQ(asset->maxConcurrentInstances, 16u);
    EXPECT_FLOAT_EQ(asset->cullDistanceMultiplier, 0.75f);
    EXPECT_EQ(asset->lodBias, 1);
    EXPECT_EQ(asset->priority, 2);
}

TEST(VfxEffectTypeAsset, ParsesPlatformOverrides) {
    const char* json = R"({
        "format": "uzvfxtype",
        "version": 1,
        "name": "Weapon",
        "max_concurrent_instances": 16,
        "platform_overrides": {
            "mobile":  { "max_concurrent_instances": 4, "lod_bias": 2 },
            "console": { "max_concurrent_instances": 12 }
        }
    })";
    auto asset = VfxEffectType::LoadFromString(json);
    ASSERT_NE(asset, nullptr);
    ASSERT_EQ(asset->platformOverrides.size(), 2u);
    EXPECT_EQ(asset->platformOverrides["mobile"].maxConcurrentInstances, 4);
    EXPECT_EQ(asset->platformOverrides["mobile"].lodBias, 2);
    EXPECT_EQ(asset->platformOverrides["console"].maxConcurrentInstances, 12);
}

TEST(VfxEffectTypeAsset, RejectsBadFormatTag) {
    EXPECT_EQ(VfxEffectType::LoadFromString(R"({"format":"wrong"})"), nullptr);
    EXPECT_EQ(VfxEffectType::LoadFromString("garbage"), nullptr);
}

TEST(VfxEffectTypeAsset, DefaultsForMissingFields) {
    auto asset = VfxEffectType::LoadFromString(R"({"format":"uzvfxtype"})");
    ASSERT_NE(asset, nullptr);
    EXPECT_EQ(asset->maxConcurrentInstances, 0u);
    EXPECT_FLOAT_EQ(asset->cullDistanceMultiplier, 1.0f);
    EXPECT_EQ(asset->lodBias, 0);
    EXPECT_EQ(asset->priority, 0);
    EXPECT_TRUE(asset->platformOverrides.empty());
}

// ─── Resolve (baseline + platform merge) ───────────────────────────────────

TEST(VfxEffectTypeResolve, BaselineWhenPlatformEmpty) {
    VfxEffectType t;
    t.maxConcurrentInstances = 10;
    t.cullDistanceMultiplier = 0.9f;
    t.lodBias = 1;

    const auto r = t.Resolve({});
    EXPECT_EQ(r.maxConcurrentInstances, 10u);
    EXPECT_FLOAT_EQ(r.cullDistanceMultiplier, 0.9f);
    EXPECT_EQ(r.lodBias, 1);
}

TEST(VfxEffectTypeResolve, PlatformOverrideReplacesBaseline) {
    VfxEffectType t;
    t.maxConcurrentInstances = 10;
    t.cullDistanceMultiplier = 0.9f;
    t.lodBias = 1;

    VfxEffectTypePlatformOverride ov;
    ov.maxConcurrentInstances = 3;
    ov.lodBias = 2;
    t.platformOverrides["mobile"] = ov;

    const auto r = t.Resolve("mobile");
    EXPECT_EQ(r.maxConcurrentInstances, 3u);
    // cullDistanceMultiplier left unset in the override => baseline wins.
    EXPECT_FLOAT_EQ(r.cullDistanceMultiplier, 0.9f);
    EXPECT_EQ(r.lodBias, 2);
}

TEST(VfxEffectTypeResolve, UnknownPlatformFallsBackToBaseline) {
    VfxEffectType t;
    t.maxConcurrentInstances = 8;
    const auto r = t.Resolve("bogus");
    EXPECT_EQ(r.maxConcurrentInstances, 8u);
}

// ─── LRU tracker ────────────────────────────────────────────────────────────

TEST(VfxEffectTypeTracker, AdmitsUnderCap) {
    VfxEffectType t;
    t.name = "Ambient";
    t.maxConcurrentInstances = 3;

    VfxEffectTypeInstanceTracker tr;
    for (int i = 0; i < 3; ++i) {
        const auto r = tr.TryAcquire(t, /*handle=*/ 100u + i, /*priority=*/ 0);
        EXPECT_EQ(r.status,
                  VfxEffectTypeInstanceTracker::AcquireResult::Status::Admitted);
    }
    EXPECT_EQ(tr.GetActiveCount("Ambient"), 3u);
}

TEST(VfxEffectTypeTracker, EvictsLowestPriorityOldestWhenCapFull) {
    VfxEffectType t;
    t.name = "Weapon";
    t.maxConcurrentInstances = 2;

    VfxEffectTypeInstanceTracker tr;
    (void)tr.TryAcquire(t, /*handle=*/ 1u, /*priority=*/ 1);   // age 1
    (void)tr.TryAcquire(t, /*handle=*/ 2u, /*priority=*/ 5);   // age 2

    // Cap full. Newcomer priority 5 should evict handle 1 (priority 1, oldest).
    const auto r = tr.TryAcquire(t, /*handle=*/ 3u, /*priority=*/ 5);
    EXPECT_EQ(r.status,
              VfxEffectTypeInstanceTracker::AcquireResult::Status::AdmittedWithEviction);
    EXPECT_EQ(r.evicted.userHandle, 1u);
    EXPECT_EQ(tr.GetActiveCount("Weapon"), 2u);
}

TEST(VfxEffectTypeTracker, RejectsWhenAllExistingOutrankNewcomer) {
    VfxEffectType t;
    t.name = "Cinematic";
    t.maxConcurrentInstances = 1;

    VfxEffectTypeInstanceTracker tr;
    (void)tr.TryAcquire(t, /*handle=*/ 10u, /*priority=*/ 100);

    const auto r = tr.TryAcquire(t, /*handle=*/ 20u, /*priority=*/ 0);
    EXPECT_EQ(r.status,
              VfxEffectTypeInstanceTracker::AcquireResult::Status::Rejected);
    EXPECT_EQ(tr.GetActiveCount("Cinematic"), 1u);
}

TEST(VfxEffectTypeTracker, ReleaseRemovesEntry) {
    VfxEffectType t;
    t.name = "Test";
    t.maxConcurrentInstances = 4;

    VfxEffectTypeInstanceTracker tr;
    (void)tr.TryAcquire(t, 1u, 0);
    (void)tr.TryAcquire(t, 2u, 0);
    tr.Release("Test", 1u);
    EXPECT_EQ(tr.GetActiveCount("Test"), 1u);
    tr.Release("Test", 999u);  // unknown, no-op
    EXPECT_EQ(tr.GetActiveCount("Test"), 1u);
}

TEST(VfxEffectTypeTracker, ZeroCapMeansNoLimit) {
    VfxEffectType t;
    t.name = "Unbounded";
    t.maxConcurrentInstances = 0;  // no cap

    VfxEffectTypeInstanceTracker tr;
    for (int i = 0; i < 100; ++i) {
        const auto r = tr.TryAcquire(t, static_cast<uint64_t>(i + 1), 0);
        ASSERT_EQ(r.status,
                  VfxEffectTypeInstanceTracker::AcquireResult::Status::Admitted);
    }
    EXPECT_EQ(tr.GetActiveCount("Unbounded"), 100u);
}

TEST(VfxEffectTypeTracker, PlatformResolvedCapIsHonoured) {
    VfxEffectType t;
    t.name = "Weapon";
    t.maxConcurrentInstances = 16;

    VfxEffectTypePlatformOverride ov;
    ov.maxConcurrentInstances = 2;
    t.platformOverrides["mobile"] = ov;

    VfxEffectTypeInstanceTracker tr;
    (void)tr.TryAcquire(t, 1u, 0, "mobile");
    (void)tr.TryAcquire(t, 2u, 0, "mobile");
    // Third call should trigger the cap (via eviction or rejection).
    const auto r = tr.TryAcquire(t, 3u, 0, "mobile");
    EXPECT_NE(r.status,
              VfxEffectTypeInstanceTracker::AcquireResult::Status::Admitted);
}
