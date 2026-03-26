/// Decal System Demo -- Headless bullet-hole and blend-mode showcase.
///
/// Demonstrates the UzEngine decal system through four phases:
///
/// **Phase 1 -- Asset Loading**
///   Loads five .uzdecal assets from inline JSON: one bullet hole (Opaque)
///   and four blend-mode showcase assets (Opaque, Additive, Multiply, Screen).
///   Validates all fields via DecalAsset::LoadFromString.
///
/// **Phase 2 -- Pool Exhaustion & Recovery**
///   Exercises DecalPool directly: acquire all 10 slots, confirm exhaustion
///   returns nullptr, return half, re-acquire, then Clear to full capacity.
///
/// **Phase 3 -- Atlas Shelf Packing**
///   Calls DecalAtlas::AddEntry with 12 textures (128–1024px), reports UV
///   coordinates and occupancy, and validates HasSpace / Reset.
///
/// **Phase 4 -- Full Simulation (15 seconds)**
///   - Initializes DecalManager with pool_size=1000
///   - Phase 4A: Spawns 120 bullet-hole decals on five axis-aligned walls
///     (north, south, east, west, ceiling) using random surface scatter.
///   - Phase 4B: Blend-mode showcase -- 4 sets of 25 decals (5×5 grid on
///     the north wall) for Opaque, Additive, Multiply, Screen.
///   - Runs 900 frames (15 s @ 60 FPS). Decals fade linearly over 10 s and
///     expire, returning their slots to the pool automatically.
///   - Prints performance overlay every 60 frames: active count, pool stats,
///     atlas occupancy, and wall-clock frame time.
///
/// This is a headless demo (no window, no GPU). The mock RHI device satisfies
/// DecalManager without touching real hardware.
///
/// # Build
///   cd demos/decals
///   zig build
///   ./zig-out/bin/decal_demo
///
/// # Expected Output
///   All four phases print validation [OK]/[FAIL] lines.
///   Final line: "[PASS] Decal system demo completed successfully."

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

// ─────────────────────────────────────────────────────────────────────────────
// Inline mock RHI device -- avoids dependency on real GPU infrastructure
// ─────────────────────────────────────────────────────────────────────────────

#include "engine/rhi/rhi.h"

namespace {

/// Minimal mock device for headless demos. Returns valid dummy handles so
/// DecalManager::Initialize succeeds without any GPU hardware present.
class DemoMockDevice final : public uz::IRHIDevice {
public:
    uz::RHIBufferHandle CreateBuffer(const uz::RHIBufferDesc&) override {
        return {++idx_, 1u};
    }
    void DestroyBuffer(uz::RHIBufferHandle) override {}

    uz::RHITextureHandle  CreateTexture(const uz::RHITextureDesc&) override  { return {0, 0}; }
    void                  DestroyTexture(uz::RHITextureHandle) override      {}
    void*                 MapBuffer(uz::RHIBufferHandle) override            { return nullptr; }
    void                  UnmapBuffer(uz::RHIBufferHandle) override          {}
    uint32_t              GetBufferSRVIndex(uz::RHIBufferHandle) const override  { return UINT32_MAX; }
    uint32_t              GetBufferUAVIndex(uz::RHIBufferHandle) const override  { return UINT32_MAX; }
    uint64_t              GetBufferGPUAddress(uz::RHIBufferHandle) const override { return 0; }
    uint32_t              GetTextureSRVIndex(uz::RHITextureHandle) const override { return UINT32_MAX; }
    uint32_t              GetTextureUAVIndex(uz::RHITextureHandle) const override { return UINT32_MAX; }
    uz::RHITextureDesc    GetTextureDesc(uz::RHITextureHandle) const override    { return {}; }
    uz::RHIShaderHandle   CreateShader(const uz::RHIShaderDesc&) override        { return {0, 0}; }
    void                  DestroyShader(uz::RHIShaderHandle) override            {}
    uz::RHIPipelineHandle CreateGraphicsPipeline(const uz::RHIGraphicsPipelineDesc&) override { return {0, 0}; }
    uz::RHIPipelineHandle CreateComputePipeline(const uz::RHIComputePipelineDesc&) override   { return {0, 0}; }
    void                  DestroyPipeline(uz::RHIPipelineHandle) override        {}
    std::vector<uint8_t>  GetPipelineCachedBlob(uz::RHIPipelineHandle) override  { return {}; }
    uz::RHISamplerHandle  CreateSampler(const uz::RHISamplerDesc&) override      { return {0, 0}; }
    void                  DestroySampler(uz::RHISamplerHandle) override          {}
    uint32_t              GetSamplerIndex(uz::RHISamplerHandle) const override   { return UINT32_MAX; }
    uz::IRHISwapChain*    CreateSwapChain(const uz::RHISwapChainDesc&) override  { return nullptr; }
    void                  DestroySwapChain(uz::IRHISwapChain*) override          {}
    uz::IRHIFence*        CreateFence() override                                 { return nullptr; }
    void                  DestroyFence(uz::IRHIFence*) override                  {}
    uz::IRHICommandList*  CreateCommandList(uz::RHIQueueType) override           { return nullptr; }
    void                  DestroyCommandList(uz::IRHICommandList*) override      {}
    void                  WaitIdle() override                                    {}
    const char*           GetAdapterName() const override   { return "DemoMockDevice"; }
    const char*           GetFeatureLevel() const override  { return "Mock"; }
    uz::RHIMemoryBudget   GetMemoryBudget() const override  { return {}; }
    uz::ShaderModel       GetShaderModel() const override   { return static_cast<uz::ShaderModel>(0); }
    uz::RHIBackend        GetBackend() const override       { return static_cast<uz::RHIBackend>(0); }
    void                  GpuProfilerBeginFrame(uz::IRHICommandList*) override    {}
    void                  GpuProfilerEndFrame(uz::IRHICommandList*) override      {}
    void                  GpuProfilerBeginTimestamp(uz::IRHICommandList*, const char*) override {}
    void                  GpuProfilerEndTimestamp(uz::IRHICommandList*, const char*) override   {}
    void                  GpuProfilerResolve() override                           {}
    std::vector<uz::GpuProfilerResult> GpuProfilerGetResults() const override     { return {}; }
    float                 GpuProfilerGetFrameTimeMs() const override              { return 0.0f; }

private:
    uint32_t idx_ = 0;
};

} // anonymous namespace

// ─────────────────────────────────────────────────────────────────────────────
// Decal system includes
// ─────────────────────────────────────────────────────────────────────────────

#include "engine/vfx/decals/decal_asset.h"
#include "engine/vfx/decals/decal_atlas.h"
#include "engine/vfx/decals/decal_component.h"
#include "engine/vfx/decals/decal_manager.h"
#include "engine/vfx/decals/decal_pool.h"

// ─────────────────────────────────────────────────────────────────────────────
// Demo constants
// ─────────────────────────────────────────────────────────────────────────────

/// Pool sized to hold bullet holes + showcase decals with comfortable headroom.
/// Phase 4A spawns 120 bullet holes, Phase 4B spawns 100 showcase = 220 peak.
static constexpr uint32_t kPoolSize = 1000;

/// Square texture atlas dimension in pixels.
static constexpr uint32_t kAtlasSize = 2048;

/// Fixed timestep (60 FPS).
static constexpr float kDt = 1.0f / 60.0f;

/// Total simulation duration: 15 seconds at 60 FPS.
/// Long enough for all 10-second bullet holes to fully expire.
static constexpr int kTotalFrames = 900;

/// Stat overlay printed every N frames (once per second).
static constexpr int kReportInterval = 60;

/// Number of bullet-hole decals placed in Phase 4A.
static constexpr uint32_t kBulletHoleCount = 120;

/// Bullet hole lifetime; long enough to maintain 100+ simultaneous decals.
static constexpr float kBulletHoleLifetime = 10.0f;

/// Fade-out window at the end of the bullet hole's lifetime.
static constexpr float kBulletHoleFadeDuration = 2.0f;

/// Decals spawned per blend mode in the Phase 4B showcase (5×5 grid).
static constexpr uint32_t kShowcaseDecalsPerMode = 25;

/// Showcase decal lifetime (shorter than bullet holes so the pool drains).
static constexpr float kShowcaseLifetime = 8.0f;

/// Simulated camera position used for back-to-front sort in GetSortedDecals.
static constexpr uz::Vec3 kCameraPos{0.0f, 1.7f, -5.0f};

// ─────────────────────────────────────────────────────────────────────────────
// Inline asset JSON definitions (no disk I/O)
// ─────────────────────────────────────────────────────────────────────────────

/// Bullet hole: small Opaque decal with normal override and roughness change.
static const char* const kBulletHoleJson = R"({
    "format": "uzdecal",
    "version": 1,
    "material": "materials/decals/bullet_hole.uzmat",
    "blend_mode": "Opaque",
    "lifetime": 10.0,
    "fade_duration": 2.0,
    "normal_intensity": 0.9,
    "roughness_override": 0.8,
    "sort_order": 0,
    "half_extents": [0.15, 0.15, 0.1]
})";

/// Opaque showcase: larger crack pattern; replaces surface properties.
static const char* const kOpaqueShowcaseJson = R"({
    "format": "uzdecal",
    "version": 1,
    "material": "materials/decals/crack_opaque.uzmat",
    "blend_mode": "Opaque",
    "lifetime": 8.0,
    "fade_duration": 1.5,
    "normal_intensity": 0.8,
    "roughness_override": 0.7,
    "sort_order": 0,
    "half_extents": [0.4, 0.4, 0.12]
})";

/// Additive showcase: emissive scorch mark that brightens the surface.
static const char* const kAdditiveJson = R"({
    "format": "uzdecal",
    "version": 1,
    "material": "materials/decals/scorch_additive.uzmat",
    "blend_mode": "Additive",
    "lifetime": 8.0,
    "fade_duration": 1.5,
    "normal_intensity": 0.3,
    "roughness_override": -1.0,
    "sort_order": 1,
    "half_extents": [0.4, 0.4, 0.15]
})";

/// Multiply showcase: dirt/grime that darkens the surface by multiplication.
static const char* const kMultiplyJson = R"({
    "format": "uzdecal",
    "version": 1,
    "material": "materials/decals/dirt_multiply.uzmat",
    "blend_mode": "Multiply",
    "lifetime": 8.0,
    "fade_duration": 1.5,
    "normal_intensity": 0.5,
    "roughness_override": 0.6,
    "sort_order": 2,
    "half_extents": [0.5, 0.5, 0.12]
})";

/// Screen showcase: wet-surface highlight that brightens and desaturates.
static const char* const kScreenJson = R"({
    "format": "uzdecal",
    "version": 1,
    "material": "materials/decals/wetspot_screen.uzmat",
    "blend_mode": "Screen",
    "lifetime": 8.0,
    "fade_duration": 1.5,
    "normal_intensity": 0.7,
    "roughness_override": 0.1,
    "sort_order": 3,
    "half_extents": [0.35, 0.35, 0.1]
})";

// ─────────────────────────────────────────────────────────────────────────────
// Simulated wall surfaces
// ─────────────────────────────────────────────────────────────────────────────

namespace {

/// Planar wall surface used to scatter bullet-hole impact points.
///
/// No real geometry -- stores the logical centre and normal of an axis-aligned
/// wall so SpawnBulletHole can place decals without scene intersection tests.
struct WallSurface {
    const char* name;     ///< Human-readable label for log output.
    uz::Vec3    center;   ///< World-space OBB centre of this wall.
    uz::Vec3    normal;   ///< Outward unit normal (towards the shooter).
    float       halfWidth;  ///< Half-extent along the primary tangent axis.
    float       halfHeight; ///< Half-extent along the secondary tangent axis.
};

/// Five cardinal walls surrounding a small room (4 m × 3 m × 4 m).
static const std::array<WallSurface, 5> kWalls = {{
    {"north_wall",  {  0.0f, 1.5f,  4.0f}, { 0.0f,  0.0f, -1.0f}, 3.0f, 1.5f},
    {"south_wall",  {  0.0f, 1.5f, -4.0f}, { 0.0f,  0.0f,  1.0f}, 3.0f, 1.5f},
    {"east_wall",   {  4.0f, 1.5f,  0.0f}, {-1.0f,  0.0f,  0.0f}, 3.0f, 1.5f},
    {"west_wall",   { -4.0f, 1.5f,  0.0f}, { 1.0f,  0.0f,  0.0f}, 3.0f, 1.5f},
    {"ceiling",     {  0.0f, 3.0f,  0.0f}, { 0.0f, -1.0f,  0.0f}, 3.5f, 3.5f},
}};

// ─────────────────────────────────────────────────────────────────────────────
// Utility helpers
// ─────────────────────────────────────────────────────────────────────────────

/// Deterministic LCG float in [0, 1). Uses Numerical Recipes constants.
///
/// Avoids pulling in `<random>` state at file scope, keeping the demo
/// fully reproducible across platforms.
///
/// # Parameters
/// * `seed` -- Mutable seed updated each call (in/out).
///
/// # Returns
/// Float in [0.0, 1.0).
[[nodiscard]] float LcgFloat(uint32_t& seed) noexcept {
    seed = seed * 1664525u + 1013904223u;
    return static_cast<float>(seed & 0x00FFFFFFu) / static_cast<float>(0x01000000u);
}

/// Prints a horizontal separator line to stdout.
void PrintSeparator() {
    std::printf("──────────────────────────────────────────────────────────────\n");
}

// ─────────────────────────────────────────────────────────────────────────────
// Phase helpers
// ─────────────────────────────────────────────────────────────────────────────

/// Spawns one bullet-hole decal at a random point on the given wall surface.
///
/// Scatters the impact point uniformly within the wall's half-extents using
/// the provided LCG seed. Chooses the two surface-tangent axes automatically
/// based on the dominant component of the wall normal.
///
/// # Parameters
/// * `mgr`    -- Initialized DecalManager (must own a live pool slot).
/// * `asset`  -- Pre-loaded bullet hole DecalAsset.
/// * `wall`   -- Target wall (provides centre, normal, and scatter bounds).
/// * `seed`   -- LCG seed, mutated in-place for each call.
///
/// # Returns
/// Non-owning pointer to the spawned decal, or nullptr if the pool is full.
[[nodiscard]] uz::DecalComponent* SpawnBulletHole(uz::DecalManager& mgr,
                                                   const uz::DecalAsset& asset,
                                                   const WallSurface& wall,
                                                   uint32_t& seed) {
    // Signed scatter within the wall rectangle: [-half, +half].
    const float u = (LcgFloat(seed) * 2.0f - 1.0f) * wall.halfWidth;
    const float v = (LcgFloat(seed) * 2.0f - 1.0f) * wall.halfHeight;

    // Decompose: choose tangent axes based on the dominant normal component.
    // Z-dominant  (north/south walls): tangent U = X, tangent V = Y.
    // X-dominant  (east/west walls)  : tangent U = Z, tangent V = Y.
    // Y-dominant  (ceiling)          : tangent U = X, tangent V = Z.
    uz::Vec3 impact;
    if (std::abs(wall.normal.z) > 0.5f) {
        impact = {wall.center.x + u, wall.center.y + v, wall.center.z};
    } else if (std::abs(wall.normal.x) > 0.5f) {
        impact = {wall.center.x, wall.center.y + v, wall.center.z + u};
    } else {
        impact = {wall.center.x + u, wall.center.y, wall.center.z + v};
    }

    return mgr.SpawnDecal(asset, impact, wall.normal, kBulletHoleLifetime);
}

/// Spawns a 5×5 grid of showcase decals on the north-wall face.
///
/// Tiles kShowcaseDecalsPerMode (25) decals evenly in a 2.0 m × 1.6 m
/// patch centred at (0, 1.5, 3.95) -- just inside the north wall.
///
/// # Parameters
/// * `mgr`      -- Initialized DecalManager.
/// * `asset`    -- Pre-loaded asset for the blend mode under test.
/// * `modeName` -- Label for the stats line (e.g. "Additive").
///
/// # Returns
/// Number of decals successfully spawned.
[[nodiscard]] uint32_t SpawnBlendModeSet(uz::DecalManager& mgr,
                                          const uz::DecalAsset& asset,
                                          const char* modeName) {
    static constexpr int   kCols    = 5;
    static constexpr int   kRows    = 5;
    static constexpr float kStepX   = 0.5f;  // 2.0 m total span / 4 gaps
    static constexpr float kStepY   = 0.4f;  // 1.6 m total span / 4 gaps
    static constexpr float kWallZ   = 3.95f;
    static constexpr float kBaseY   = 0.8f;  // Bottom row height

    uint32_t spawned = 0;
    for (int row = 0; row < kRows; ++row) {
        for (int col = 0; col < kCols; ++col) {
            const float x = (col - kCols / 2) * kStepX;
            const float y = kBaseY + static_cast<float>(row) * kStepY;
            const uz::Vec3 pos{x, y, kWallZ};
            const uz::Vec3 normal{0.0f, 0.0f, -1.0f};

            if (mgr.SpawnDecal(asset, pos, normal, kShowcaseLifetime)) {
                ++spawned;
            }
        }
    }

    std::printf("  %-10s  %u / %u decals spawned\n",
                modeName, spawned, kShowcaseDecalsPerMode);
    return spawned;
}

/// Prints the per-frame performance overlay line to stdout.
///
/// Emits active count, pool in_use/available, atlas occupancy, and the
/// wall-clock duration of the most recent Update + GetSortedDecals call.
///
/// # Parameters
/// * `frame`    -- Current frame index.
/// * `mgr`      -- Manager to read stats from (const, no side effects).
/// * `frameMs`  -- Measured wall-clock time for this frame in milliseconds.
void PrintStats(int frame, const uz::DecalManager& mgr, double frameMs) {
    const uint32_t active    = mgr.GetActiveCount();
    const uint32_t available = mgr.GetPoolAvailable();
    const float    occupancy = mgr.GetAtlasOccupancy();

    std::printf("  [Frame %4d | t=%5.2fs]  active=%-4u  in_use=%-4u  avail=%-4u"
                "  atlas=%5.2f%%  frame=%.3fms\n",
                frame,
                static_cast<double>(frame) * kDt,
                active, active, available,
                static_cast<double>(occupancy * 100.0f),
                frameMs);
}

} // anonymous namespace

// ─────────────────────────────────────────────────────────────────────────────
// Phase 1: DecalAsset loading from inline JSON
// ─────────────────────────────────────────────────────────────────────────────

/// Loads five decal assets from inline JSON and prints a validation table.
///
/// # Outputs (via out-parameters)
/// * `bulletHole`       -- Opaque bullet hole asset.
/// * `opaqueShowcase`   -- Opaque showcase (crack pattern).
/// * `additive`         -- Additive showcase (scorch).
/// * `multiply`         -- Multiply showcase (dirt).
/// * `screen`           -- Screen showcase (wet spot).
///
/// # Returns
/// true if all five assets loaded and validated successfully.
static bool RunAssetLoadingPhase(std::unique_ptr<uz::DecalAsset>& bulletHole,
                                  std::unique_ptr<uz::DecalAsset>& opaqueShowcase,
                                  std::unique_ptr<uz::DecalAsset>& additive,
                                  std::unique_ptr<uz::DecalAsset>& multiply,
                                  std::unique_ptr<uz::DecalAsset>& screen) {
    std::printf("\n== Phase 1: DecalAsset Loading ==============================\n");

    bulletHole     = uz::DecalAsset::LoadFromString(kBulletHoleJson);
    opaqueShowcase = uz::DecalAsset::LoadFromString(kOpaqueShowcaseJson);
    additive       = uz::DecalAsset::LoadFromString(kAdditiveJson);
    multiply       = uz::DecalAsset::LoadFromString(kMultiplyJson);
    screen         = uz::DecalAsset::LoadFromString(kScreenJson);

    bool allLoaded = true;

    struct AssetInfo {
        const char*           name;
        const uz::DecalAsset* asset;
    };

    const AssetInfo assets[] = {
        {"bullet_hole (Opaque)",     bulletHole.get()},
        {"crack_opaque (Opaque)",    opaqueShowcase.get()},
        {"scorch_additive (Add)",    additive.get()},
        {"dirt_multiply (Mul)",      multiply.get()},
        {"wetspot_screen (Screen)",  screen.get()},
    };

    for (const auto& info : assets) {
        if (!info.asset) {
            std::fprintf(stderr, "  [FAIL] %s: LoadFromString returned nullptr\n", info.name);
            allLoaded = false;
            continue;
        }
        std::printf("  [OK] %-30s  blend=%-10s  life=%4.1fs  extents=[%.2f,%.2f,%.2f]\n",
                    info.name,
                    uz::DecalBlendModeToString(info.asset->blendMode),
                    static_cast<double>(info.asset->lifetime),
                    static_cast<double>(info.asset->halfExtents.x),
                    static_cast<double>(info.asset->halfExtents.y),
                    static_cast<double>(info.asset->halfExtents.z));
    }

    return allLoaded;
}

// ─────────────────────────────────────────────────────────────────────────────
// Phase 2: DecalPool direct operations (exhaustion and recovery)
// ─────────────────────────────────────────────────────────────────────────────

/// Exercises DecalPool acquire/return/exhaustion/clear without DecalManager.
///
/// Uses a small 10-slot pool to keep output concise.
///
/// # Returns
/// true if all pool invariants hold, false on any violation.
static bool RunPoolPhase() {
    std::printf("\n== Phase 2: DecalPool Exhaustion & Recovery =================\n");

    static constexpr uint32_t kSmallPoolSize = 10;
    uz::DecalPool pool(kSmallPoolSize);

    std::printf("  Pool created: capacity=%u  active=%u  available=%u\n",
                pool.GetPoolSize(), pool.GetActiveCount(), pool.GetAvailableCount());

    bool pass = true;

    // Acquire every slot.
    uz::DecalComponent* acquired[kSmallPoolSize] = {};
    for (uint32_t i = 0; i < kSmallPoolSize; ++i) {
        acquired[i] = pool.AcquireDecal();
        if (!acquired[i]) {
            std::fprintf(stderr, "  [FAIL] AcquireDecal slot %u returned nullptr\n", i);
            pass = false;
        }
    }

    std::printf("  After fill:  active=%u  available=%u\n",
                pool.GetActiveCount(), pool.GetAvailableCount());

    if (pool.GetActiveCount() != kSmallPoolSize) {
        std::fprintf(stderr, "  [FAIL] Expected active=%u, got %u\n",
                     kSmallPoolSize, pool.GetActiveCount());
        pass = false;
    }

    // Exhaustion: the (kSmallPoolSize+1)-th acquire must return nullptr.
    uz::DecalComponent* overflow = pool.AcquireDecal();
    if (overflow != nullptr) {
        std::fprintf(stderr,
                     "  [FAIL] Pool exhaustion did not return nullptr (got non-null)\n");
        pass = false;
    } else {
        std::printf("  [OK]  Pool exhaustion correctly returns nullptr\n");
    }

    // Return the first half.
    for (uint32_t i = 0; i < kSmallPoolSize / 2; ++i) {
        pool.ReturnDecal(acquired[i]);
        acquired[i] = nullptr;
    }

    std::printf("  After returning %u: active=%u  available=%u\n",
                kSmallPoolSize / 2, pool.GetActiveCount(), pool.GetAvailableCount());

    if (pool.GetAvailableCount() != kSmallPoolSize / 2) {
        std::fprintf(stderr, "  [FAIL] Expected available=%u after partial return, got %u\n",
                     kSmallPoolSize / 2, pool.GetAvailableCount());
        pass = false;
    }

    // Re-acquire the returned slots.
    for (uint32_t i = 0; i < kSmallPoolSize / 2; ++i) {
        uz::DecalComponent* reacq = pool.AcquireDecal();
        if (!reacq) {
            std::fprintf(stderr, "  [FAIL] Re-acquire slot %u returned nullptr\n", i);
            pass = false;
        }
    }

    std::printf("  After re-acquire: active=%u  available=%u\n",
                pool.GetActiveCount(), pool.GetAvailableCount());

    // Clear must restore full availability.
    pool.Clear();
    std::printf("  After Clear: active=%u  available=%u\n",
                pool.GetActiveCount(), pool.GetAvailableCount());

    if (pool.GetActiveCount() != 0 || pool.GetAvailableCount() != kSmallPoolSize) {
        std::fprintf(stderr, "  [FAIL] Clear did not restore full pool\n");
        pass = false;
    } else {
        std::printf("  [OK]  Pool Clear restored full capacity (%u slots)\n", kSmallPoolSize);
    }

    return pass;
}

// ─────────────────────────────────────────────────────────────────────────────
// Phase 3: DecalAtlas shelf packing
// ─────────────────────────────────────────────────────────────────────────────

/// Exercises the DecalAtlas shelf-packing allocator with realistic texture sizes.
///
/// Packs 12 entries (128–1024 px), reports UV coordinates and occupancy,
/// tests HasSpace for a small and an oversized entry, then validates Reset.
///
/// # Returns
/// true if all atlas operations behave as specified, false on any violation.
static bool RunAtlasPhase() {
    std::printf("\n== Phase 3: DecalAtlas Packing ==============================\n");

    uz::DecalAtlas atlas(kAtlasSize);

    std::printf("  Atlas created: size=%u px  occupancy=%.2f%%\n",
                atlas.GetAtlasSize(),
                static_cast<double>(atlas.GetOccupancy() * 100.0f));

    bool pass = true;

    struct TexEntry { const char* name; uint32_t w; uint32_t h; };

    static constexpr TexEntry kEntries[] = {
        {"bullet_hole_albedo",     256,  256},
        {"bullet_hole_normal",     256,  256},
        {"scorch_mark_albedo",     512,  512},
        {"scorch_mark_normal",     512,  512},
        {"dirt_stain_albedo",      128,  128},
        {"dirt_stain_normal",      128,  128},
        {"wet_surface_albedo",     256,  256},
        {"wet_surface_normal",     256,  256},
        {"crack_pattern_albedo",  1024,  512},
        {"crack_pattern_normal",  1024,  512},
        {"blood_splat_albedo",     384,  384},
        {"blood_splat_normal",     384,  384},
    };

    for (const auto& e : kEntries) {
        const auto result = atlas.AddEntry(e.w, e.h);
        if (result) {
            std::printf("  [OK]  %-28s %4ux%-4u  UV[%.4f,%.4f]-[%.4f,%.4f]\n",
                        e.name, e.w, e.h,
                        static_cast<double>(result->uvMin.x),
                        static_cast<double>(result->uvMin.y),
                        static_cast<double>(result->uvMax.x),
                        static_cast<double>(result->uvMax.y));
        } else {
            std::fprintf(stderr, "  [FAIL] %-28s %4ux%-4u -- no space\n", e.name, e.w, e.h);
            pass = false;
        }
    }

    std::printf("  Atlas stats: entries=%u  occupancy=%.2f%%  usedArea=%u px\n",
                atlas.GetEntryCount(),
                static_cast<double>(atlas.GetOccupancy() * 100.0f),
                atlas.GetUsedArea());

    // A small entry must fit (atlas is only ~25% full after 12 entries).
    if (!atlas.HasSpace(64, 64)) {
        std::fprintf(stderr, "  [FAIL] HasSpace(64, 64) returned false unexpectedly\n");
        pass = false;
    } else {
        std::printf("  [OK]  HasSpace(64, 64) = true\n");
    }

    // An entry larger than the atlas itself must be rejected.
    if (atlas.HasSpace(kAtlasSize + 1, kAtlasSize + 1)) {
        std::fprintf(stderr, "  [FAIL] HasSpace(oversized) should return false\n");
        pass = false;
    } else {
        std::printf("  [OK]  HasSpace(oversized) = false\n");
    }

    // Reset must restore empty state without changing atlas dimensions.
    atlas.Reset();
    if (atlas.GetEntryCount() != 0 || atlas.GetOccupancy() != 0.0f) {
        std::fprintf(stderr, "  [FAIL] Reset did not clear atlas state\n");
        pass = false;
    } else {
        std::printf("  [OK]  Reset restored empty atlas (size still %u px)\n",
                    atlas.GetAtlasSize());
    }

    return pass;
}

// ─────────────────────────────────────────────────────────────────────────────
// Phase 4: Full DecalManager simulation (bullet holes + blend-mode showcase)
// ─────────────────────────────────────────────────────────────────────────────

/// Runs the main simulation: bullet-hole load test, blend-mode showcase,
/// and a 15-second update loop with per-second overlay output.
///
/// # Parameters
/// * `device`         -- Headless mock RHI device.
/// * `bulletHoleAsset`  -- Opaque bullet hole asset.
/// * `opaqueShowcase`   -- Opaque showcase asset.
/// * `additive`         -- Additive showcase asset.
/// * `multiply`         -- Multiply showcase asset.
/// * `screen`           -- Screen showcase asset.
///
/// # Returns
/// true if all validation checks pass, false on any failure.
static bool RunSimulationPhase(DemoMockDevice& device,
                                const uz::DecalAsset& bulletHoleAsset,
                                const uz::DecalAsset& opaqueShowcase,
                                const uz::DecalAsset& additive,
                                const uz::DecalAsset& multiply,
                                const uz::DecalAsset& screen) {
    std::printf("\n== Phase 4: Full Simulation (15 seconds) ====================\n");

    uz::DecalManager manager;
    manager.Initialize(&device, kPoolSize, kAtlasSize);

    std::printf("  Manager initialized: pool=%u  atlas=%u px\n", kPoolSize, kAtlasSize);

    // ── Phase 4A: Bullet hole load test ──────────────────────────────────────
    PrintSeparator();
    std::printf("[Phase 4A] Bullet hole load test -- %u decals across %zu walls\n",
                kBulletHoleCount, kWalls.size());

    uint32_t lcgSeed = 0xDECAFBADu; // Deterministic seed for reproducibility.
    uint32_t bulletHolesSpawned = 0;

    for (uint32_t i = 0; i < kBulletHoleCount; ++i) {
        const WallSurface& wall = kWalls[i % kWalls.size()];
        if (SpawnBulletHole(manager, bulletHoleAsset, wall, lcgSeed)) {
            ++bulletHolesSpawned;
        }
    }

    std::printf("[Phase 4A] Spawned %u / %u  (active=%u  pool_avail=%u)\n\n",
                bulletHolesSpawned, kBulletHoleCount,
                manager.GetActiveCount(), manager.GetPoolAvailable());

    // ── Phase 4B: Blend-mode showcase ────────────────────────────────────────
    PrintSeparator();
    std::printf("[Phase 4B] Blend-mode showcase -- 4 modes x %u decals (5x5 grid)\n",
                kShowcaseDecalsPerMode);

    const uint32_t opaqueSpawned   = SpawnBlendModeSet(manager, opaqueShowcase, "Opaque");
    const uint32_t additiveSpawned = SpawnBlendModeSet(manager, additive,       "Additive");
    const uint32_t multiplySpawned = SpawnBlendModeSet(manager, multiply,       "Multiply");
    const uint32_t screenSpawned   = SpawnBlendModeSet(manager, screen,         "Screen");

    const uint32_t showcaseTotal =
        opaqueSpawned + additiveSpawned + multiplySpawned + screenSpawned;

    std::printf("[Phase 4B] Showcase total: %u  (active now: %u)\n\n",
                showcaseTotal, manager.GetActiveCount());

    // ── Main simulation loop ──────────────────────────────────────────────────
    PrintSeparator();
    std::printf("[Sim] %d frames (%.1f s @ %.0f FPS)\n",
                kTotalFrames,
                static_cast<double>(kTotalFrames * kDt),
                static_cast<double>(1.0f / kDt));
    std::printf("[Sim] Bullet holes fade at t=%.1fs, expire at t=%.1fs\n",
                static_cast<double>(kBulletHoleLifetime - kBulletHoleFadeDuration),
                static_cast<double>(kBulletHoleLifetime));
    std::printf("[Sim] Showcase decals expire at t=%.1fs\n\n",
                static_cast<double>(kShowcaseLifetime));
    PrintSeparator();

    uint32_t peakActiveCount = 0;
    uint32_t prevActive      = manager.GetActiveCount();
    uint32_t totalExpired    = 0;

    for (int frame = 0; frame < kTotalFrames; ++frame) {
        const auto t0 = std::chrono::high_resolution_clock::now();

        // Advance age, fade, expiry detection.
        manager.Update(kDt);

        // Obtain back-to-front sorted decal list relative to the camera.
        // In headless mode we discard the pointer list but ensure the code
        // path (including the distance sort) executes correctly.
        const auto& visible = manager.GetSortedDecals(kCameraPos);
        (void)visible;

        const auto t1 = std::chrono::high_resolution_clock::now();
        const double frameMs =
            std::chrono::duration<double, std::milli>(t1 - t0).count();

        const uint32_t active = manager.GetActiveCount();

        // Accumulate expirations detected via pool drain.
        if (active < prevActive) {
            totalExpired += prevActive - active;
        }
        prevActive = active;

        if (active > peakActiveCount) {
            peakActiveCount = active;
        }

        // Report overlay at boundaries and regular intervals.
        const bool isBulletExpiry  = (frame == static_cast<int>(kBulletHoleLifetime / kDt));
        const bool isShowcaseExpiry = (frame == static_cast<int>(kShowcaseLifetime / kDt));
        const bool isReport =
            frame == 0 ||
            frame == kTotalFrames - 1 ||
            isBulletExpiry ||
            isShowcaseExpiry ||
            (frame > 0 && frame % kReportInterval == 0);

        if (isReport) {
            PrintStats(frame, manager, frameMs);
        }
    }

    // ── Summary ───────────────────────────────────────────────────────────────
    PrintSeparator();
    std::printf("\nSIMULATION SUMMARY\n");
    PrintSeparator();
    std::printf("  Frames simulated:          %d (%.1f s)\n",
                kTotalFrames, static_cast<double>(kTotalFrames * kDt));
    std::printf("  Bullet holes placed:        %u / %u\n",
                bulletHolesSpawned, kBulletHoleCount);
    std::printf("  Showcase decals placed:     %u / %u\n",
                showcaseTotal, kShowcaseDecalsPerMode * 4u);
    std::printf("  Peak simultaneous decals:   %u\n", peakActiveCount);
    std::printf("  Total decals expired:       %u\n", totalExpired);
    std::printf("  Final active count:         %u\n", manager.GetActiveCount());
    std::printf("  Pool available at exit:     %u / %u\n",
                manager.GetPoolAvailable(), kPoolSize);
    std::printf("  Atlas occupancy at exit:    %.2f%%\n",
                static_cast<double>(manager.GetAtlasOccupancy() * 100.0f));
    PrintSeparator();
    std::printf("  Blend mode breakdown:\n");
    std::printf("    Opaque    %u\n", opaqueSpawned);
    std::printf("    Additive  %u\n", additiveSpawned);
    std::printf("    Multiply  %u\n", multiplySpawned);
    std::printf("    Screen    %u\n", screenSpawned);
    PrintSeparator();

    // ── Validation ────────────────────────────────────────────────────────────
    bool pass = true;

    // All bullet holes must spawn (pool has 1000 slots, peak is ~220).
    if (bulletHolesSpawned != kBulletHoleCount) {
        std::fprintf(stderr, "  [FAIL] Bullet holes: expected %u, got %u\n",
                     kBulletHoleCount, bulletHolesSpawned);
        pass = false;
    }

    // Every blend mode set must complete its 5×5 grid.
    auto checkSet = [&](const char* name, uint32_t spawned) {
        if (spawned != kShowcaseDecalsPerMode) {
            std::fprintf(stderr, "  [FAIL] %s showcase: expected %u, got %u\n",
                         name, kShowcaseDecalsPerMode, spawned);
            pass = false;
        }
    };
    checkSet("Opaque",   opaqueSpawned);
    checkSet("Additive", additiveSpawned);
    checkSet("Multiply", multiplySpawned);
    checkSet("Screen",   screenSpawned);

    // Must have seen 100+ simultaneous decals (the load test target).
    if (peakActiveCount < 100) {
        std::fprintf(stderr,
                     "  [FAIL] Peak simultaneous decals %u < required 100\n",
                     peakActiveCount);
        pass = false;
    }

    // Pool accounting must be consistent (active + available == poolSize).
    const uint32_t sum = manager.GetActiveCount() + manager.GetPoolAvailable();
    if (sum != kPoolSize) {
        std::fprintf(stderr,
                     "  [FAIL] Pool sum: active(%u) + avail(%u) = %u, expected %u\n",
                     manager.GetActiveCount(), manager.GetPoolAvailable(),
                     sum, kPoolSize);
        pass = false;
    }

    // After 15 s all decals with lifetime ≤ 10 s must have expired.
    if (manager.GetActiveCount() != 0) {
        std::fprintf(stderr,
                     "  [FAIL] Expected 0 active decals at t=15s, got %u\n",
                     manager.GetActiveCount());
        pass = false;
    }

    // ── Cleanup ───────────────────────────────────────────────────────────────
    manager.Shutdown(&device);

    if (manager.IsInitialized()) {
        std::fprintf(stderr, "  [FAIL] Manager still initialized after Shutdown()\n");
        pass = false;
    } else {
        std::printf("  [OK]  Manager shut down cleanly\n");
    }

    return pass;
}

// ─────────────────────────────────────────────────────────────────────────────
// Main
// ─────────────────────────────────────────────────────────────────────────────

int main() {
    std::printf("==========================================================\n");
    std::printf("         UzEngine -- Decal System Demo\n");
    std::printf("   Bullet holes, blend modes, pool management (headless)\n");
    std::printf("==========================================================\n");

    DemoMockDevice device;
    bool allPass = true;

    // Phase 1: Load all five decal assets from inline JSON.
    std::unique_ptr<uz::DecalAsset> bulletHole, opaqueShowcase, additive, multiply, screen;
    if (!RunAssetLoadingPhase(bulletHole, opaqueShowcase, additive, multiply, screen)) {
        std::fprintf(stderr, "\n[FAIL] Phase 1 (Asset Loading) failed -- aborting\n");
        return 1;
    }

    // Phase 2: Direct pool exhaustion and recovery.
    if (!RunPoolPhase()) {
        std::fprintf(stderr, "\n[FAIL] Phase 2 (Pool Operations) failed.\n");
        allPass = false;
    }

    // Phase 3: Atlas shelf packing.
    if (!RunAtlasPhase()) {
        std::fprintf(stderr, "\n[FAIL] Phase 3 (Atlas Packing) failed.\n");
        allPass = false;
    }

    // Phase 4: Full simulation through DecalManager.
    if (!RunSimulationPhase(device, *bulletHole, *opaqueShowcase, *additive, *multiply, *screen)) {
        std::fprintf(stderr, "\n[FAIL] Phase 4 (Simulation) failed.\n");
        allPass = false;
    }

    if (allPass) {
        std::printf("\n[PASS] Decal system demo completed successfully.\n");
        return 0;
    }

    std::fprintf(stderr, "\n[FAIL] One or more phases failed.\n");
    return 1;
}
