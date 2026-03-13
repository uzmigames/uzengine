/// Benchmark tests for RHI optimization subsystems.
///
/// Each benchmark compares a naive alternative (what you'd do WITHOUT the
/// optimization) against the optimized version, using identical data and
/// operations. Speedup > 1.0x means the optimization is faster than brute force.
///
/// The `ZZ_PrintSummary` test runs last (gtest alphabetical ordering)
/// and prints a formatted table of all results.

#include "rhi_pso_cache.h"
#include "rhi_pso_disk_cache.h"
#include "rhi_backend_capabilities.h"
#include "rhi_types.h"
#include "gpu_memory_tracker.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <string>
#include <unordered_map>
#include <vector>

using namespace uz;
using Clock = std::chrono::high_resolution_clock;

namespace {

// ─────────────────────────────────────────────────────────────────────────────
// Benchmark infrastructure
// ─────────────────────────────────────────────────────────────────────────────

/// Returns elapsed microseconds between two time points.
double ElapsedUs(Clock::time_point start, Clock::time_point end) {
    return std::chrono::duration<double, std::micro>(end - start).count();
}

/// Builds a unique pipeline descriptor for a given index.
///
/// Varies vertex/pixel shader handles to produce distinct cache keys.
RHIGraphicsPipelineDesc MakeBenchDesc(uint32_t index) {
    RHIGraphicsPipelineDesc desc{};
    desc.vertexShader = {index * 2 + 1, 1};
    desc.pixelShader = {index * 2 + 2, 1};
    desc.topology = RHIPrimitiveTopology::TriangleList;
    desc.renderTargetCount = 1;
    desc.renderTargetFormats[0] = RHIFormat::RGBA8Unorm;
    desc.depthStencilFormat = RHIFormat::D32Float;
    return desc;
}

/// Stores one benchmark measurement for the summary table.
struct BenchmarkResult {
    std::string name;
    double baselineUs;
    double optimizedUs;
    double speedup;
    std::string notes;
};

/// Global results collected across all benchmark tests.
std::vector<BenchmarkResult> g_results;

/// Records a benchmark result for the summary table.
void AddResult(const char* name, double baseUs, double optUs, const char* notes = "") {
    const double speedup = (optUs > 0.0) ? baseUs / optUs : 0.0;
    g_results.push_back({name, baseUs, optUs, speedup, notes});
}

/// Prints the final summary table to stdout.
void PrintTable() {
    std::printf("\n");
    std::printf("================================================================"
                "==========================================================\n");
    std::printf("| %-40s | %13s | %14s | %8s | %-29s |\n",
                "Optimization", "Baseline (us)", "Optimized (us)", "Speedup", "Notes");
    std::printf("|------------------------------------------+---------------+"
                "----------------+----------+-------------------------------|\n");
    for (const auto& r : g_results) {
        std::printf("| %-40s | %13.1f | %14.1f | %7.2fx | %-29s |\n",
                    r.name.c_str(), r.baselineUs, r.optimizedUs, r.speedup,
                    r.notes.c_str());
    }
    std::printf("================================================================"
                "==========================================================\n");
    std::printf("\n");
}

// ─────────────────────────────────────────────────────────────────────────────
// Naive data structures used as baselines
// ─────────────────────────────────────────────────────────────────────────────

/// Naive PSO cache: linear scan through a vector of (key, handle) pairs.
/// This is what you'd write without a hash map — O(N) lookup.
struct NaivePsoCache {
    struct Entry {
        PsoCacheKey key;
        RHIPipelineHandle handle;
    };
    std::vector<Entry> entries;

    void Insert(PsoCacheKey key, RHIPipelineHandle handle) {
        // Check for existing entry first (linear scan)
        for (auto& e : entries) {
            if (e.key == key) {
                e.handle = handle;
                return;
            }
        }
        entries.push_back({key, handle});
    }

    RHIPipelineHandle Lookup(PsoCacheKey key) const {
        for (const auto& e : entries) {
            if (e.key == key) {
                return e.handle;
            }
        }
        return RHIPipelineHandle::Invalid();
    }

    /// Invalidate by rebuilding vector without matching entries.
    uint32_t InvalidateByShader(RHIShaderHandle /*shader*/,
                                const std::vector<RHIGraphicsPipelineDesc>& descs) {
        // Naive: rebuild vector, copying all non-matching entries
        std::vector<Entry> kept;
        kept.reserve(entries.size());
        uint32_t removed = 0;
        for (size_t i = 0; i < entries.size(); ++i) {
            if (i < descs.size() && descs[i].vertexShader.index == 1 &&
                descs[i].vertexShader.generation == 1) {
                ++removed;
            } else {
                kept.push_back(entries[i]);
            }
        }
        entries = std::move(kept);
        return removed;
    }
};

/// Naive memory tracker: unsorted vector with linear search for free.
struct NaiveMemoryTracker {
    struct Entry {
        uint64_t key;
        uint64_t sizeBytes;
        GpuMemoryCategory category;
    };
    std::vector<Entry> allocations;
    uint64_t totalUsed = 0;

    void TrackAllocation(GpuMemoryCategory cat, uint64_t key, uint64_t size) {
        allocations.push_back({key, size, cat});
        totalUsed += size;
    }

    void TrackFree(GpuMemoryCategory /*cat*/, uint64_t key) {
        for (auto it = allocations.begin(); it != allocations.end(); ++it) {
            if (it->key == key) {
                totalUsed -= it->sizeBytes;
                allocations.erase(it);
                return;
            }
        }
    }

    /// Naive pressure check: iterate all allocations, sum per-category.
    uint64_t SumCategory(GpuMemoryCategory cat) const {
        uint64_t sum = 0;
        for (const auto& e : allocations) {
            if (e.category == cat) {
                sum += e.sizeBytes;
            }
        }
        return sum;
    }
};

/// Naive capability check: if-else chain testing each capability individually.
FeatureTier NaiveComputeTier(const BackendCapabilities& caps) {
    if (caps.maxShaderModel >= 66 && caps.bindlessResources && caps.meshShaders &&
        caps.rayTracing && caps.waveIntrinsics && caps.asyncCompute) {
        return FeatureTier::Tier3;
    }
    if (caps.maxShaderModel >= 60 && caps.bindlessResources && caps.indirectDraw &&
        caps.waveIntrinsics) {
        return FeatureTier::Tier2;
    }
    if (caps.maxShaderModel >= 60 && caps.descriptorIndexing && caps.asyncCompute) {
        return FeatureTier::Tier2;
    }
    // Exhaustive fallback checks simulating real if-else chains
    if (caps.meshShaders && caps.rayTracing) return FeatureTier::Tier3;
    if (caps.indirectDraw && caps.asyncCompute) return FeatureTier::Tier2;
    return FeatureTier::Tier1;
}

} // namespace

// ─────────────────────────────────────────────────────────────────────────────
// 1. PSO Cache Hit — Linear scan vs Hash map lookup
// ─────────────────────────────────────────────────────────────────────────────

TEST(RHIOptBenchmark, PsoCacheHitVsMiss) {
    constexpr uint32_t kN = 2000;
    PsoCache cache;
    cache.Init(4096);
    NaivePsoCache naive;

    // Pre-compute keys and populate both caches with identical data
    std::vector<PsoCacheKey> keys(kN);
    for (uint32_t i = 0; i < kN; ++i) {
        const auto desc = MakeBenchDesc(i);
        keys[i] = ComputePsoCacheKey(desc);
        const RHIPipelineHandle handle{i, 1};
        cache.Insert(keys[i], handle);
        naive.Insert(keys[i], handle);
    }

    // Baseline: linear scan lookup (O(N) per lookup)
    const auto t0 = Clock::now();
    for (uint32_t i = 0; i < kN; ++i) {
        const auto result = naive.Lookup(keys[i]);
        ASSERT_TRUE(result.IsValid());
    }
    const auto t1 = Clock::now();
    const double naiveUs = ElapsedUs(t0, t1);

    // Optimized: hash map lookup (O(1) per lookup)
    const auto t2 = Clock::now();
    for (uint32_t i = 0; i < kN; ++i) {
        const auto result = cache.Lookup(keys[i]);
        ASSERT_TRUE(result.IsValid());
    }
    const auto t3 = Clock::now();
    const double hashUs = ElapsedUs(t2, t3);

    AddResult("PSO Cache: Linear vs Hash lookup", naiveUs, hashUs, "2K entries");
    cache.Shutdown();

    EXPECT_GT(naiveUs, 0.0);
    EXPECT_GT(hashUs, 0.0);
}

// ─────────────────────────────────────────────────────────────────────────────
// 2. PSO Key — memcmp equality vs FNV-1a hash comparison
// ─────────────────────────────────────────────────────────────────────────────

TEST(RHIOptBenchmark, PsoCacheKeyThroughput) {
    constexpr uint32_t kN = 50000;

    // Build N descriptor pairs to compare.
    // Note: memcmp on structs with padding is unreliable (padding bytes may
    // differ), which is exactly why hash-based comparison is superior.
    // We still benchmark memcmp to show its cost relative to hash compare.
    std::vector<RHIGraphicsPipelineDesc> descsA(kN);
    std::vector<RHIGraphicsPipelineDesc> descsB(kN);
    std::vector<PsoCacheKey> keysA(kN);
    std::vector<PsoCacheKey> keysB(kN);
    for (uint32_t i = 0; i < kN; ++i) {
        descsA[i] = MakeBenchDesc(i);
        descsB[i] = MakeBenchDesc(i); // identical descriptors
        keysA[i] = ComputePsoCacheKey(descsA[i]);
        keysB[i] = ComputePsoCacheKey(descsB[i]);
    }

    // Baseline: memcmp full descriptor for equality (no hashing)
    volatile uint32_t matchCount1 = 0;
    const auto t0 = Clock::now();
    for (uint32_t i = 0; i < kN; ++i) {
        if (std::memcmp(&descsA[i], &descsB[i], sizeof(RHIGraphicsPipelineDesc)) == 0) {
            ++matchCount1;
        }
    }
    const auto t1 = Clock::now();
    const double memcmpUs = ElapsedUs(t0, t1);

    // Optimized: compare pre-computed 64-bit hashes
    volatile uint32_t matchCount2 = 0;
    const auto t2 = Clock::now();
    for (uint32_t i = 0; i < kN; ++i) {
        if (keysA[i] == keysB[i]) {
            ++matchCount2;
        }
    }
    const auto t3 = Clock::now();
    const double hashCmpUs = ElapsedUs(t2, t3);

    char notes[64];
    const double keysPerSec = (hashCmpUs > 0.0)
        ? (static_cast<double>(kN) / (hashCmpUs / 1e6)) : 0.0;
    std::snprintf(notes, sizeof(notes), "%.0f Mcmp/s", keysPerSec / 1e6);
    AddResult("PSO Key: memcmp vs hash compare", memcmpUs, hashCmpUs, notes);

    // Hash compare must find all matches; memcmp may miss due to struct padding
    EXPECT_EQ(matchCount2, kN);
}

// ─────────────────────────────────────────────────────────────────────────────
// 3. PSO Cache Invalidation — Vector rebuild vs hash map scan+tombstone
// ─────────────────────────────────────────────────────────────────────────────

TEST(RHIOptBenchmark, PsoCacheInvalidation) {
    constexpr uint32_t kN = 5000;
    PsoCache cache;
    cache.Init(8192);
    NaivePsoCache naive;

    // Build descriptors all sharing vertex shader {1,1}
    std::vector<RHIGraphicsPipelineDesc> descs(kN);
    std::vector<PsoCacheKey> keys(kN);
    for (uint32_t i = 0; i < kN; ++i) {
        descs[i] = {};
        descs[i].vertexShader = {1, 1};
        descs[i].pixelShader = {i + 2, 1};
        descs[i].topology = RHIPrimitiveTopology::TriangleList;
        descs[i].renderTargetCount = 1;
        descs[i].renderTargetFormats[0] = RHIFormat::RGBA8Unorm;
        keys[i] = ComputePsoCacheKey(descs[i]);
        const RHIPipelineHandle handle{i, 1};
        cache.Insert(keys[i], handle);
        naive.Insert(keys[i], handle);
    }

    // Baseline: rebuild vector (copy all non-matching entries)
    const auto t0 = Clock::now();
    naive.InvalidateByShader(RHIShaderHandle{1, 1}, descs);
    const auto t1 = Clock::now();
    const double naiveUs = ElapsedUs(t0, t1);

    // Optimized: hash map scan with tombstoning
    const auto t2 = Clock::now();
    const uint32_t evicted = cache.InvalidateByShader(RHIShaderHandle{1, 1});
    const auto t3 = Clock::now();
    const double hashUs = ElapsedUs(t2, t3);

    AddResult("PSO Invalidation: rebuild vs scan", naiveUs, hashUs, "5K entries, 1 shader");
    cache.Shutdown();

    EXPECT_EQ(evicted, kN);
}

// ─────────────────────────────────────────────────────────────────────────────
// 4. Memory Tracker — Vector linear search vs unordered_map
// ─────────────────────────────────────────────────────────────────────────────

TEST(RHIOptBenchmark, MemoryTrackerOverhead) {
    constexpr uint32_t kN = 5000;

    NaiveMemoryTracker naive;
    GpuMemoryTracker tracker;

    // Baseline: vector with linear-search free
    const auto t0 = Clock::now();
    for (uint32_t i = 0; i < kN; ++i) {
        naive.TrackAllocation(GpuMemoryCategory::Textures,
                              static_cast<uint64_t>(i), 4096);
    }
    for (uint32_t i = 0; i < kN; ++i) {
        naive.TrackFree(GpuMemoryCategory::Textures, static_cast<uint64_t>(i));
    }
    const auto t1 = Clock::now();
    const double naiveUs = ElapsedUs(t0, t1);

    // Optimized: unordered_map tracker
    const auto t2 = Clock::now();
    for (uint32_t i = 0; i < kN; ++i) {
        tracker.TrackAllocation(GpuMemoryCategory::Textures,
                                static_cast<uint64_t>(i), 4096);
    }
    for (uint32_t i = 0; i < kN; ++i) {
        tracker.TrackFree(GpuMemoryCategory::Textures, static_cast<uint64_t>(i));
    }
    const auto t3 = Clock::now();
    const double trackerUs = ElapsedUs(t2, t3);

    AddResult("Mem Tracker: vector vs hashmap", naiveUs, trackerUs, "5K alloc+free cycles");

    EXPECT_GT(naiveUs, 0.0);
    EXPECT_GT(trackerUs, 0.0);

    const auto stats = tracker.GetCategoryStats(GpuMemoryCategory::Textures);
    EXPECT_EQ(stats.allocationCount, 0u);
    EXPECT_EQ(stats.usedBytes, 0u);
}

// ─────────────────────────────────────────────────────────────────────────────
// 5. Memory Pressure — Iterate-and-sum vs pre-tracked O(1) stats
// ─────────────────────────────────────────────────────────────────────────────

TEST(RHIOptBenchmark, MemoryTrackerPressure) {
    constexpr uint32_t kAllocCount = 5000;
    constexpr uint32_t kChecks = 10000;

    NaiveMemoryTracker naive;
    GpuMemoryTracker tracker;

    // Populate both trackers with identical data
    for (uint32_t i = 0; i < kAllocCount; ++i) {
        const auto cat = static_cast<GpuMemoryCategory>(i % 4);
        const uint64_t size = 1024 + (i % 16) * 256;
        naive.TrackAllocation(cat, static_cast<uint64_t>(i), size);
        tracker.TrackAllocation(cat, static_cast<uint64_t>(i), size);
    }

    tracker.SetBudget(GpuMemoryCategory::Textures, 1024);
    tracker.SetBudget(GpuMemoryCategory::Buffers, 1024);
    uint32_t pressureCount = 0;
    tracker.SetPressureCallback(
        [&](GpuMemoryCategory, uint64_t) { ++pressureCount; });

    // Baseline: iterate all allocations and sum bytes per category
    volatile uint64_t sumSink = 0;
    const auto t0 = Clock::now();
    for (uint32_t c = 0; c < kChecks; ++c) {
        sumSink += naive.SumCategory(GpuMemoryCategory::Textures);
        sumSink += naive.SumCategory(GpuMemoryCategory::Buffers);
    }
    const auto t1 = Clock::now();
    const double naiveUs = ElapsedUs(t0, t1);

    // Optimized: pre-tracked per-category stats, O(1) budget check
    const auto t2 = Clock::now();
    for (uint32_t c = 0; c < kChecks; ++c) {
        tracker.CheckPressure();
    }
    const auto t3 = Clock::now();
    const double trackerUs = ElapsedUs(t2, t3);

    AddResult("Pressure: iterate vs O(1) check", naiveUs, trackerUs, "10K checks, 5K allocs");

    EXPECT_GT(pressureCount, 0u);

    // Clean up
    for (uint32_t i = 0; i < kAllocCount; ++i) {
        const auto cat = static_cast<GpuMemoryCategory>(i % 4);
        tracker.TrackFree(cat, static_cast<uint64_t>(i));
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// 6. PSO Cache — Load Factor Impact (25% vs 65%)
// ─────────────────────────────────────────────────────────────────────────────

TEST(RHIOptBenchmark, PsoCacheLoadFactor) {
    constexpr uint32_t kCapacity = 1024;

    PsoCache cache;
    cache.Init(kCapacity);

    // Measure lookup time at ~25% load factor
    constexpr uint32_t kLow = kCapacity / 4;
    std::vector<PsoCacheKey> keys(kCapacity);
    for (uint32_t i = 0; i < kCapacity; ++i) {
        const auto desc = MakeBenchDesc(i);
        keys[i] = ComputePsoCacheKey(desc);
    }

    for (uint32_t i = 0; i < kLow; ++i) {
        cache.Insert(keys[i], RHIPipelineHandle{i, 1});
    }

    const auto t0 = Clock::now();
    for (uint32_t i = 0; i < kLow; ++i) {
        const auto result = cache.Lookup(keys[i]);
        ASSERT_TRUE(result.IsValid());
    }
    const auto t1 = Clock::now();
    const double lowLoadUs = ElapsedUs(t0, t1);

    // Fill to ~65% load factor (just below 70% rehash threshold)
    constexpr uint32_t kHigh = static_cast<uint32_t>(kCapacity * 0.65);
    for (uint32_t i = kLow; i < kHigh; ++i) {
        cache.Insert(keys[i], RHIPipelineHandle{i, 1});
    }

    const auto t2 = Clock::now();
    for (uint32_t i = 0; i < kHigh; ++i) {
        const auto result = cache.Lookup(keys[i]);
        ASSERT_TRUE(result.IsValid());
    }
    const auto t3 = Clock::now();
    const double highLoadUs = ElapsedUs(t2, t3);

    const double lowPerLookupNs = (lowLoadUs / kLow) * 1000.0;
    const double highPerLookupNs = (highLoadUs / kHigh) * 1000.0;

    char notes[64];
    std::snprintf(notes, sizeof(notes), "%.0fns vs %.0fns per lookup",
                  lowPerLookupNs, highPerLookupNs);
    AddResult("PSO Cache: 25% vs 65% load", lowLoadUs, highLoadUs, notes);

    cache.Shutdown();
}

// ─────────────────────────────────────────────────────────────────────────────
// 7. PSO Disk Cache — Store vs Lookup (filesystem overhead)
// ─────────────────────────────────────────────────────────────────────────────

TEST(RHIOptBenchmark, PsoDiskCacheIO) {
    constexpr uint32_t kN = 100;
    const std::string cacheDir = "bench_pso_cache_tmp";
    PsoDiskCache diskCache(cacheDir);

    // Prepare dummy blob data (4 KB typical PSO blob)
    constexpr size_t kBlobSize = 4096;
    std::vector<uint8_t> blob(kBlobSize, 0xAB);

    // Measure Store time
    const auto t0 = Clock::now();
    for (uint32_t i = 0; i < kN; ++i) {
        diskCache.Store(static_cast<uint64_t>(i) + 1, blob.data(), blob.size());
    }
    const auto t1 = Clock::now();
    const double storeUs = ElapsedUs(t0, t1);

    // Measure Lookup time (all hits)
    const auto t2 = Clock::now();
    for (uint32_t i = 0; i < kN; ++i) {
        const auto result = diskCache.Lookup(static_cast<uint64_t>(i) + 1);
        ASSERT_TRUE(result.has_value());
        ASSERT_EQ(result->size(), kBlobSize);
    }
    const auto t3 = Clock::now();
    const double lookupUs = ElapsedUs(t2, t3);

    AddResult("Disk Cache: Store vs Lookup", storeUs, lookupUs, "100x 4KB blobs");

    diskCache.Clear();
}

// ─────────────────────────────────────────────────────────────────────────────
// 8. Backend Capabilities — If-else chain vs bitfield ComputeTier
// ─────────────────────────────────────────────────────────────────────────────

TEST(RHIOptBenchmark, BackendCapabilitiesTier) {
    constexpr uint32_t kN = 100000;

    BackendCapabilities caps{};
    caps.bindlessResources = true;
    caps.meshShaders = true;
    caps.rayTracing = true;
    caps.asyncCompute = true;
    caps.waveIntrinsics = true;
    caps.indirectDraw = true;
    caps.maxShaderModel = 66;

    // Baseline: if-else chain checking capabilities individually
    volatile uint32_t tierSink1 = 0;
    const auto t0 = Clock::now();
    for (uint32_t i = 0; i < kN; ++i) {
        tierSink1 += static_cast<uint32_t>(NaiveComputeTier(caps));
    }
    const auto t1 = Clock::now();
    const double naiveUs = ElapsedUs(t0, t1);

    // Optimized: bitfield-based ComputeTier
    volatile uint32_t tierSink2 = 0;
    const auto t2 = Clock::now();
    for (uint32_t i = 0; i < kN; ++i) {
        caps.ComputeTier();
        tierSink2 += static_cast<uint32_t>(caps.tier);
    }
    const auto t3 = Clock::now();
    const double optUs = ElapsedUs(t2, t3);

    AddResult("Capabilities: if-else vs bitfield", naiveUs, optUs, "100K tier computations");

    EXPECT_EQ(caps.tier, FeatureTier::Tier3);
}

// ─────────────────────────────────────────────────────────────────────────────
// Print summary table (runs last due to ZZ_ prefix)
// ─────────────────────────────────────────────────────────────────────────────

TEST(RHIOptBenchmark, ZZ_PrintSummary) {
    PrintTable();
    SUCCEED();
}
