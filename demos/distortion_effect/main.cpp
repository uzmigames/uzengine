/// Distortion Effect Demo -- Headless screen-space distortion VFX showcase.
///
/// Demonstrates screen-space distortion by exercising the VFXSystem with two
/// particle emitters that represent heat shimmer above a flame and a glass-sphere
/// refraction source. The demo validates the DistortionConstants and
/// DistortionResolveConstants struct layouts, populates DistortionEmitterData
/// with mock buffer handles, and runs the full emitter tick loop.
///
/// This is a headless (no window, no GPU) demo that validates:
/// - VFXSystem + two ParticleEmitters with separate distortion configurations
/// - DistortionConstants layout: sizeof==16, distortionStrength default==1.0f
/// - DistortionResolveConstants layout: sizeof==16
/// - DistortionEmitterData population with mock pipeline and CB handles
/// - Emitter spawn accumulation over 120 frames (2 s at 60 FPS)
/// - Per-frame stats printed at report intervals
///
/// # Build
///   cd demos/distortion_effect
///   zig build
///   ./zig-out/bin/distortion_effect
///
/// # Emitters
/// - heat_shimmer: rate=200/s, lifetime=1.5s, velocity=(0,3,0), maxParticles=5000
/// - glass_sphere:  rate=100/s, lifetime=3.0s, velocity=(0,0,0), maxParticles=2000

#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <algorithm>
#include <memory>
#include <string>

// ─────────────────────────────────────────────────────────────────────────────
// Inline mock RHI device -- avoids dependency on test infrastructure
// ─────────────────────────────────────────────────────────────────────────────

#include "engine/rhi/rhi.h"

namespace {

/// Minimal mock device for headless demo. Returns valid dummy handles so that
/// VFXSystem::Initialize and ParticleEmitter::Initialize succeed without GPU.
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
// VFX includes
// ─────────────────────────────────────────────────────────────────────────────

#include "engine/vfx/vfx_system.h"
#include "engine/vfx/components/particle_emitter.h"
#include "engine/vfx/core/particle_budget.h"
#include "engine/vfx/modules/emitter_module_stack.h"
#include "engine/vfx/modules/spawn_module.h"
#include "engine/vfx/modules/update_module.h"
#include "engine/vfx/modules/decay_module.h"

// Distortion pass struct definitions (no GPU execution — header types only).
#include "engine/vfx/passes/distortion_pass.h"
#include "engine/vfx/passes/distortion_resolve_pass.h"

// ─────────────────────────────────────────────────────────────────────────────
// Demo parameters
// ─────────────────────────────────────────────────────────────────────────────

/// heat_shimmer emitter: high-rate, short-lived distortion above a flame source.
static constexpr uint32_t kHeatMaxParticles  = 5000u;
static constexpr float    kHeatEmissionRate  = 200.0f;   // particles/sec
static constexpr float    kHeatLifetime      = 1.5f;     // seconds
static constexpr float    kHeatSpreadAngle   = 25.0f;    // degrees -- narrow rising column

/// glass_sphere emitter: lower rate, longer lifetime, sphere-distributed distortion.
static constexpr uint32_t kGlassMaxParticles = 2000u;
static constexpr float    kGlassEmissionRate = 100.0f;   // particles/sec
static constexpr float    kGlassLifetime     = 3.0f;     // seconds
static constexpr float    kGlassSpreadAngle  = 90.0f;    // degrees -- full hemisphere

static constexpr float    kDt             = 1.0f / 60.0f;
static constexpr int      kTotalFrames    = 120;          // 2 seconds at 60 FPS
static constexpr int      kReportInterval = 30;           // print stats every N frames
static constexpr uint32_t kBudgetMax      = 500000u;

// ─────────────────────────────────────────────────────────────────────────────
// Main
// ─────────────────────────────────────────────────────────────────────────────

int main() {
    std::printf("==========================================================\n");
    std::printf("      UzEngine -- Distortion Effect Demo\n");
    std::printf("  Heat shimmer + glass sphere (headless, no GPU)\n");
    std::printf("==========================================================\n\n");

    // ── Setup ──────────────────────────────────────────────────────────
    DemoMockDevice device;

    uz::VFXSystem vfxSystem;
    vfxSystem.Initialize(&device, kBudgetMax);
    std::printf("[Init] VFXSystem initialized -- budget: %u particles\n\n", kBudgetMax);

    // ── Emitter A: heat_shimmer ─────────────────────────────────────────
    uz::ParticleEmitter heatEmitter;
    heatEmitter.maxParticles  = kHeatMaxParticles;
    heatEmitter.emissionRate  = kHeatEmissionRate;
    heatEmitter.lifetime      = kHeatLifetime;
    heatEmitter.spawnVelocity = {0.0f, 3.0f, 0.0f};   // upward shimmer column
    heatEmitter.spreadAngle   = kHeatSpreadAngle;
    heatEmitter.gravity       = {0.0f, 0.5f, 0.0f};   // slight upward buoyancy
    heatEmitter.damping       = 0.97f;
    heatEmitter.isEmitting    = true;

    if (!heatEmitter.Initialize(&device, vfxSystem.GetBudget())) {
        std::fprintf(stderr, "[Error] Failed to initialize heat_shimmer emitter\n");
        vfxSystem.Shutdown(&device);
        return 1;
    }

    std::printf("[Init] heat_shimmer emitter initialized\n");
    std::printf("       capacity=%u  rate=%.1f/s  lifetime=%.2fs\n",
                heatEmitter.allocatedFromBudget, kHeatEmissionRate, kHeatLifetime);

    // Build a basic module stack for heat_shimmer (spawn + update + decay).
    {
        auto& stack = heatEmitter.moduleStack;

        auto spawnMod         = std::make_unique<uz::SpawnModule>();
        spawnMod->rate        = kHeatEmissionRate;
        spawnMod->spreadAngle = kHeatSpreadAngle;
        spawnMod->velocity    = {0.0f, 3.0f, 0.0f};
        spawnMod->lifespanMin = 0.5f;
        spawnMod->lifespanMax = kHeatLifetime;
        stack.AddSpawnModule(std::move(spawnMod));

        auto updateMod    = std::make_unique<uz::UpdateModule>();
        updateMod->gravity = {0.0f, 0.5f, 0.0f};
        updateMod->drag    = 0.97f;
        stack.AddUpdateModule(std::move(updateMod));

        stack.AddUpdateModule(std::make_unique<uz::DecayModule>());
    }

    // ── Emitter B: glass_sphere ─────────────────────────────────────────
    uz::ParticleEmitter glassEmitter;
    glassEmitter.maxParticles  = kGlassMaxParticles;
    glassEmitter.emissionRate  = kGlassEmissionRate;
    glassEmitter.lifetime      = kGlassLifetime;
    glassEmitter.spawnVelocity = {0.0f, 0.0f, 0.0f};  // no initial velocity -- sphere surface
    glassEmitter.spreadAngle   = kGlassSpreadAngle;    // full hemisphere coverage
    glassEmitter.gravity       = {0.0f, 0.0f, 0.0f};  // no gravity -- glass is static
    glassEmitter.damping       = 1.0f;                 // no damping
    glassEmitter.isEmitting    = true;

    if (!glassEmitter.Initialize(&device, vfxSystem.GetBudget())) {
        std::fprintf(stderr, "[Error] Failed to initialize glass_sphere emitter\n");
        heatEmitter.Shutdown(&device, vfxSystem.GetBudget());
        vfxSystem.Shutdown(&device);
        return 1;
    }

    std::printf("[Init] glass_sphere emitter initialized\n");
    std::printf("       capacity=%u  rate=%.1f/s  lifetime=%.2fs\n\n",
                glassEmitter.allocatedFromBudget, kGlassEmissionRate, kGlassLifetime);

    // Build a basic module stack for glass_sphere (spawn + decay).
    // No UpdateModule with gravity — glass sphere distortion is a static effect.
    {
        auto& stack = glassEmitter.moduleStack;

        auto spawnMod         = std::make_unique<uz::SpawnModule>();
        spawnMod->rate        = kGlassEmissionRate;
        spawnMod->spreadAngle = kGlassSpreadAngle;
        spawnMod->velocity    = {0.0f, 0.0f, 0.0f};
        spawnMod->lifespanMin = 1.5f;
        spawnMod->lifespanMax = kGlassLifetime;
        stack.AddSpawnModule(std::move(spawnMod));

        stack.AddUpdateModule(std::make_unique<uz::DecayModule>());
    }

    // ── Register emitters ────────────────────────────────────────────────
    vfxSystem.RegisterEmitter(&heatEmitter);
    vfxSystem.RegisterEmitter(&glassEmitter);

    std::printf("[Init] Both emitters registered. Active count: %u\n",
                vfxSystem.GetActiveEmitterCount());

    // ── Distortion struct verification ───────────────────────────────────

    std::printf("\n── Distortion Struct Layout ─────────────────────────────────────\n");

    // DistortionConstants: must be exactly 16 bytes.
    static_assert(sizeof(uz::DistortionConstants) == 16,
                  "DistortionConstants must be 16 bytes");
    {
        uz::DistortionConstants dc;
        std::printf("  DistortionConstants:\n");
        std::printf("    sizeof = %zu  (expected 16)\n", sizeof(dc));
        std::printf("    distortionStrength = %.4f  (expected 1.0000)\n",
                    static_cast<double>(dc.distortionStrength));
        std::printf("    normalMapSrvIndex  = %u   (expected UINT32_MAX=%u)\n",
                    dc.normalMapSrvIndex, UINT32_MAX);
        std::printf("    _pad0 = %.4f  _pad1 = %.4f\n",
                    static_cast<double>(dc._pad0),
                    static_cast<double>(dc._pad1));
    }

    // DistortionResolveConstants: must be exactly 16 bytes.
    static_assert(sizeof(uz::DistortionResolveConstants) == 16,
                  "DistortionResolveConstants must be 16 bytes");
    {
        uz::DistortionResolveConstants drc;
        std::printf("  DistortionResolveConstants:\n");
        std::printf("    sizeof = %zu  (expected 16)\n", sizeof(drc));
        std::printf("    colorTextureSrvIndex      = %u\n",
                    drc.colorTextureSrvIndex);
        std::printf("    distortionTextureSrvIndex = %u\n",
                    drc.distortionTextureSrvIndex);
        std::printf("    _pad0 = %u  _pad1 = %u\n",
                    drc._pad0, drc._pad1);
    }

    // DistortionEmitterData: populate with mock handles to verify field access.
    {
        uz::DistortionEmitterData ded;
        // Mock GPU buffer pointer -- points to heat emitter's buffer struct (valid for demo).
        ded.buffer = &heatEmitter.buffer;
        // Assign a mock pipeline handle (heat shimmer distortion PSO).
        ded.pipeline = device.CreateGraphicsPipeline({});
        // Assign mock CB allocations (non-null gpuAddr for heat shimmer).
        ded.billboardCB  = {nullptr, 0x1000, {0, 0}, 0, 256};
        ded.distortionCB = {nullptr, 0x2000, {0, 0}, 0, 256};
        ded.readSetSrvIndex = 42u;
        ded.distanceToCamera = 350.0f;

        std::printf("  DistortionEmitterData (heat_shimmer mock):\n");
        std::printf("    buffer ptr      = %s\n", ded.buffer ? "valid" : "null");
        std::printf("    readSetSrvIndex = %u\n", ded.readSetSrvIndex);
        std::printf("    distanceToCamera= %.2f cm\n",
                    static_cast<double>(ded.distanceToCamera));
        std::printf("    billboardCB.gpuAddr  = 0x%llX\n",
                    static_cast<unsigned long long>(ded.billboardCB.gpuAddr));
        std::printf("    distortionCB.gpuAddr = 0x%llX\n",
                    static_cast<unsigned long long>(ded.distortionCB.gpuAddr));
    }

    std::printf("─────────────────────────────────────────────────────────────────\n\n");

    // ── Simulation loop ──────────────────────────────────────────────────
    std::printf("%-8s  %-14s  %-14s  %-14s  %-14s  %-10s\n",
                "Frame",
                "Heat Spawned",
                "Glass Spawned",
                "Heat Accum",
                "Glass Accum",
                "BudgetUsed");
    std::printf("-------- -------------- -------------- -------------- -------------- ----------\n");

    uint32_t totalHeatSpawned  = 0;
    uint32_t totalGlassSpawned = 0;
    uint32_t peakHeatPerFrame  = 0;
    uint32_t peakGlassPerFrame = 0;

    const auto startTime = std::chrono::high_resolution_clock::now();

    for (int frame = 0; frame < kTotalFrames; ++frame) {
        const uint32_t heatSpawn  = heatEmitter.CalculateSpawnCount(kDt);
        const uint32_t glassSpawn = glassEmitter.CalculateSpawnCount(kDt);

        totalHeatSpawned  += heatSpawn;
        totalGlassSpawned += glassSpawn;
        peakHeatPerFrame   = std::max(peakHeatPerFrame,  heatSpawn);
        peakGlassPerFrame  = std::max(peakGlassPerFrame, glassSpawn);

        vfxSystem.Tick(kDt);

        if (frame == 0 || frame == kTotalFrames - 1 ||
            (frame + 1) % kReportInterval == 0) {
            std::printf("%-8d  %-14u  %-14u  %-14.6f  %-14.6f  %-10u\n",
                        frame,
                        heatSpawn,
                        glassSpawn,
                        static_cast<double>(heatEmitter.spawnAccumulator),
                        static_cast<double>(glassEmitter.spawnAccumulator),
                        vfxSystem.GetBudget().GetUsed());
        }
    }

    const auto endTime = std::chrono::high_resolution_clock::now();
    const double elapsedMs =
        std::chrono::duration<double, std::milli>(endTime - startTime).count();

    // ── Summary ────────────────────────────────────────────────────────
    std::printf("\n==========================================================\n");
    std::printf("DISTORTION EFFECT DEMO -- SUMMARY\n");
    std::printf("==========================================================\n");
    std::printf("  Frames simulated:          %d (%.1f seconds @ 60 FPS)\n",
                kTotalFrames, kTotalFrames * kDt);
    std::printf("\n");
    std::printf("  heat_shimmer emitter:\n");
    std::printf("    Total particles spawned: %u\n",  totalHeatSpawned);
    std::printf("    Peak spawn/frame:        %u\n",  peakHeatPerFrame);
    std::printf("    Final accumulator:       %.6f\n",
                static_cast<double>(heatEmitter.spawnAccumulator));
    std::printf("    Distortion strength:     %.4f (default)\n", 1.0);
    std::printf("    Velocity:                (0, 3, 0) cm/s upward column\n");
    std::printf("    Lifetime:                %.2f s\n", kHeatLifetime);
    std::printf("\n");
    std::printf("  glass_sphere emitter:\n");
    std::printf("    Total particles spawned: %u\n",  totalGlassSpawned);
    std::printf("    Peak spawn/frame:        %u\n",  peakGlassPerFrame);
    std::printf("    Final accumulator:       %.6f\n",
                static_cast<double>(glassEmitter.spawnAccumulator));
    std::printf("    Distortion strength:     %.4f (default)\n", 1.0);
    std::printf("    Spawn shape:             Point (spread=%.0f deg hemisphere)\n",
                static_cast<double>(kGlassSpreadAngle));
    std::printf("    Lifetime:                %.2f s\n", kGlassLifetime);
    std::printf("\n");
    std::printf("  Budget used:               %u / %u\n",
                vfxSystem.GetBudget().GetUsed(),
                vfxSystem.GetBudget().GetMax());
    std::printf("  CPU wall time:             %.2f ms\n", elapsedMs);
    std::printf("\n");
    std::printf("  DistortionConstants sizeof:        %zu bytes (expected 16)\n",
                sizeof(uz::DistortionConstants));
    std::printf("  DistortionResolveConstants sizeof: %zu bytes (expected 16)\n",
                sizeof(uz::DistortionResolveConstants));
    std::printf("==========================================================\n");

    // ── Validation ─────────────────────────────────────────────────────
    bool pass = true;

    // Emitters must have initialized.
    if (!heatEmitter.initialized) {
        std::fprintf(stderr, "[FAIL] heat_shimmer emitter not initialized\n");
        pass = false;
    }
    if (!glassEmitter.initialized) {
        std::fprintf(stderr, "[FAIL] glass_sphere emitter not initialized\n");
        pass = false;
    }

    // Both emitters must have spawned particles over the 2-second run.
    // heat_shimmer: 200/s x 2s = 400, glass_sphere: 100/s x 2s = 200.
    // Apply a tolerance of 10 for accumulator rounding.
    {
        const uint32_t expectedHeat  = static_cast<uint32_t>(kHeatEmissionRate  * kTotalFrames * kDt);
        const uint32_t expectedGlass = static_cast<uint32_t>(kGlassEmissionRate * kTotalFrames * kDt);
        const uint32_t tolerance = 10;

        if (totalHeatSpawned < expectedHeat - tolerance ||
            totalHeatSpawned > expectedHeat + tolerance) {
            std::fprintf(stderr, "[FAIL] heat_shimmer: expected ~%u spawns, got %u\n",
                         expectedHeat, totalHeatSpawned);
            pass = false;
        }
        if (totalGlassSpawned < expectedGlass - tolerance ||
            totalGlassSpawned > expectedGlass + tolerance) {
            std::fprintf(stderr, "[FAIL] glass_sphere: expected ~%u spawns, got %u\n",
                         expectedGlass, totalGlassSpawned);
            pass = false;
        }
    }

    // Spawn accumulators must be in [0, 1).
    if (heatEmitter.spawnAccumulator < 0.0f || heatEmitter.spawnAccumulator >= 1.0f) {
        std::fprintf(stderr, "[FAIL] heat_shimmer accumulator out of range: %.6f\n",
                     static_cast<double>(heatEmitter.spawnAccumulator));
        pass = false;
    }
    if (glassEmitter.spawnAccumulator < 0.0f || glassEmitter.spawnAccumulator >= 1.0f) {
        std::fprintf(stderr, "[FAIL] glass_sphere accumulator out of range: %.6f\n",
                     static_cast<double>(glassEmitter.spawnAccumulator));
        pass = false;
    }

    // DistortionConstants: verify default field values.
    {
        const uz::DistortionConstants dc;
        if (dc.distortionStrength != 1.0f) {
            std::fprintf(stderr, "[FAIL] DistortionConstants.distortionStrength = %.4f, expected 1.0\n",
                         static_cast<double>(dc.distortionStrength));
            pass = false;
        }
        if (dc.normalMapSrvIndex != UINT32_MAX) {
            std::fprintf(stderr, "[FAIL] DistortionConstants.normalMapSrvIndex = %u, expected UINT32_MAX\n",
                         dc.normalMapSrvIndex);
            pass = false;
        }
        if (dc._pad0 != 0.0f || dc._pad1 != 0.0f) {
            std::fprintf(stderr, "[FAIL] DistortionConstants padding is non-zero\n");
            pass = false;
        }
        if (sizeof(dc) != 16) {
            std::fprintf(stderr, "[FAIL] DistortionConstants sizeof=%zu, expected 16\n",
                         sizeof(dc));
            pass = false;
        }
    }

    // DistortionResolveConstants: verify default field values and size.
    {
        const uz::DistortionResolveConstants drc;
        if (drc.colorTextureSrvIndex != UINT32_MAX) {
            std::fprintf(stderr,
                         "[FAIL] DistortionResolveConstants.colorTextureSrvIndex = %u, expected UINT32_MAX\n",
                         drc.colorTextureSrvIndex);
            pass = false;
        }
        if (drc.distortionTextureSrvIndex != UINT32_MAX) {
            std::fprintf(stderr,
                         "[FAIL] DistortionResolveConstants.distortionTextureSrvIndex = %u, expected UINT32_MAX\n",
                         drc.distortionTextureSrvIndex);
            pass = false;
        }
        if (sizeof(drc) != 16) {
            std::fprintf(stderr, "[FAIL] DistortionResolveConstants sizeof=%zu, expected 16\n",
                         sizeof(drc));
            pass = false;
        }
    }

    // Budget must account for both emitters.
    {
        const uint32_t expectedBudgetUsed =
            heatEmitter.allocatedFromBudget + glassEmitter.allocatedFromBudget;
        if (vfxSystem.GetBudget().GetUsed() != expectedBudgetUsed) {
            std::fprintf(stderr,
                         "[FAIL] Budget used=%u, expected %u (heat=%u + glass=%u)\n",
                         vfxSystem.GetBudget().GetUsed(),
                         expectedBudgetUsed,
                         heatEmitter.allocatedFromBudget,
                         glassEmitter.allocatedFromBudget);
            pass = false;
        }
    }

    // ── Cleanup ────────────────────────────────────────────────────────
    vfxSystem.UnregisterEmitter(&heatEmitter);
    vfxSystem.UnregisterEmitter(&glassEmitter);

    heatEmitter.Shutdown(&device, vfxSystem.GetBudget());
    glassEmitter.Shutdown(&device, vfxSystem.GetBudget());

    if (vfxSystem.GetBudget().GetUsed() != 0) {
        std::fprintf(stderr, "[FAIL] Budget not fully returned after shutdown: %u\n",
                     vfxSystem.GetBudget().GetUsed());
        pass = false;
    }

    vfxSystem.Shutdown(&device);

    // ── Result ─────────────────────────────────────────────────────────
    if (pass) {
        std::printf("\n[PASS] Distortion effect demo completed successfully.\n");
        return 0;
    } else {
        std::fprintf(stderr, "\n[FAIL] One or more validation checks failed.\n");
        return 1;
    }
}
