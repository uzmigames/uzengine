// Surface radiance cache, Phase D stage 1 (docs/specs/rendering/surface-radiance-cache.md,
// acceptance A5): the contract math the shaders run (gi/surface_cache_contract.hlsli) evaluated
// on the CPU — the level / cell-size rule at its boundaries, the key round trip and the
// never-(0,0) guarantee, the normal index for the six axes and ties, the sample packing and the
// frame-cap arithmetic, the record metadata, the EMA, the jitter determinism — plus the binding
// contracts (the params record, the resolve root block, the reflection pass's DWORD 16 and flag
// bit) and the CPU invalidation policy. Stage 2 (acceptance B1-B4): the local-light scalars the
// cell lighting shares with the deferred lighting (attenuation, spot cone, rect window, the
// relevance luminance), the 80-byte light record and its CPU builder, the emissive extension of
// the GI material table and the stage 2 CVar defaults. Stage 3 (acceptance C1-C5): the SDF
// consumers' cell read — the biased level, the snap of an expanded-surface hit back to the raw
// surface, the reader policy that replaces the whole hit radiance (the SH writer's and
// ShadeSdfReflectionHit's math) — the stage 3 flags / writer tags / stats, the feed root block,
// the three consumer bindings (the LightingCB pad, the SSGI root DWORDs, the 96-byte record's
// camera row) and the stage 3 CVar defaults.
// (The reflection contract header defines non-inline functions and is owned by
// test_rt_reflection.cpp, which pins the C++ flag enum against it, the new bit included.)
#include "../../../shaders/runtime/gi/surface_cache_contract.hlsli"
#include "gi/surface_radiance_cache.h"
#include "gi/ssgi_renderer.h"
#include "passes/raytracing/rt_reflection_pass.h"
#include "passes/pathtracing/path_tracer_pass.h"
#include "constant_buffers.h"
#include "core/cvar.h"

#include <gtest/gtest.h>
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <set>
#include <vector>

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
    // 96 bytes since stage 3 (the camera row); 80 with stage 2's lights row; the first four rows
    // are the stage 1 record.
    EXPECT_EQ(sizeof(SurfaceCacheParams), 96u);
    EXPECT_EQ(sizeof(SurfaceCacheParamsGPU), 96u);
    EXPECT_EQ(offsetof(SurfaceCacheParamsGPU, cameraPosX), offsetof(SurfaceCacheParams, cameraPosX));
    EXPECT_EQ(offsetof(SurfaceCacheParamsGPU, cameraPosX), 80u);
    EXPECT_EQ(offsetof(SurfaceCacheParamsGPU, cameraPosZ), 88u);
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
    EXPECT_EQ(offsetof(SurfaceCacheParamsGPU, lightsSrv), offsetof(SurfaceCacheParams, lightsSrv));
    EXPECT_EQ(offsetof(SurfaceCacheParamsGPU, lightsSrv), 64u);
    EXPECT_EQ(offsetof(SurfaceCacheParamsGPU, lightCount), offsetof(SurfaceCacheParams, lightCount));
    EXPECT_EQ(offsetof(SurfaceCacheParamsGPU, localLights), offsetof(SurfaceCacheParams, localLights));
    EXPECT_EQ(offsetof(SurfaceCacheParamsGPU, localLights), 72u);
    const SurfaceCacheParamsGPU defaults{};
    EXPECT_EQ(defaults.lightsSrv, UINT32_MAX);
    EXPECT_EQ(defaults.lightCount, 0u);
    EXPECT_EQ(defaults.localLights, 0u);
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
    // Stage 2: the sub-switches default to the spec's K = 4 and emissive on, under the master (off).
    ASSERT_NE(registry.Find("r.GI.SurfaceCache.LocalLights"), nullptr);
    EXPECT_EQ(registry.Find("r.GI.SurfaceCache.LocalLights")->defaultValue, 4.0f);
    EXPECT_EQ(registry.Find("r.GI.SurfaceCache.Emissive")->defaultValue, 1.0f);
    EXPECT_EQ(SurfaceRadianceCache::LocalLights(), 4u);
    EXPECT_TRUE(SurfaceRadianceCache::EmissiveEnabled());
}

// ─── Stage 3: the SDF consumers and the probes read exact-material radiance (spec 2.5, 3.1, D7, D8) ──

TEST(SurfaceCacheStage3, SdfLevelIsTheCameraLevelPlusTheBiasClampedToTheLastLevel) {
    const float lod = 400.0f;
    EXPECT_EQ(SurfaceCacheSdfLevel(100.0f, lod, 0), 0u);
    EXPECT_EQ(SurfaceCacheSdfLevel(100.0f, lod, 2), 2u);
    EXPECT_EQ(SurfaceCacheSdfLevel(800.0f, lod, 2), SurfaceCacheLevel(800.0f, lod) + 2u);
    EXPECT_EQ(SurfaceCacheSdfLevel(3200.0f, lod, 4), 3u + 4u);
    // Never past the last level the key can hold.
    EXPECT_EQ(SurfaceCacheSdfLevel(1e9f, lod, 4), kSurfaceCacheLevels - 1u);
    EXPECT_EQ(SurfaceCacheSdfLevel(lod * 16384.0f, lod, 1), kSurfaceCacheLevels - 1u);
    // The biased level is what the cell size doubles by: SdfLodBias 2 = 4 x the TLAS hit's cell.
    EXPECT_FLOAT_EQ(SurfaceCacheCellSize(10.0f, SurfaceCacheSdfLevel(100.0f, lod, 2)),
                    4.0f * SurfaceCacheCellSize(10.0f, SurfaceCacheLevel(100.0f, lod)));
}

TEST(SurfaceCacheStage3, SnapDistanceKeepsSurfaceDistancesAndDropsMarkersFarHitsAndNaN) {
    EXPECT_FLOAT_EQ(SurfaceCacheSnapDistance(12.5f, kSurfaceCacheMaxSnap), 12.5f);
    EXPECT_FLOAT_EQ(SurfaceCacheSnapDistance(-3.0f, kSurfaceCacheMaxSnap), -3.0f);     // inside: moved outward
    EXPECT_FLOAT_EQ(SurfaceCacheSnapDistance(0.0f, kSurfaceCacheMaxSnap), 0.0f);
    EXPECT_FLOAT_EQ(SurfaceCacheSnapDistance(kSurfaceCacheMaxSnap, kSurfaceCacheMaxSnap), kSurfaceCacheMaxSnap);
    EXPECT_FLOAT_EQ(SurfaceCacheSnapDistance(-65504.0f, kSurfaceCacheMaxSnap), 0.0f);  // missing raw cascade
    EXPECT_FLOAT_EQ(SurfaceCacheSnapDistance(65504.0f, kSurfaceCacheMaxSnap), 0.0f);   // outside every cascade
    EXPECT_FLOAT_EQ(SurfaceCacheSnapDistance(1e9f, kSurfaceCacheMaxSnap), 0.0f);       // the GDF sentinel
    EXPECT_FLOAT_EQ(SurfaceCacheSnapDistance(std::nanf(""), kSurfaceCacheMaxSnap), 0.0f);
    EXPECT_GT(kSurfaceCacheMaxSnap, 0.0f);
    // = kReflectionSurfaceBias (rt_reflection_contract.hlsli, pinned by test_rt_reflection.cpp) and the
    // DDGI writer's kSurfaceBias: the point every TLAS writer keys its cell from.
    EXPECT_FLOAT_EQ(kSurfaceCacheSurfaceBias, 2.0f);
}

TEST(SurfaceCacheStage3, SdfReadPositionMovesTheHitToTheRawSurfacePlusTheWritersBias) {
    const Vec3 hit(100.0f, 50.0f, -20.0f);
    const Vec3 n(0.0f, 0.0f, 1.0f);
    // An expanded-surface hit 7 cm above the raw surface reads at raw surface + 2 cm.
    const Vec3 p = SurfaceCacheSdfReadPosition(hit, n, 7.0f);
    EXPECT_FLOAT_EQ(p.x, 100.0f);
    EXPECT_FLOAT_EQ(p.y, 50.0f);
    EXPECT_FLOAT_EQ(p.z, -20.0f - 7.0f + kSurfaceCacheSurfaceBias);
    // No raw distance (a marker): only the bias is applied, the hit is treated as the surface.
    const Vec3 q = SurfaceCacheSdfReadPosition(hit, n, -65504.0f);
    EXPECT_FLOAT_EQ(q.z, -20.0f + kSurfaceCacheSurfaceBias);
    // A general normal: the shift is along it.
    const Vec3 m(0.6f, 0.8f, 0.0f);
    const Vec3 r = SurfaceCacheSdfReadPosition(hit, m, 12.0f);
    EXPECT_NEAR(r.x, 100.0f - 0.6f * 10.0f, 1e-4f);
    EXPECT_NEAR(r.y, 50.0f - 0.8f * 10.0f, 1e-4f);
    EXPECT_FLOAT_EQ(r.z, -20.0f);
}

// The reader policy (D7) every stage 3 consumer applies — ShadeSdfReflectionHit, the SH writer's
// hit branch and the DDGI trace: a readable cell replaces the WHOLE hit radiance (never adds the
// SH term on top), DirectMode 1 keeps the inline sun, a miss is the inline shading unchanged.
TEST(SurfaceCacheStage3, HitRadianceReplacesTheWholeShadingOnlyWhenACellIsReadable) {
    const Vec3 cellSun(1.0f, 2.0f, 3.0f), cellOther(0.5f, 0.25f, 0.125f);
    const Vec3 inlineSun(10.0f, 20.0f, 30.0f), inlineRest(7.0f, 7.0f, 7.0f);
    const Vec3 miss = SurfaceCacheHitRadiance(false, false, cellSun, cellOther, inlineSun, inlineRest);
    EXPECT_FLOAT_EQ(miss.x, 17.0f); EXPECT_FLOAT_EQ(miss.y, 27.0f); EXPECT_FLOAT_EQ(miss.z, 37.0f);
    const Vec3 cell = SurfaceCacheHitRadiance(true, false, cellSun, cellOther, inlineSun, inlineRest);
    EXPECT_FLOAT_EQ(cell.x, 1.5f); EXPECT_FLOAT_EQ(cell.y, 2.25f); EXPECT_FLOAT_EQ(cell.z, 3.125f);
    const Vec3 direct = SurfaceCacheHitRadiance(true, true, cellSun, cellOther, inlineSun, inlineRest);
    EXPECT_FLOAT_EQ(direct.x, 10.5f); EXPECT_FLOAT_EQ(direct.y, 20.25f); EXPECT_FLOAT_EQ(direct.z, 30.125f);
    // The SH writer's hit: albedo x (sun / pi + E / pi) inline, the cell's two layers when readable —
    // the probe then projects the exact-material radiance with the same 4 pi / N weight.
    const Vec3 albedo(0.5f, 0.25f, 1.0f);
    const Vec3 sunRad(3.14159265f, 3.14159265f, 3.14159265f), indirect(0.2f, 0.2f, 0.2f);
    const Vec3 writerSun(albedo.x * sunRad.x / kSurfaceCachePi, albedo.y * sunRad.y / kSurfaceCachePi, albedo.z * sunRad.z / kSurfaceCachePi);
    const Vec3 writerRest(albedo.x * indirect.x, albedo.y * indirect.y, albedo.z * indirect.z);
    const Vec3 inlineWriter = SurfaceCacheHitRadiance(false, false, cellSun, cellOther, writerSun, writerRest);
    EXPECT_NEAR(inlineWriter.x, 0.5f * (1.0f + 0.2f), 1e-6f);
    EXPECT_NEAR(inlineWriter.y, 0.25f * (1.0f + 0.2f), 1e-6f);
    EXPECT_NEAR(inlineWriter.z, 1.0f * (1.0f + 0.2f), 1e-6f);
    const Vec3 cachedWriter = SurfaceCacheHitRadiance(true, false, cellSun, cellOther, writerSun, writerRest);
    EXPECT_FLOAT_EQ(cachedWriter.x, cellSun.x + cellOther.x);
    EXPECT_FLOAT_EQ(cachedWriter.z, cellSun.z + cellOther.z);
}

TEST(SurfaceCacheStage3, FlagsWriterTagsAndStatsAreDisjointAndInRange) {
    // The new flag bit is its own and outside the debug field.
    EXPECT_EQ(kSurfaceCacheFlagSdfReads & (kSurfaceCacheFlagDirectInline | kSurfaceCacheFlagJitter
                                           | kSurfaceCacheFlagSdfWrites | kSurfaceCacheFlagStats
                                           | kSurfaceCacheFlagEmissive | kSurfaceCacheFlagDebugMask), 0u);
    // Writer tags: one bit each, disjoint, below the touch bit.
    const uint32_t tags[] = {kSurfaceCacheWriterReflections, kSurfaceCacheWriterFeed, kSurfaceCacheWriterDdgi, kSurfaceCacheWriterSdf};
    uint32_t all = 0;
    for (uint32_t t : tags) {
        EXPECT_EQ(t & (t - 1u), 0u);
        EXPECT_EQ(all & t, 0u);
        EXPECT_EQ(t & kSurfaceCacheTagTouched, 0u);
        all |= t;
    }
    // Stats: the stage 3 words are distinct and inside the block.
    const uint32_t stats[] = {kSurfaceCacheStatSdfQueries, kSurfaceCacheStatSdfHits, kSurfaceCacheStatFeedUpdates};
    std::set<uint32_t> distinct;
    for (uint32_t s : stats) {
        EXPECT_LT(s, kSurfaceCacheStatCount);
        EXPECT_GT(s, kSurfaceCacheStatProbeSum);
        distinct.insert(s);
    }
    EXPECT_EQ(distinct.size(), 3u);
}

TEST(SurfaceCacheStage3, FeedRootBlockAndTheConsumerBindingsFitTheirBudgets) {
    EXPECT_EQ(sizeof(SurfaceCacheFeedRootConstants), 20u);
    EXPECT_EQ(kSurfaceCacheFeedRootDwords, 5u);
    EXPECT_EQ(offsetof(SurfaceCacheFeedRootConstants, surfaceCacheParamsSrv), 12u);
    EXPECT_EQ(offsetof(SurfaceCacheFeedRootConstants, feedRays), 16u);
    const SurfaceCacheFeedRootConstants feed{};
    EXPECT_EQ(feed.tlasSrv, UINT32_MAX);
    EXPECT_EQ(feed.materialTableSrv, UINT32_MAX);
    EXPECT_EQ(feed.feedRays, 0u);
    // The deferred lighting's binding in the LightingCB's former pad: the CB size and every
    // offset before it unchanged.
    EXPECT_EQ(sizeof(LightingCB), 43264u);
    EXPECT_EQ(offsetof(LightingCB, surfaceCacheParamsSrv), 43104u);
    EXPECT_EQ(offsetof(LightingCB, ltcMagnitudeSrv) + 4u, offsetof(LightingCB, surfaceCacheParamsSrv));
    const LightingCB lighting{};
    EXPECT_EQ(lighting.surfaceCacheParamsSrv, UINT32_MAX);
    // The ReSTIR GI root block: the former reserved0, the block still 16 DWORDs.
    EXPECT_EQ(sizeof(SSGIRestirRootConstants), 64u);
    EXPECT_EQ(offsetof(SSGIRestirRootConstants, surfaceCacheParamsSrv), 56u);
    // The reflection pass's block is untouched by stage 3.
    EXPECT_EQ(kRTReflectionRootDwords, 17u);
}

TEST(SurfaceCacheStage3, RegisteredDefaultsFollowTheSpecUnderTheMasterSwitch) {
    auto& registry = CVarRegistry::Get();
    EXPECT_FALSE(SurfaceRadianceCache::Enabled());
    ASSERT_NE(registry.Find("r.GI.SurfaceCache.FeedRays"), nullptr);
    EXPECT_EQ(registry.Find("r.GI.SurfaceCache.FeedRays")->defaultValue, 16.0f);
    EXPECT_EQ(registry.Find("r.GI.SurfaceCache.SdfReads")->defaultValue, 1.0f);
    EXPECT_EQ(registry.Find("r.GI.SurfaceCache.SdfLodBias")->defaultValue, 2.0f);
    EXPECT_EQ(registry.Find("r.GI.SurfaceCache.SdfWrites")->defaultValue, 0.0f);
    EXPECT_EQ(registry.Find("r.GI.SurfaceCache.DDGI")->defaultValue, 1.0f);
    EXPECT_EQ(SurfaceRadianceCache::FeedRays(), 16u);
    EXPECT_TRUE(SurfaceRadianceCache::SdfReadsEnabled());
    EXPECT_FALSE(SurfaceRadianceCache::SdfWritesEnabled());
    EXPECT_EQ(SurfaceRadianceCache::SdfLodBias(), 2u);
    EXPECT_TRUE(SurfaceRadianceCache::DdgiEnabled());
    EXPECT_TRUE(SurfaceRadianceCache::SdfConsumersBound());
}

// ─── Stage 2: local lights and emissive at the hit (spec 2.5, 3.1; acceptance B1-B4) ─────────

TEST(SurfaceCacheStage2, LightRecordMirrorsTheShaderStructAndTheLimitsMatchTheLightingCB) {
    EXPECT_EQ(sizeof(SurfaceCacheLight), 80u);
    EXPECT_EQ(sizeof(SurfaceCacheLightGPU), 80u);
    EXPECT_EQ(offsetof(SurfaceCacheLightGPU, positionX), offsetof(SurfaceCacheLight, position));
    EXPECT_EQ(offsetof(SurfaceCacheLightGPU, radius), offsetof(SurfaceCacheLight, radius));
    EXPECT_EQ(offsetof(SurfaceCacheLightGPU, radius), 12u);
    EXPECT_EQ(offsetof(SurfaceCacheLightGPU, colorR), offsetof(SurfaceCacheLight, color));
    EXPECT_EQ(offsetof(SurfaceCacheLightGPU, type), offsetof(SurfaceCacheLight, type));
    EXPECT_EQ(offsetof(SurfaceCacheLightGPU, type), 28u);
    EXPECT_EQ(offsetof(SurfaceCacheLightGPU, directionX), offsetof(SurfaceCacheLight, direction));
    EXPECT_EQ(offsetof(SurfaceCacheLightGPU, cosOuterAngle), offsetof(SurfaceCacheLight, cosOuterAngle));
    EXPECT_EQ(offsetof(SurfaceCacheLightGPU, cosOuterAngle), 44u);
    EXPECT_EQ(offsetof(SurfaceCacheLightGPU, rightX), offsetof(SurfaceCacheLight, right));
    EXPECT_EQ(offsetof(SurfaceCacheLightGPU, invCosConeDifference), offsetof(SurfaceCacheLight, invCosConeDifference));
    EXPECT_EQ(offsetof(SurfaceCacheLightGPU, invCosConeDifference), 60u);
    EXPECT_EQ(offsetof(SurfaceCacheLightGPU, upX), offsetof(SurfaceCacheLight, up));
    EXPECT_EQ(offsetof(SurfaceCacheLightGPU, falloffExponent), offsetof(SurfaceCacheLight, falloffExponent));
    EXPECT_EQ(offsetof(SurfaceCacheLightGPU, falloffExponent), 76u);
    // Every light the LightingCB and the rect buffer can carry fits the per-frame record buffer.
    EXPECT_EQ(kSurfaceCacheMaxLights, kMaxPointLights + kMaxSpotLights + kMaxRectLights);
    EXPECT_EQ(kSurfaceCacheMaxLocalLights, 16u);
    // The record's type codes and flag bits are disjoint, and the flags mirror the light data's.
    EXPECT_EQ(kSurfaceCacheLightTypeMask & kSurfaceCacheLightInverseSquared, 0u);
    EXPECT_EQ(kSurfaceCacheLightTypeMask & kSurfaceCacheLightTwoSided, 0u);
    EXPECT_EQ(kSurfaceCacheLightInverseSquared & kSurfaceCacheLightTwoSided, 0u);
    EXPECT_LE(kSurfaceCacheLightTypePoint, kSurfaceCacheLightTypeMask);
    EXPECT_LE(kSurfaceCacheLightTypeSpot, kSurfaceCacheLightTypeMask);
    EXPECT_LE(kSurfaceCacheLightTypeRect, kSurfaceCacheLightTypeMask);
    EXPECT_NE(kSurfaceCacheLightTypePoint, kSurfaceCacheLightTypeSpot);
    EXPECT_NE(kSurfaceCacheLightTypeSpot, kSurfaceCacheLightTypeRect);
    // The emissive flag of the params record is its own bit, outside the debug field.
    EXPECT_EQ(kSurfaceCacheFlagEmissive, 64u);
    EXPECT_EQ(kSurfaceCacheFlagEmissive & (kSurfaceCacheFlagDebugMask | kSurfaceCacheFlagDirectInline
                                           | kSurfaceCacheFlagJitter | kSurfaceCacheFlagSdfWrites
                                           | kSurfaceCacheFlagStats), 0u);
}

TEST(SurfaceCacheStage2, LightAttenuationIsTheDeferredLightingsWithItsRangeTest) {
    // Windowed inverse square (deferred_lighting_ps.hlsl LocalLightAttenuation): (1 - f^2)^2 / d^2
    // with f = d^2 / r^2 and the 0.01 floor on d^2.
    const float r = 10.0f;
    EXPECT_NEAR(SurfaceCacheLightAttenuation(4.0f, r, 0.0f, true), std::pow(1.0f - 0.04f * 0.04f, 2.0f) / 4.0f, 1e-6f);
    EXPECT_NEAR(SurfaceCacheLightAttenuation(0.0f, r, 0.0f, true), 1.0f / 0.01f, 1e-3f);        // the floor
    EXPECT_NEAR(SurfaceCacheLightAttenuation(0.001f, r, 0.0f, true),
                std::pow(1.0f - 1e-5f * 1e-5f, 2.0f) / 0.01f, 1e-3f);
    EXPECT_EQ(SurfaceCacheLightAttenuation(100.0f, r, 0.0f, true), 0.0f);                       // at the radius
    EXPECT_EQ(SurfaceCacheLightAttenuation(101.0f, r, 0.0f, true), 0.0f);                       // beyond it
    // Custom falloff (UE's RadialAttenuation): (1 - f)^e, no 1 / d^2, exponent floored at 1e-3.
    EXPECT_NEAR(SurfaceCacheLightAttenuation(25.0f, r, 2.0f, false), 0.75f * 0.75f, 1e-6f);
    EXPECT_NEAR(SurfaceCacheLightAttenuation(25.0f, r, 0.0f, false), std::pow(0.75f, 1e-3f), 1e-6f);
    EXPECT_EQ(SurfaceCacheLightAttenuation(101.0f, r, 2.0f, false), 0.0f);
    // A degenerate radius contributes nothing (the raster never binds one).
    EXPECT_EQ(SurfaceCacheLightAttenuation(1.0f, 0.0f, 1.0f, true), 0.0f);
    EXPECT_EQ(SurfaceCacheLightAttenuation(1.0f, -1.0f, 1.0f, false), 0.0f);
}

TEST(SurfaceCacheStage2, SpotConeAndRectWindowAreTheRastersSquaredRamps) {
    // ShadeSpotLight: saturate((cosA - cosOuter) * invCosConeDifference)^2.
    const float cosOuter = 0.5f, cosInner = 0.9f, invDiff = 1.0f / (cosInner - cosOuter);
    EXPECT_EQ(SurfaceCacheSpotCone(0.4f, cosOuter, invDiff), 0.0f);
    EXPECT_EQ(SurfaceCacheSpotCone(cosOuter, cosOuter, invDiff), 0.0f);
    EXPECT_NEAR(SurfaceCacheSpotCone(0.7f, cosOuter, invDiff), 0.25f, 1e-6f);
    EXPECT_NEAR(SurfaceCacheSpotCone(cosInner, cosOuter, invDiff), 1.0f, 1e-6f);
    EXPECT_EQ(SurfaceCacheSpotCone(1.0f, cosOuter, invDiff), 1.0f);
    // ShadeRectLight: saturate(1 - f^2)^2 with f = d^2 / range^2 (the 1 / d^2 lives in the form factor).
    EXPECT_EQ(SurfaceCacheRectWindow(0.0f, 100.0f), 1.0f);
    EXPECT_NEAR(SurfaceCacheRectWindow(5000.0f, 100.0f), 0.75f * 0.75f, 1e-6f);
    EXPECT_EQ(SurfaceCacheRectWindow(10000.0f, 100.0f), 0.0f);
    EXPECT_EQ(SurfaceCacheRectWindow(20000.0f, 100.0f), 0.0f);
    EXPECT_EQ(SurfaceCacheRectWindow(1.0f, 0.0f), 0.0f);                                        // range floor 1e-4
    // Relevance: Rec. 709 luminance of the unshadowed Lambert radiance.
    EXPECT_NEAR(SurfaceCacheLuminance(float3{1.0f, 1.0f, 1.0f}), 1.0f, 1e-6f);
    EXPECT_NEAR(SurfaceCacheLuminance(float3{1.0f, 0.0f, 0.0f}), 0.2126f, 1e-6f);
    EXPECT_NEAR(SurfaceCacheLuminance(float3{0.0f, 1.0f, 0.0f}), 0.7152f, 1e-6f);
    EXPECT_NEAR(SurfaceCacheLuminance(float3{0.0f, 0.0f, 1.0f}), 0.0722f, 1e-6f);
}

TEST(SurfaceCacheStage2, BuildLightsFoldsTheScalesOrdersRectPointSpotAndSkipsDeadLights) {
    auto lighting = std::make_unique<LightingCB>();
    lighting->pointLightCount = 3;
    lighting->pointLights[0].position = Vec3(1.0f, 2.0f, 3.0f);
    lighting->pointLights[0].radius = 500.0f;
    lighting->pointLights[0].color = Vec3(2.0f, 4.0f, 6.0f);
    lighting->pointLights[0].diffuseScale = 0.5f;
    lighting->pointLights[0].falloffExponent = 3.0f;
    lighting->pointLights[0].flags = kLightFlag_InverseSquared;
    lighting->pointLights[1] = lighting->pointLights[0];
    lighting->pointLights[1].color = Vec3(0.0f, 0.0f, 0.0f);                                    // dead: no colour
    lighting->pointLights[2] = lighting->pointLights[0];
    lighting->pointLights[2].diffuseScale = 0.0f;                                               // dead: no diffuse
    lighting->spotLightCount = 2;
    lighting->spotLights[0].position = Vec3(-1.0f, 0.0f, 9.0f);
    lighting->spotLights[0].radius = 300.0f;
    lighting->spotLights[0].color = Vec3(1.0f, 1.0f, 1.0f);
    lighting->spotLights[0].diffuseScale = 1.0f;
    lighting->spotLights[0].direction = Vec3(0.0f, 0.0f, -1.0f);
    lighting->spotLights[0].cosOuterAngle = 0.5f;
    lighting->spotLights[0].invCosConeDifference = 2.5f;
    lighting->spotLights[0].falloffExponent = 2.0f;
    lighting->spotLights[0].flags = 0u;                                                         // custom falloff
    lighting->spotLights[1] = lighting->spotLights[0];
    lighting->spotLights[1].radius = 0.0f;                                                      // dead: no range
    RectLightData rects[2]{};
    rects[0].position = Vec3(380.0f, 430.0f, 160.0f);
    rects[0].range = 5000.0f;
    rects[0].right = Vec3(1.0f, 0.0f, 0.0f);
    rects[0].halfWidth = 60.0f;
    rects[0].up = Vec3(0.0f, 1.0f, 0.0f);
    rects[0].halfHeight = 40.0f;
    rects[0].color = Vec3(8.0f, 8.0f, 8.0f);
    rects[0].diffuseScale = 1.0f;
    rects[0].flags = kRectLightFlag_TwoSided;
    rects[1] = rects[0];
    rects[1].halfHeight = 0.0f;                                                                 // dead: no area

    SurfaceCacheLightGPU out[8]{};
    const uint32_t n = SurfaceCacheBuildLights(out, 8, lighting.get(), rects, 2);
    ASSERT_EQ(n, 3u);
    // Rect first, with the half extents folded into the axes and the emission normal cross(right, up).
    EXPECT_EQ(out[0].type, kSurfaceCacheLightTypeRect | kSurfaceCacheLightTwoSided);
    EXPECT_FLOAT_EQ(out[0].positionZ, 160.0f);
    EXPECT_FLOAT_EQ(out[0].radius, 5000.0f);
    EXPECT_FLOAT_EQ(out[0].colorR, 8.0f);
    EXPECT_FLOAT_EQ(out[0].rightX, 60.0f);  EXPECT_FLOAT_EQ(out[0].rightY, 0.0f);
    EXPECT_FLOAT_EQ(out[0].upY, 40.0f);     EXPECT_FLOAT_EQ(out[0].upX, 0.0f);
    EXPECT_FLOAT_EQ(out[0].directionX, 0.0f);
    EXPECT_FLOAT_EQ(out[0].directionY, 0.0f);
    EXPECT_FLOAT_EQ(out[0].directionZ, 1.0f);
    // Then the live point light: colour x diffuseScale, the falloff flag and exponent carried.
    EXPECT_EQ(out[1].type, kSurfaceCacheLightTypePoint | kSurfaceCacheLightInverseSquared);
    EXPECT_FLOAT_EQ(out[1].positionX, 1.0f);
    EXPECT_FLOAT_EQ(out[1].radius, 500.0f);
    EXPECT_FLOAT_EQ(out[1].colorR, 1.0f);
    EXPECT_FLOAT_EQ(out[1].colorG, 2.0f);
    EXPECT_FLOAT_EQ(out[1].colorB, 3.0f);
    EXPECT_FLOAT_EQ(out[1].falloffExponent, 3.0f);
    // Then the live spot light with its cone scalars and axis.
    EXPECT_EQ(out[2].type, kSurfaceCacheLightTypeSpot);
    EXPECT_FLOAT_EQ(out[2].positionZ, 9.0f);
    EXPECT_FLOAT_EQ(out[2].directionZ, -1.0f);
    EXPECT_FLOAT_EQ(out[2].cosOuterAngle, 0.5f);
    EXPECT_FLOAT_EQ(out[2].invCosConeDifference, 2.5f);
    EXPECT_FLOAT_EQ(out[2].falloffExponent, 2.0f);
    EXPECT_FLOAT_EQ(out[2].colorG, 1.0f);
    // The capacity bounds the records; a missing kind is simply absent; nothing without a target.
    SurfaceCacheLightGPU one[1]{};
    EXPECT_EQ(SurfaceCacheBuildLights(one, 1, lighting.get(), rects, 2), 1u);
    EXPECT_EQ(one[0].type & kSurfaceCacheLightTypeMask, kSurfaceCacheLightTypeRect);
    EXPECT_EQ(SurfaceCacheBuildLights(out, 8, nullptr, rects, 2), 1u);
    EXPECT_EQ(SurfaceCacheBuildLights(out, 8, lighting.get(), nullptr, 0), 2u);
    EXPECT_EQ(out[0].type & kSurfaceCacheLightTypeMask, kSurfaceCacheLightTypePoint);
    EXPECT_EQ(SurfaceCacheBuildLights(out, 0, lighting.get(), rects, 2), 0u);
    EXPECT_EQ(SurfaceCacheBuildLights(nullptr, 8, lighting.get(), rects, 2), 0u);
    // Counts above the CB's limits are clamped to them (the arrays are never read past their size),
    // and every live light of a full frame fits the record buffer.
    for (uint32_t i = 0; i < kMaxPointLights; ++i) lighting->pointLights[i] = lighting->pointLights[0];
    for (uint32_t i = 0; i < kMaxSpotLights; ++i) lighting->spotLights[i] = lighting->spotLights[0];
    lighting->pointLightCount = kMaxPointLights + 7;
    lighting->spotLightCount = kMaxSpotLights + 7;
    std::vector<RectLightData> everyRect(kMaxRectLights, rects[0]);
    std::vector<SurfaceCacheLightGPU> all(kSurfaceCacheMaxLights);
    EXPECT_EQ(SurfaceCacheBuildLights(all.data(), kSurfaceCacheMaxLights, lighting.get(), everyRect.data(),
                                      kMaxRectLights + 1),
              kSurfaceCacheMaxLights);
}

TEST(SurfaceCacheStage2, MaterialTableCarriesTheEmissionBehindThePathTracersFields) {
    // 48 -> 64 bytes: the stage 1 fields keep their offsets (the path tracer reads them unchanged),
    // the emission follows, and a default entry emits nothing with no texture.
    EXPECT_EQ(sizeof(GIMeshMaterialGPU), 64u);
    EXPECT_EQ(offsetof(GIMeshMaterialGPU, tintR), 0u);
    EXPECT_EQ(offsetof(GIMeshMaterialGPU, albedoSrv), 12u);
    EXPECT_EQ(offsetof(GIMeshMaterialGPU, normalFormat), 40u);
    EXPECT_EQ(offsetof(GIMeshMaterialGPU, emissiveSrv), 44u);
    EXPECT_EQ(offsetof(GIMeshMaterialGPU, emissiveR), 48u);
    EXPECT_EQ(offsetof(GIMeshMaterialGPU, emissiveG), 52u);
    EXPECT_EQ(offsetof(GIMeshMaterialGPU, emissiveB), 56u);
    EXPECT_EQ(offsetof(GIMeshMaterialGPU, padding), 60u);
    const GIMeshMaterialGPU entry{};
    EXPECT_EQ(entry.emissiveSrv, UINT32_MAX);
    EXPECT_EQ(entry.emissiveR, 0.0f);
    EXPECT_EQ(entry.emissiveG, 0.0f);
    EXPECT_EQ(entry.emissiveB, 0.0f);
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

TEST(SurfaceCacheStage4, ScheduleVisitsEveryEntryOnceInAnyCompleteCycle) {
    constexpr uint32_t log2 = 16u;
    constexpr uint32_t entries = 1u << log2;
    for (const uint32_t slices : {1u, 3u, 16u, 63u, 64u}) {
        std::vector<uint32_t> visits(entries, 0u);
        // Start mid-cycle: a light change need not occur at slice zero.
        for (uint32_t frame = 7u; frame < 7u + slices; ++frame) {
            const uint32_t slice = SurfaceCacheSliceIndex(frame, slices);
            const uint32_t first = SurfaceCacheSliceFirst(log2, slice, slices);
            const uint32_t count = SurfaceCacheSliceCount(log2, slice, slices);
            ASSERT_LE(first + count, entries);
            for (uint32_t i = first; i < first + count; ++i) ++visits[i];
        }
        for (const auto count : visits) ASSERT_EQ(count, 1u);
    }
}

TEST(SurfaceCacheStage4, LightingChangeReplacesDirectLayerWithoutEviction) {
    const auto decision = SurfaceCacheDecideInvalidation(false, 0u, false, true, true);
    EXPECT_FALSE(decision.clear);
    EXPECT_TRUE(decision.relight);
    EXPECT_EQ(decision.regionCount, 0u);
    const bool replaces = SurfaceCacheRelightReplaces(3u, 4u);
    ASSERT_TRUE(replaces);
    EXPECT_FLOAT_EQ(SurfaceCacheRelightSun(replaces, 10.0f, 2.0f, 32u), 2.0f);
    EXPECT_FALSE(SurfaceCacheRelightReplaces(4u, 4u));
    EXPECT_EQ(SurfaceCacheRelightVisits(replaces, 8u), 1u);
}

TEST(SurfaceCacheStage4, CosineGatherPreservesConstantEnvironmentEnergy) {
    constexpr float albedo = 0.6f;
    constexpr float radiance = 3.0f;
    for (uint32_t rays = 1u; rays <= 16u; ++rays) {
        const float gathered = SurfaceCacheGatherIndirect(albedo, radiance * rays, rays);
        EXPECT_NEAR(gathered, albedo * radiance, 1e-6f);
        EXPECT_LE(gathered, radiance);
        // A second cosine or 1/pi after cosine-PDF cancellation is a biased negative control.
        EXPECT_GT(std::fabs(gathered - gathered / kSurfaceCachePi), 1.0f);
    }
    EXPECT_FLOAT_EQ(SurfaceCacheGatherIndirect(albedo, radiance, 0u), 0.0f);
    EXPECT_FLOAT_EQ(SurfaceCacheGatherIndirect(0.0f, radiance, 1u), 0.0f);
    EXPECT_FLOAT_EQ(SurfaceCacheIndirectBlend(100.0f, 2.0f, SurfaceCacheNextVisits(0u, 8u)), 2.0f);
}

TEST(SurfaceCacheStage4, FeaturesAreOptInAndRootBlockFitsBudget) {
    EXPECT_FALSE(SurfaceRadianceCache::RelightEnabled());
    EXPECT_FALSE(SurfaceRadianceCache::MultiBounceEnabled());
    EXPECT_EQ(SurfaceRadianceCache::RelightSlices(), 16u);
    EXPECT_EQ(sizeof(SurfaceCacheRelightRootConstants), 48u);
    EXPECT_EQ(offsetof(SurfaceCacheRelightRootConstants, skyCubemapSrv), 32u);
    EXPECT_LE(kSurfaceCacheRelightRootDwords, 20u);
    EXPECT_EQ(SurfaceRadianceCache::Bounces(), 4u);
}

TEST(SurfaceCacheStage4, JacobiTransportIsBoundedConvergentAndOrderIndependent) {
    // Two mutually visible diffuse patches. The shader's cosine average has nonnegative
    // weights summing to one, so this is its worst case (no ray escapes).
    const float source[2] = {1.0f, 0.2f};
    const float rho[2] = {0.8f, 0.6f};
    const float exact[2] = {(source[0] + rho[0] * source[1]) / (1.0f - rho[0] * rho[1]),
                           (source[1] + rho[1] * source[0]) / (1.0f - rho[0] * rho[1])};
    float previous[2] = {source[0], source[1]};
    float lastError = 10.0f;
    for (uint32_t bounce = 1u; bounce < 64u; ++bounce) {
        float next[2], reverse[2];
        for (uint32_t i = 0u; i < 2u; ++i)
            next[i] = SurfaceCacheTransportStep(source[i], rho[i], previous[1u - i] * 4.0f, 4u);
        for (int i = 1; i >= 0; --i)
            reverse[i] = SurfaceCacheTransportStep(source[i], rho[i], previous[1 - i] * 4.0f, 4u);
        const float error = std::fabs(next[0] - exact[0]) + std::fabs(next[1] - exact[1]);
        EXPECT_LE(error, lastError + 1e-6f);
        lastError = error;
        for (uint32_t i = 0u; i < 2u; ++i) {
            EXPECT_FLOAT_EQ(next[i], reverse[i]);
            EXPECT_LE(next[i], 1.0f / (1.0f - kSurfaceCacheTransportGain));
            previous[i] = next[i];
        }
    }
    EXPECT_LT(lastError, 1e-5f);
    EXPECT_LT(SurfaceCacheTransportReflectance(1.0f), 1.0f);
    // Negative control: an unbounded SH return gain can defeat rho < 1. This is why
    // cold hits use direct/emission only rather than an unproven scaled SH fallback.
    float divergent = 1.0f;
    for (uint32_t bounce = 0u; bounce < 64u; ++bounce)
        divergent = SurfaceCacheTransportStep(1.0f, 0.8f, divergent * 1.5f, 1u);
    EXPECT_GT(divergent, 100000.0f);
}

TEST(SurfaceCacheStage4, FiniteDepthTransportMatchesTruncatedNeumannSeriesAndStaysBounded) {
    // The GPU schedule on a closed 5-patch box (a Cornell-like room, every ray of a patch lands
    // on another patch or escapes to a black sky): Base L[0] = source, Bounces - 1 Jacobi gathers
    // L[n+1] = source + rho x (F L[n]) through SurfaceCacheTransportStep, then the per-visit
    // temporal blend of Finish. F is row-substochastic (row sum = 1 - escape), rho <= 0.98.
    constexpr uint32_t kN = 5u;
    const float F[kN][kN] = {{0.00f, 0.30f, 0.20f, 0.20f, 0.25f},
                             {0.30f, 0.00f, 0.20f, 0.20f, 0.25f},
                             {0.25f, 0.25f, 0.00f, 0.15f, 0.30f},
                             {0.25f, 0.25f, 0.15f, 0.00f, 0.30f},
                             {0.20f, 0.20f, 0.25f, 0.25f, 0.00f}};
    const float rho[kN] = {0.65f, 0.65f, 0.65f, 0.06f, 1.0f};   // the last one clamps to 0.98
    const float source[kN] = {1.2f, 0.0f, 0.3f, 0.0f, 0.0f};
    float sMax = 0.0f;
    for (const float s : source) sMax = std::max(sMax, s);
    const auto solve = [&](uint32_t bounces, float gain, float* out) {
        float prev[kN], next[kN];
        for (uint32_t i = 0u; i < kN; ++i) prev[i] = out[i] = source[i];
        for (uint32_t b = 1u; b < bounces; ++b) {
            for (uint32_t i = 0u; i < kN; ++i) {
                float raySum = 0.0f;   // one "ray" carrying the exact expected value of the cosine mean
                for (uint32_t j = 0u; j < kN; ++j) raySum += F[i][j] * prev[j] * gain;
                next[i] = SurfaceCacheTransportStep(source[i], rho[i], raySum, 1u);
            }
            for (uint32_t i = 0u; i < kN; ++i) prev[i] = out[i] = next[i];
        }
    };
    // Monotone in the depth, bounded by ||source|| / (1 - 0.98), converging (geometric rate
    // <= max rho x max row sum) to the infinite-bounce solution.
    float previous[kN];
    solve(1u, 1.0f, previous);
    for (uint32_t i = 0u; i < kN; ++i) EXPECT_FLOAT_EQ(previous[i], source[i]);   // depth 1 = direct only
    float deltaPrev = 1e9f;
    for (uint32_t bounces = 2u; bounces <= 64u; ++bounces) {
        float cur[kN];
        solve(bounces, 1.0f, cur);
        float delta = 0.0f;
        for (uint32_t i = 0u; i < kN; ++i) {
            EXPECT_GE(cur[i] + 1e-6f, previous[i]);                       // more vertices never remove energy
            EXPECT_LE(cur[i], sMax / (1.0f - kSurfaceCacheTransportGain)); // the substochastic bound
            delta = std::max(delta, cur[i] - previous[i]);
            previous[i] = cur[i];
        }
        if (bounces > 2u) EXPECT_LE(delta, deltaPrev * 0.98f + 1e-6f);   // the series terms shrink by the gain
        deltaPrev = delta;
    }
    EXPECT_LT(deltaPrev, 1e-3f);
    // The temporal blend of Finish averages finite-depth estimates: from a cold or a stale
    // `other`, the per-visit running mean / EMA converges to the same depth-4 value (no feedback
    // across frames: every frame restarts the iteration at L[0]).
    float depth4[kN];
    solve(4u, 1.0f, depth4);
    for (const float stale : {0.0f, 50.0f}) {
        float other = stale;
        uint32_t visits = 0u;
        for (uint32_t frame = 0u; frame < 200u; ++frame) {
            visits = SurfaceCacheNextVisits(visits, 8u);
            other = SurfaceCacheIndirectBlend(other, depth4[0], visits);
            EXPECT_LE(other, std::max(stale, depth4[0]) + 1e-5f);
        }
        EXPECT_NEAR(other, depth4[0], 1e-4f);
    }
    // Negative control: the same schedule with an SH-style return gain of 2 on the gathered
    // radiance (rho x row sum x gain up to 1.76: spectral radius > 1) is not substochastic and
    // diverges with the depth - the reason cold hits never fall back to the probe SH.
    float divergent[kN];
    solve(64u, 2.0f, divergent);
    EXPECT_GT(divergent[4], 1e3f);
}
