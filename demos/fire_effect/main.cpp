/// Fire Effect Demo -- Headless VFX module system proof-of-concept.
///
/// Demonstrates the composable module stack by building a fire-like particle
/// effect with SpawnModule, UpdateModule, TurbulenceModule, SpriteRenderModule,
/// and DecayModule. The ModuleCompositor stitches their HLSL snippets into a
/// single compute shader and prints the generated source.
///
/// This is a headless (no window, no GPU) demo that validates:
/// - EmitterModuleStack lifecycle (add modules, compose, query)
/// - ModuleCompositor HLSL generation from module snippets
/// - ModuleRegistry built-in registration and factory creation
/// - VFXSystem + ParticleEmitter budget allocation and spawn accumulator
/// - Module parameter inspection and reporting
///
/// # Build
/// Compiled as part of the engine test suite or standalone:
///   zig build -Ddemo=fire_effect
///
/// # Parameters (fire-like)
/// - velocity:       [0, 8, 0]   (upward flame)
/// - gravity:        [0, -3, 0]  (gentle downdraft)
/// - damping:        0.98
/// - lifetime:       0.5 -- 2.0 s
/// - emission_rate:  500 particles/sec
/// - max_particles:  25000
/// - turbulence:     strength=2.0, freq=0.5, octaves=3
/// - sprite:         0.5 x 0.5 world units

#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
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
#include "engine/vfx/modules/module_compositor.h"
#include "engine/vfx/modules/module_registry.h"
#include "engine/vfx/modules/register_builtin_modules.h"
#include "engine/vfx/modules/spawn_module.h"
#include "engine/vfx/modules/update_module.h"
#include "engine/vfx/modules/decay_module.h"
#include "engine/vfx/modules/turbulence_module.h"
#include "engine/vfx/modules/sprite_render_module.h"

// ─────────────────────────────────────────────────────────────────────────────
// Demo parameters
// ─────────────────────────────────────────────────────────────────────────────

static constexpr uint32_t kMaxParticles   = 25000u;
static constexpr float    kEmissionRate   = 500.0f;    // particles/sec
static constexpr float    kLifetimeMin    = 0.5f;      // seconds
static constexpr float    kLifetimeMax    = 2.0f;      // seconds
static constexpr float    kDamping        = 0.98f;
static constexpr float    kDt             = 1.0f / 60.0f;
static constexpr int      kTotalFrames    = 120;       // 2 seconds at 60 FPS
static constexpr int      kReportInterval = 30;        // print stats every N frames
static constexpr uint32_t kBudgetMax      = 500000u;

/// Spread angle converted to radians for SpawnModule configuration.
static constexpr float kSpreadAngleDeg = 60.0f;

// ─────────────────────────────────────────────────────────────────────────────
// Helpers
// ─────────────────────────────────────────────────────────────────────────────

namespace {

/// Prints a section of the composed HLSL, truncated to a sensible preview length.
void PrintComposedHlsl(const std::string& hlsl) {
    static constexpr size_t kMaxPreviewLen = 2000;
    std::printf("\n── Composed Update Shader (HLSL) ──────────────────────────────────\n");
    if (hlsl.empty()) {
        std::printf("  (empty -- composition failed)\n");
        return;
    }
    const size_t printLen = std::min(hlsl.size(), kMaxPreviewLen);
    std::printf("%.*s", static_cast<int>(printLen), hlsl.c_str());
    if (hlsl.size() > kMaxPreviewLen) {
        std::printf("\n  ... (truncated, %zu total chars)\n",
                    hlsl.size());
    }
    std::printf("\n───────────────────────────────────────────────────────────────────\n");
}

/// Prints parameter definitions for a single module.
void PrintModuleParams(const uz::IVFXModule* mod) {
    const auto params = mod->GetParameters();
    std::printf("  [%s] (%zu params)\n", mod->GetName(),
                params.size());
    for (const auto& p : params) {
        switch (p.type) {
        case uz::ParameterType::Float:
            std::printf("    %-22s  Float   default=%.3f\n", p.name, static_cast<double>(p.defaultFloat));
            break;
        case uz::ParameterType::Vec3:
            std::printf("    %-22s  Vec3    default=[%.2f, %.2f, %.2f]\n",
                        p.name,
                        static_cast<double>(p.defaultVec3.x),
                        static_cast<double>(p.defaultVec3.y),
                        static_cast<double>(p.defaultVec3.z));
            break;
        case uz::ParameterType::Int:
            std::printf("    %-22s  Int     default=%d\n", p.name, p.defaultInt);
            break;
        case uz::ParameterType::Bool:
            std::printf("    %-22s  Bool    default=%s\n", p.name, p.defaultBool ? "true" : "false");
            break;
        default:
            std::printf("    %-22s  (type=%d)\n", p.name, static_cast<int>(p.type));
            break;
        }
    }
}

} // anonymous namespace

// ─────────────────────────────────────────────────────────────────────────────
// Main
// ─────────────────────────────────────────────────────────────────────────────

int main() {
    std::printf("==========================================================\n");
    std::printf("         UzEngine -- Fire Effect Demo\n");
    std::printf("   Headless VFX module system (no GPU required)\n");
    std::printf("==========================================================\n\n");

    // ── Setup ──────────────────────────────────────────────────────────
    DemoMockDevice device;

    uz::VFXSystem vfxSystem;
    vfxSystem.Initialize(&device, kBudgetMax);
    std::printf("[Init] VFXSystem initialized -- budget: %u particles\n", kBudgetMax);

    // Verify built-in modules are registered.
    const auto& registry = vfxSystem.GetModuleRegistry();
    const auto names = registry.GetRegisteredNames();
    std::printf("[Init] ModuleRegistry has %zu built-in modules:", names.size());
    for (const auto& n : names) {
        std::printf(" %s", n.c_str());
    }
    std::printf("\n");

    // ── Emitter ────────────────────────────────────────────────────────
    uz::ParticleEmitter emitter;
    emitter.maxParticles  = kMaxParticles;
    emitter.emissionRate  = kEmissionRate;
    emitter.lifetime      = kLifetimeMax;
    emitter.gravity       = {0.0f, -3.0f, 0.0f};
    emitter.damping       = kDamping;
    emitter.spawnVelocity = {0.0f, 8.0f, 0.0f};
    emitter.spreadAngle   = kSpreadAngleDeg;
    emitter.isEmitting    = true;

    if (!emitter.Initialize(&device, vfxSystem.GetBudget())) {
        std::fprintf(stderr, "[Error] Failed to initialize emitter\n");
        vfxSystem.Shutdown(&device);
        return 1;
    }

    std::printf("[Init] Emitter initialized -- capacity: %u, budget used: %u/%u\n",
                emitter.allocatedFromBudget,
                vfxSystem.GetBudget().GetUsed(),
                vfxSystem.GetBudget().GetMax());

    // ── Build module stack ─────────────────────────────────────────────
    auto& stack = emitter.moduleStack;

    // Spawn module with fire parameters.
    auto spawnMod = std::make_unique<uz::SpawnModule>();
    spawnMod->rate            = kEmissionRate;
    spawnMod->spreadAngle     = kSpreadAngleDeg;
    spawnMod->velocity        = {0.0f, 8.0f, 0.0f};
    spawnMod->lifespanMin     = kLifetimeMin;
    spawnMod->lifespanMax     = kLifetimeMax;
    stack.AddSpawnModule(std::move(spawnMod));

    // Update module with gentle gravity and drag.
    auto updateMod = std::make_unique<uz::UpdateModule>();
    updateMod->gravity = {0.0f, -3.0f, 0.0f};
    updateMod->drag    = kDamping;
    stack.AddUpdateModule(std::move(updateMod));

    // Turbulence for organic flame flicker.
    auto turbMod = std::make_unique<uz::TurbulenceModule>();
    turbMod->strength  = 2.0f;
    turbMod->frequency = 0.5f;
    turbMod->octaves   = 3;
    stack.AddUpdateModule(std::move(turbMod));

    // Sprite render for billboard visuals.
    auto spriteMod = std::make_unique<uz::SpriteRenderModule>();
    spriteMod->spriteWidth  = 0.5f;
    spriteMod->spriteHeight = 0.5f;
    stack.SetRenderModule(std::move(spriteMod));

    // Decay must be last in update order.
    stack.AddUpdateModule(std::make_unique<uz::DecayModule>());

    std::printf("[Init] Module stack: %zu spawn, %zu update, render=%s\n",
                stack.spawnModules.size(),
                stack.updateModules.size(),
                stack.renderModule ? stack.renderModule->GetName() : "none");

    // ── Compose HLSL via ModuleCompositor ──────────────────────────────
    uz::ModuleCompositor compositor;
    const auto updatePtrs = stack.GetUpdateModulePtrs();
    const auto composed   = compositor.ComposeUpdateShader(updatePtrs);

    PrintComposedHlsl(composed.hlslSource);
    std::printf("[Compose] Cache key: 0x%016llX\n",
                static_cast<unsigned long long>(composed.cacheKey));

    // ── Register emitter and run simulation ────────────────────────────
    vfxSystem.RegisterEmitter(&emitter);

    std::printf("\n%-8s  %-12s  %-12s  %-14s  %-10s\n",
                "Frame", "Spawned", "Accumulator", "Budget Used", "Emitters");
    std::printf("-------- ------------ ------------ -------------- ----------\n");

    uint32_t totalSpawned     = 0;
    uint32_t peakSpawnPerFrame = 0;

    const auto startTime = std::chrono::high_resolution_clock::now();

    for (int frame = 0; frame < kTotalFrames; ++frame) {
        const uint32_t spawnCount = emitter.CalculateSpawnCount(kDt);
        totalSpawned += spawnCount;
        peakSpawnPerFrame = std::max(peakSpawnPerFrame, spawnCount);

        vfxSystem.Tick(kDt);

        if (frame == 0 || frame == kTotalFrames - 1 ||
            (frame + 1) % kReportInterval == 0) {
            std::printf("%-8d  %-12u  %-12.6f  %-14u  %-10u\n",
                        frame,
                        spawnCount,
                        static_cast<double>(emitter.spawnAccumulator),
                        vfxSystem.GetBudget().GetUsed(),
                        vfxSystem.GetActiveEmitterCount());
        }
    }

    const auto endTime = std::chrono::high_resolution_clock::now();
    const double elapsedMs =
        std::chrono::duration<double, std::milli>(endTime - startTime).count();

    // ── Summary ────────────────────────────────────────────────────────
    std::printf("\n--------------------------------------------------\n");
    std::printf("SIMULATION SUMMARY\n");
    std::printf("--------------------------------------------------\n");
    std::printf("  Frames simulated:       %d (%.1f seconds)\n",
                kTotalFrames, kTotalFrames * kDt);
    std::printf("  Total particles spawned: %u\n", totalSpawned);
    std::printf("  Peak spawn/frame:        %u\n", peakSpawnPerFrame);
    std::printf("  Final accumulator:       %.6f\n",
                static_cast<double>(emitter.spawnAccumulator));
    std::printf("  Budget used:             %u / %u\n",
                vfxSystem.GetBudget().GetUsed(),
                vfxSystem.GetBudget().GetMax());
    std::printf("  CPU wall time:           %.2f ms\n", elapsedMs);
    std::printf("--------------------------------------------------\n");

    // ── Parameter summary ──────────────────────────────────────────────
    std::printf("\nMODULE PARAMETER SUMMARY\n");
    std::printf("--------------------------------------------------\n");
    std::printf("Spawn modules:\n");
    for (const auto& m : stack.spawnModules) {
        PrintModuleParams(m.get());
    }
    std::printf("Update modules:\n");
    for (const auto& m : stack.updateModules) {
        PrintModuleParams(m.get());
    }
    if (stack.renderModule) {
        std::printf("Render module:\n");
        PrintModuleParams(stack.renderModule.get());
    }
    std::printf("--------------------------------------------------\n");

    // ── Validation ─────────────────────────────────────────────────────
    bool pass = true;

    // Module stack must have correct counts.
    if (stack.spawnModules.size() != 1) {
        std::fprintf(stderr, "[FAIL] Expected 1 spawn module, got %zu\n",
                     stack.spawnModules.size());
        pass = false;
    }
    if (stack.updateModules.size() != 3) {
        std::fprintf(stderr, "[FAIL] Expected 3 update modules, got %zu\n",
                     stack.updateModules.size());
        pass = false;
    }
    if (!stack.renderModule) {
        std::fprintf(stderr, "[FAIL] Render module is null\n");
        pass = false;
    }

    // Composed HLSL must not be empty.
    if (composed.hlslSource.empty()) {
        std::fprintf(stderr, "[FAIL] Composed update shader is empty\n");
        pass = false;
    }
    if (composed.cacheKey == 0) {
        std::fprintf(stderr, "[FAIL] Composed cache key is zero\n");
        pass = false;
    }

    // At 500/sec x 2sec = 1000 expected spawns (tolerance for accumulator rounding).
    const uint32_t expectedSpawns = static_cast<uint32_t>(kEmissionRate * kTotalFrames * kDt);
    const uint32_t tolerance = 10;
    if (totalSpawned < expectedSpawns - tolerance ||
        totalSpawned > expectedSpawns + tolerance) {
        std::fprintf(stderr, "[FAIL] Expected ~%u total spawns, got %u\n",
                     expectedSpawns, totalSpawned);
        pass = false;
    }

    // Accumulator must be in [0, 1).
    if (emitter.spawnAccumulator < 0.0f || emitter.spawnAccumulator >= 1.0f) {
        std::fprintf(stderr, "[FAIL] Accumulator out of range: %.6f\n",
                     static_cast<double>(emitter.spawnAccumulator));
        pass = false;
    }

    // Budget must still show the emitter's allocation.
    if (vfxSystem.GetBudget().GetUsed() != kMaxParticles) {
        std::fprintf(stderr, "[FAIL] Budget used=%u, expected %u\n",
                     vfxSystem.GetBudget().GetUsed(), kMaxParticles);
        pass = false;
    }

    // ── Cleanup ────────────────────────────────────────────────────────
    vfxSystem.UnregisterEmitter(&emitter);
    emitter.Shutdown(&device, vfxSystem.GetBudget());

    if (vfxSystem.GetBudget().GetUsed() != 0) {
        std::fprintf(stderr, "[FAIL] Budget not fully returned after shutdown: %u\n",
                     vfxSystem.GetBudget().GetUsed());
        pass = false;
    }

    vfxSystem.Shutdown(&device);

    // ── Result ─────────────────────────────────────────────────────────
    if (pass) {
        std::printf("\n[PASS] Fire effect demo completed successfully.\n");
        return 0;
    } else {
        std::fprintf(stderr, "\n[FAIL] One or more validation checks failed.\n");
        return 1;
    }
}
