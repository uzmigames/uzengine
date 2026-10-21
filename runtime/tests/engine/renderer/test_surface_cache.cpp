// Surface radiance cache, Phase D stage 1 (docs/specs/rendering/surface-radiance-cache.md,
// acceptance A5): the contract math the shaders run (gi/surface_cache_contract.hlsli) evaluated
// on the CPU — the level / cell-size rule at its boundaries, the key round trip and the
// never-(0,0) guarantee, the normal index for the six axes and ties, the sample packing and the
// frame-cap arithmetic, the record metadata, the EMA, the jitter determinism — plus the binding
// contracts (the 64-byte params record, the resolve root block, the reflection pass's DWORD 16 and
// flag bit) and the CPU invalidation policy.
// (The reflection contract header defines non-inline functions and is owned by
// test_rt_reflection.cpp, which pins the C++ flag enum against it, the new bit included.)
#include "../../../shaders/runtime/gi/surface_cache_contract.hlsli"
#include "gi/surface_radiance_cache.h"
#include "passes/raytracing/rt_reflection_pass.h"
#include "core/cvar.h"

#include <gtest/gtest.h>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <set>

using namespace uz;
using namespace uz::surfacecache;

// ─── Level and cell size (D3) ───────────────────────────────────────────────────────────────

TEST(SurfaceCacheContract, LevelDoublesWithCameraDistanceFromLodDistance) {
    const float lod = 400.0f;
    EXPECT_EQ(SurfaceCacheLevel(0.0f, lod), 0u);
    EXPECT_EQ(SurfaceCacheLevel(399.0f, lod), 0u);
    EXPECT_EQ(SurfaceCacheLevel(400.0f, lod), 0u);              // ratio 1: log2 = 0
    EXPECT_EQ(SurfaceCacheLevel(799.0f, lod), 0u);              // ratio < 2
    EXPECT_EQ(SurfaceCacheLevel(800.0f, lod), 1u);              // ratio 2 exactly: the boundary
    EXPECT_EQ(SurfaceCacheLevel(1599.0f, lod), 1u);
    EXPECT_EQ(SurfaceCacheLevel(1600.0f, lod), 2u);
    EXPECT_EQ(SurfaceCacheLevel(3200.0f, lod), 3u);
    EXPECT_EQ(SurfaceCacheLevel(400.0f * 32768.0f, lod), 15u);  // the top level
    EXPECT_EQ(SurfaceCacheLevel(1e9f, lod), 15u);               // clamped
    EXPECT_EQ(SurfaceCacheLevel(-5.0f, lod), 0u);               // never negative
    EXPECT_EQ(SurfaceCacheLevel(500.0f, 0.0f), 0u);             // a degenerate lod distance
    EXPECT_EQ(SurfaceCacheLevel(std::nanf(""), lod), 0u);
    EXPECT_LT(SurfaceCacheLevel(1e30f, lod), kSurfaceCacheLevels);
}

TEST(SurfaceCacheContract, CellSizeDoublesPerLevel) {
    EXPECT_FLOAT_EQ(SurfaceCacheCellSize(10.0f, 0), 10.0f);
    EXPECT_FLOAT_EQ(SurfaceCacheCellSize(10.0f, 1), 20.0f);
    EXPECT_FLOAT_EQ(SurfaceCacheCellSize(10.0f, 4), 160.0f);
    EXPECT_FLOAT_EQ(SurfaceCacheCellSize(10.0f, 15), 10.0f * 32768.0f);
}

TEST(SurfaceCacheContract, CellCoordinatesFloorTowardMinusInfinity) {
    const int3 cell = SurfaceCacheCell(Vec3(25.0f, -0.5f, 10.0f), 10.0f);
    EXPECT_EQ(cell.x, 2);
    EXPECT_EQ(cell.y, -1);
    EXPECT_EQ(cell.z, 1);
    EXPECT_EQ(SurfaceCacheCellCoord(-10.0f, 10.0f), -1);
    EXPECT_EQ(SurfaceCacheCellCoord(-10.0001f, 10.0f), -2);
}

// ─── Normal index ───────────────────────────────────────────────────────────────────────────

TEST(SurfaceCacheContract, NormalIndexCoversTheSixAxesAndBreaksTiesTowardXThenY) {
    EXPECT_EQ(SurfaceCacheNormalIndex(Vec3(1, 0, 0)), 0u);
    EXPECT_EQ(SurfaceCacheNormalIndex(Vec3(-1, 0, 0)), 1u);
    EXPECT_EQ(SurfaceCacheNormalIndex(Vec3(0, 1, 0)), 2u);
    EXPECT_EQ(SurfaceCacheNormalIndex(Vec3(0, -1, 0)), 3u);
    EXPECT_EQ(SurfaceCacheNormalIndex(Vec3(0, 0, 1)), 4u);
    EXPECT_EQ(SurfaceCacheNormalIndex(Vec3(0, 0, -1)), 5u);
    EXPECT_EQ(SurfaceCacheNormalIndex(Vec3(0.3f, -0.9f, 0.2f)), 3u);
    EXPECT_EQ(SurfaceCacheNormalIndex(Vec3(0.5f, 0.5f, 0.5f)), 0u);     // three-way tie: X
    EXPECT_EQ(SurfaceCacheNormalIndex(Vec3(0.0f, -0.7f, 0.7f)), 3u);    // Y/Z tie: Y
    EXPECT_EQ(SurfaceCacheNormalIndex(Vec3(-0.7f, 0.0f, 0.7f)), 1u);    // X/Z tie: X
    EXPECT_LT(SurfaceCacheNormalIndex(Vec3(0, 0, 0)), 6u);               // a zero normal still keys
}

// ─── Key packing (D2) ───────────────────────────────────────────────────────────────────────

TEST(SurfaceCacheContract, KeyRoundTripsCellNormalAndLevel) {
    const int3 cells[] = {{0, 0, 0}, {1, 2, 3}, {-1, -2, -3}, {131071, -131072, 4095}, {-131072, 131071, -8192}, {8191, 8192, 8193}};
    for (const int3& cell : cells) {
        for (uint32_t normal = 0; normal < 6; ++normal) {
            for (uint32_t level : {0u, 1u, 7u, 15u}) {
                const uint2 key = SurfaceCacheKey(cell, normal, level);
                const int3 back = SurfaceCacheKeyCell(key);
                EXPECT_EQ(back.x, cell.x);
                EXPECT_EQ(back.y, cell.y);
                EXPECT_EQ(back.z, cell.z);
                EXPECT_EQ(SurfaceCacheKeyNormal(key), normal);
                EXPECT_EQ(SurfaceCacheKeyLevel(key), level);
            }
        }
    }
}

TEST(SurfaceCacheContract, KeyIsNeverEmptyAndCoordinatesClampToTheRange) {
    const uint2 origin = SurfaceCacheKey(int3{0, 0, 0}, 0, 0);
    EXPECT_NE(origin.x, 0u);
    EXPECT_NE(origin.y, 0u);
    EXPECT_FALSE(SurfaceCacheKeyEmpty(origin));
    EXPECT_TRUE(SurfaceCacheKeyLive(origin));
    EXPECT_FALSE(SurfaceCacheKeyFree(origin));
    EXPECT_TRUE(SurfaceCacheKeyEmpty(uint2{0u, 0u}));
    EXPECT_TRUE(SurfaceCacheKeyFree(uint2{0u, 0u}));
    // A tombstone ends no probe chain, is not live, and can be claimed by an insert.
    const uint2 tombstone{0u, kSurfaceCacheTombstoneY};
    EXPECT_FALSE(SurfaceCacheKeyEmpty(tombstone));
    EXPECT_FALSE(SurfaceCacheKeyLive(tombstone));
    EXPECT_TRUE(SurfaceCacheKeyFree(tombstone));
    // A torn insert (first word written, second not yet) is neither empty nor free.
    const uint2 torn{origin.x, 0u};
    EXPECT_FALSE(SurfaceCacheKeyEmpty(torn));
    EXPECT_FALSE(SurfaceCacheKeyFree(torn));
    // Out-of-range coordinates clamp instead of wrapping into another cell's key.
    const int3 far = SurfaceCacheKeyCell(SurfaceCacheKey(int3{1000000, -1000000, 0}, 0, 0));
    EXPECT_EQ(far.x, 131071);
    EXPECT_EQ(far.y, -131072);
    // The fields do not alias: distinct (cell, normal, level) give distinct keys.
    std::set<uint64_t> keys;
    for (int x = -2; x <= 2; ++x)
        for (int y = -2; y <= 2; ++y)
            for (int z = -2; z <= 2; ++z)
                for (uint32_t normal = 0; normal < 6; ++normal)
                    for (uint32_t level = 0; level < 16; ++level) {
                        const uint2 key = SurfaceCacheKey(int3{x, y, z}, normal, level);
                        keys.insert((uint64_t(key.y) << 32) | key.x);
                    }
    EXPECT_EQ(keys.size(), size_t(5 * 5 * 5 * 6 * 16));
}

TEST(SurfaceCacheContract, SlotStaysInsideTheTableAndSpreadsKeys) {
    const uint2 key = SurfaceCacheKey(int3{12, -7, 3}, 2, 1);
    EXPECT_LT(SurfaceCacheSlot(key, 16), 1u << 16);
    EXPECT_LT(SurfaceCacheSlot(key, 22), 1u << 22);
    EXPECT_EQ(SurfaceCacheSlot(key, 20), SurfaceCacheHash(key) & ((1u << 20) - 1u));
    // Neighbouring cells do not collide systematically (a weak spread check on 1000 keys).
    std::set<uint32_t> slots;
    for (int i = 0; i < 1000; ++i) slots.insert(SurfaceCacheSlot(SurfaceCacheKey(int3{i, i / 7, -i / 3}, i % 6, 0), 20));
    EXPECT_GT(slots.size(), 990u);
    // XXH32 reference digests (seed 0, the two little-endian words as 8 bytes), from the
    // reference algorithm validated on the published vectors XXH32("") = 0x02cc5d05 and
    // XXH32("a") = 0x550d7456.
    EXPECT_EQ(SurfaceCacheHash(uint2{0u, 0u}), 0xdeb39513u);
    EXPECT_EQ(SurfaceCacheHash(uint2{1u, 0u}), 0x08ed6331u);
    EXPECT_EQ(SurfaceCacheHash(uint2{0u, 1u}), 0xda0a0754u);
    EXPECT_EQ(SurfaceCacheHash(uint2{0x80000000u, 0x80000000u}), 0x40d1685au);
    EXPECT_EQ(SurfaceCacheHash(uint2{0xDEADBEEFu, 0x12345678u}), 0xa73d0caau);
}

// ─── Jitter ─────────────────────────────────────────────────────────────────────────────────

TEST(SurfaceCacheContract, JitterIsHalfACellAndDeterministicPerCellAndFrame) {
    const int3 cell{5, -3, 9};
    const Vec3 a = SurfaceCacheJitter(cell, 17);
    const Vec3 b = SurfaceCacheJitter(cell, 17);
    EXPECT_EQ(a.x, b.x);
    EXPECT_EQ(a.y, b.y);
    EXPECT_EQ(a.z, b.z);
    for (uint32_t frame = 0; frame < 64; ++frame) {
        const Vec3 j = SurfaceCacheJitter(cell, frame);
        EXPECT_GE(j.x, -0.5f); EXPECT_LT(j.x, 0.5f);
        EXPECT_GE(j.y, -0.5f); EXPECT_LT(j.y, 0.5f);
        EXPECT_GE(j.z, -0.5f); EXPECT_LT(j.z, 0.5f);
    }
    const Vec3 c = SurfaceCacheJitter(cell, 18);
    EXPECT_TRUE(a.x != c.x || a.y != c.y || a.z != c.z);
}

// ─── Fixed-point samples (D5) ───────────────────────────────────────────────────────────────

TEST(SurfaceCacheContract, PackSampleClampsScalesAndRoundTrips) {
    EXPECT_EQ(SurfaceCachePackSample(0.0f, 4096.0f), 0u);
    EXPECT_EQ(SurfaceCachePackSample(-1.0f, 4096.0f), 0u);
    EXPECT_EQ(SurfaceCachePackSample(std::nanf(""), 4096.0f), 0u);
    EXPECT_EQ(SurfaceCachePackSample(1.0f, 4096.0f), 1024u);
    EXPECT_EQ(SurfaceCachePackSample(0.5f, 4096.0f), 512u);
    EXPECT_EQ(SurfaceCachePackSample(1e9f, 4096.0f), kSurfaceCachePackMax);
    EXPECT_EQ(SurfaceCachePackSample(1e9f, 8.0f), 8192u);           // the per-sample clamp
    EXPECT_FLOAT_EQ(SurfaceCacheUnpackMean(SurfaceCachePackSample(1.0f, 4096.0f) * 10u, 10u), 1.0f);
    EXPECT_NEAR(SurfaceCacheUnpackMean(SurfaceCachePackSample(0.123f, 4096.0f), 1u), 0.123f, 0.5f / kSurfaceCacheScale);
}

TEST(SurfaceCacheContract, FrameCapNeverOverflowsTheAccumulator) {
    const uint64_t largest = SurfaceCachePackSample(1e30f, 4096.0f);
    EXPECT_EQ(largest, uint64_t(kSurfaceCachePackMax));
    EXPECT_LE(largest * kSurfaceCacheFrameCap, uint64_t(0xFFFFFFFFu));
    EXPECT_EQ(kSurfaceCachePackMax, 0xFFFFFFFFu / kSurfaceCacheFrameCap);
    // The radiance clamp itself (4096 x 2^10 = 2^22) is above the pack cap by one, so the cap is
    // what bounds the sum, not the clamp.
    EXPECT_EQ(uint32_t(4096.0f * kSurfaceCacheScale), kSurfaceCachePackMax + 1u);
}

// ─── Resolved record metadata + EMA ────────────────────────────────────────────────────────

TEST(SurfaceCacheContract, MetaPacksSamplesFramesAndAge) {
    const uint32_t meta = SurfaceCachePackMeta(65535, 32, 7);
    EXPECT_EQ(SurfaceCacheMetaSamples(meta), 65535u);
    EXPECT_EQ(SurfaceCacheMetaFrames(meta), 32u);
    EXPECT_EQ(SurfaceCacheMetaAge(meta), 7u);
    EXPECT_EQ(SurfaceCacheMetaSamples(SurfaceCachePackMeta(70000, 0, 0)), 70000u & 0xFFFFu);
    EXPECT_TRUE(SurfaceCacheReadable(SurfaceCachePackMeta(8, 1, 0), 8));
    EXPECT_FALSE(SurfaceCacheReadable(SurfaceCachePackMeta(7, 1, 0), 8));
    EXPECT_EQ(SurfaceCacheNextSamples(65530, 100), 65535u);
    EXPECT_EQ(SurfaceCacheNextSamples(10, 5), 15u);
}

TEST(SurfaceCacheContract, EmaIsTheRunningMeanThenAnExponentialAverage) {
    // Running mean while frames < AccumFrames: fold 2, 4, 6 -> 2, 3, 4.
    uint32_t frames = 0;
    float mean = 0.0f;
    for (float v : {2.0f, 4.0f, 6.0f}) {
        frames = SurfaceCacheNextFrames(frames, 32);
        mean = SurfaceCacheEma(mean, v, frames);
    }
    EXPECT_EQ(frames, 3u);
    EXPECT_FLOAT_EQ(mean, 4.0f);
    // At the horizon the weight stays 1 / AccumFrames.
    EXPECT_EQ(SurfaceCacheNextFrames(32, 32), 32u);
    EXPECT_EQ(SurfaceCacheNextFrames(200, 32), 32u);
    EXPECT_FLOAT_EQ(SurfaceCacheEma(10.0f, 42.0f, 32), 10.0f + 32.0f / 32.0f);
    // A first fold replaces the (empty) mean.
    EXPECT_FLOAT_EQ(SurfaceCacheEma(0.0f, 3.5f, SurfaceCacheNextFrames(0, 32)), 3.5f);
}

// ─── Binding contracts ──────────────────────────────────────────────────────────────────────

TEST(SurfaceCacheContract, ParamsRecordMirrorsTheShaderStruct) {
    EXPECT_EQ(sizeof(SurfaceCacheParams), 64u);
    EXPECT_EQ(sizeof(SurfaceCacheParamsGPU), 64u);
    EXPECT_EQ(offsetof(SurfaceCacheParamsGPU, keysUav), offsetof(SurfaceCacheParams, keysUav));
    EXPECT_EQ(offsetof(SurfaceCacheParamsGPU, resolvedSrv), offsetof(SurfaceCacheParams, resolvedSrv));
    EXPECT_EQ(offsetof(SurfaceCacheParamsGPU, entriesLog2), 16u);
    EXPECT_EQ(offsetof(SurfaceCacheParamsGPU, flags), 24u);
    EXPECT_EQ(offsetof(SurfaceCacheParamsGPU, frame), offsetof(SurfaceCacheParams, frame));
    EXPECT_EQ(offsetof(SurfaceCacheParamsGPU, cellSize), 32u);
    EXPECT_EQ(offsetof(SurfaceCacheParamsGPU, maxRadiance), offsetof(SurfaceCacheParams, maxRadiance));
    EXPECT_EQ(offsetof(SurfaceCacheParamsGPU, statsUav), offsetof(SurfaceCacheParams, statsUav));
    EXPECT_EQ(offsetof(SurfaceCacheParamsGPU, generation), 56u);
    EXPECT_EQ(offsetof(SurfaceCacheParamsGPU, refreshRate), offsetof(SurfaceCacheParams, refreshRate));
    EXPECT_EQ(offsetof(SurfaceCacheParamsGPU, refreshRate), 60u);
}

TEST(SurfaceCacheContract, RefreshSelectsOneHitInRateDeterministically) {
    EXPECT_FALSE(SurfaceCacheRefresh(5, 7, 0));
    EXPECT_TRUE(SurfaceCacheRefresh(5, 7, 1));
    EXPECT_EQ(SurfaceCacheRefresh(5, 7, 16), SurfaceCacheRefresh(5, 7, 16));
    uint32_t selected = 0;
    for (uint32_t ray = 0; ray < 64000; ++ray) selected += SurfaceCacheRefresh(ray, 3, 16) ? 1u : 0u;
    EXPECT_NEAR(double(selected) / 64000.0, 1.0 / 16.0, 0.005);
    // The same ray is refreshed on different frames, not always or never.
    uint32_t frames = 0;
    for (uint32_t frame = 0; frame < 256; ++frame) frames += SurfaceCacheRefresh(1234, frame, 16) ? 1u : 0u;
    EXPECT_GT(frames, 4u);
    EXPECT_LT(frames, 40u);
    EXPECT_EQ(kSurfaceCacheTagTouched & kSurfaceCacheWriterReflections, 0u);
}

TEST(SurfaceCacheContract, ResolveRootBlockFitsTheBindlessBudget) {
    EXPECT_EQ(sizeof(SurfaceCacheResolveRootConstants), kSurfaceCacheResolveRootDwords * sizeof(uint32_t));
    EXPECT_LE(kSurfaceCacheResolveRootDwords, 20u);
    EXPECT_EQ(offsetof(SurfaceCacheResolveRootConstants, mode), 28u);
    EXPECT_EQ(offsetof(SurfaceCacheResolveRootConstants, cellSize), 32u);
    EXPECT_EQ(offsetof(SurfaceCacheResolveRootConstants, frame), 40u);
    EXPECT_EQ(sizeof(SurfaceCacheResolveRegionsCB), 512u);
    EXPECT_EQ(kSurfaceCacheStatWords, 2u * kSurfaceCacheStatCount);
    EXPECT_LT(kSurfaceCacheStatProbeSum, kSurfaceCacheStatCount);
}

TEST(SurfaceCacheContract, ReflectionPassBindsTheCacheAtDwordSixteenUnderItsOwnFlag) {
    EXPECT_EQ(offsetof(RTReflectionRootConstants, surfaceCacheParamsSrv), 64u);
    EXPECT_EQ(kRTReflectionRootDwords, 17u);
    EXPECT_LE(kRTReflectionRootDwords, 20u);
    EXPECT_EQ(uint32_t(kRTReflectionFlagSurfaceCache), 16u);
    // Disjoint from the stage 1-3 bits.
    EXPECT_EQ(uint32_t(kRTReflectionFlagSurfaceCache)
              & (kRTReflectionFlagGlossy | kRTReflectionFlagIndirect
                 | kRTReflectionFlagHitDistance | kRTReflectionFlagNoScreenTrace), 0u);
    const RTReflectionRootConstants defaults{};
    EXPECT_EQ(defaults.surfaceCacheParamsSrv, UINT32_MAX);
    EXPECT_EQ(defaults.flags & kRTReflectionFlagSurfaceCache, 0u);
}

TEST(SurfaceCacheContract, DebugModeLivesInTheFlagBits) {
    EXPECT_EQ(kSurfaceCacheFlagDebugMask, 3u << kSurfaceCacheFlagDebugShift);
    EXPECT_EQ(kSurfaceCacheFlagDebugMask & (kSurfaceCacheFlagDirectInline | kSurfaceCacheFlagJitter
                                            | kSurfaceCacheFlagSdfWrites | kSurfaceCacheFlagStats), 0u);
}

// ─── CVar defaults: opt-in, nothing on by default ───────────────────────────────────────────

TEST(SurfaceCacheContract, RegisteredDefaultsAreOff) {
    auto& registry = CVarRegistry::Get();
    ASSERT_NE(registry.Find("r.GI.SurfaceCache"), nullptr);
    EXPECT_EQ(registry.Find("r.GI.SurfaceCache")->defaultValue, 0.0f);
    EXPECT_FALSE(SurfaceRadianceCache::Enabled());
    EXPECT_EQ(registry.Find("r.GI.SurfaceCache.EntriesLog2")->defaultValue, 20.0f);
    EXPECT_EQ(registry.Find("r.GI.SurfaceCache.CellSize")->defaultValue, 10.0f);
    EXPECT_EQ(registry.Find("r.GI.SurfaceCache.LodDistance")->defaultValue, 400.0f);
    EXPECT_EQ(registry.Find("r.GI.SurfaceCache.MinSamples")->defaultValue, 8.0f);
    EXPECT_EQ(registry.Find("r.GI.SurfaceCache.AccumFrames")->defaultValue, 32.0f);
    EXPECT_EQ(registry.Find("r.GI.SurfaceCache.StaleFrames")->defaultValue, 64.0f);
    EXPECT_EQ(registry.Find("r.GI.SurfaceCache.MaxRadiance")->defaultValue, 4096.0f);
    EXPECT_EQ(registry.Find("r.GI.SurfaceCache.Jitter")->defaultValue, 1.0f);
    EXPECT_EQ(registry.Find("r.GI.SurfaceCache.DirectMode")->defaultValue, 0.0f);
    EXPECT_EQ(registry.Find("r.GI.SurfaceCache.Stats")->defaultValue, 0.0f);
    EXPECT_EQ(registry.Find("r.GI.SurfaceCache.Debug")->defaultValue, 0.0f);
}

// ─── Invalidation policy (spec 2.6) ─────────────────────────────────────────────────────────

TEST(SurfaceCacheContract, InvalidationClearsOnRevisionOrLightingAndEvictsByBoxes) {
    SurfaceCacheInvalidation d = SurfaceCacheDecideInvalidation(false, 0, false, false);
    EXPECT_FALSE(d.clear); EXPECT_EQ(d.regionCount, 0u);
    d = SurfaceCacheDecideInvalidation(true, 3, false, false);
    EXPECT_TRUE(d.clear); EXPECT_EQ(d.regionCount, 0u);
    d = SurfaceCacheDecideInvalidation(false, 3, true, false);        // boxes beat the geometry hash
    EXPECT_FALSE(d.clear); EXPECT_EQ(d.regionCount, 3u);
    d = SurfaceCacheDecideInvalidation(false, 0, true, false);        // moved with no box: clear
    EXPECT_TRUE(d.clear);
    d = SurfaceCacheDecideInvalidation(false, 0, false, true);        // lights changed: clear
    EXPECT_TRUE(d.clear);
    d = SurfaceCacheDecideInvalidation(false, kSurfaceCacheMaxRegions + 1, false, false);
    EXPECT_TRUE(d.clear);                                             // more boxes than the resolve carries
    d = SurfaceCacheDecideInvalidation(false, kSurfaceCacheMaxRegions, false, false);
    EXPECT_FALSE(d.clear); EXPECT_EQ(d.regionCount, kSurfaceCacheMaxRegions);
}

TEST(SurfaceCacheContract, ByteHashIsOrderSensitiveAndChainable) {
    const uint32_t a[2] = {1, 2}, b[2] = {2, 1};
    EXPECT_NE(SurfaceCacheHashBytes(a, sizeof(a)), SurfaceCacheHashBytes(b, sizeof(b)));
    EXPECT_EQ(SurfaceCacheHashBytes(a, sizeof(a)),
              SurfaceCacheHashBytes(&a[1], sizeof(a[1]), SurfaceCacheHashBytes(&a[0], sizeof(a[0]))));
    EXPECT_EQ(SurfaceCacheHashBytes(a, 0), 14695981039346656037ull);
}
