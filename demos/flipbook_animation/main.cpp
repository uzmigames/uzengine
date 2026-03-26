/// Flipbook Animation Demo -- Headless VFX flipbook (SubUV) sprite sheet proof-of-concept.
///
/// Demonstrates the SpriteRenderModule flipbook animation feature by building a
/// fire-sprite particle effect with a 4x4 sprite sheet (16 frames at 24 FPS).
/// Uses FlipbookBlendMode::Linear for smooth inter-frame interpolation.
///
/// This is a headless (no window, no GPU) demo that validates:
/// - SpriteRenderModule flipbook configuration (rows, cols, totalFrames, FPS, blendMode)
/// - HasFlipbook() returns true when rows or cols > 1
/// - EmitterModuleStack correctly holds the sprite_render module
/// - SpriteRenderModule::GetParameters() exposes all flipbook parameters
/// - Per-frame flipbook frame index computed from particle age * fps
/// - VFXSystem + ParticleEmitter budget allocation and spawn accumulator
///
/// # Build
///   cd demos/flipbook_animation
///   zig build
///   ./zig-out/bin/flipbook_animation
///
/// # Flipbook parameters (4x4 fire sprite sheet)
/// - rows:          4
/// - cols:          4
/// - totalFrames:   16
/// - fps:           24.0
/// - blendMode:     Linear
/// - sprite:        1.0 x 1.0 world units

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
#include "engine/vfx/modules/sprite_render_module.h"

// ─────────────────────────────────────────────────────────────────────────────
// Demo parameters
// ─────────────────────────────────────────────────────────────────────────────

static constexpr uint32_t kMaxParticles   = 10000u;
static constexpr float    kEmissionRate   = 200.0f;    // particles/sec
static constexpr float    kLifetimeMin    = 0.5f;      // seconds
static constexpr float    kLifetimeMax    = 2.0f;      // seconds
static constexpr float    kDt             = 1.0f / 60.0f;
static constexpr int      kTotalFrames    = 120;       // 2 seconds at 60 FPS
static constexpr int      kReportInterval = 30;        // print stats every N frames
static constexpr uint32_t kBudgetMax      = 500000u;

// Flipbook parameters for a 4x4 fire sprite sheet.
static constexpr uint32_t kFlipbookRows        = 4u;
static constexpr uint32_t kFlipbookCols        = 4u;
static constexpr uint32_t kFlipbookTotalFrames = 16u;
static constexpr float    kFlipbookFPS         = 24.0f;

// ─────────────────────────────────────────────────────────────────────────────
// Helpers
// ─────────────────────────────────────────────────────────────────────────────

namespace {

/// Computes the flipbook frame index a particle would display given its age.
///
/// @ref Nguyen 2007 "GPU Gems 3" ch. 23 -- SubUV frame selection: frameIndex = floor(age * fps) % totalFrames
[[nodiscard]] uint32_t ComputeFlipbookFrame(float age, float fps, uint32_t totalFrames) noexcept {
    const auto rawFrame = static_cast<uint32_t>(age * fps);
    return rawFrame % totalFrames;
}

/// Prints parameter definitions for a single module.
void PrintModuleParams(const uz::IVFXModule* mod) {
    const auto params = mod->GetParameters();
    std::printf("  [%s] (%zu params)\n", mod->GetName(), params.size());
    for (const auto& p : params) {
        switch (p.type) {
        case uz::ParameterType::Float:
            std::printf("    %-26s  Float   default=%.3f\n", p.name, static_cast<double>(p.defaultFloat));
            break;
        case uz::ParameterType::Vec3:
            std::printf("    %-26s  Vec3    default=[%.2f, %.2f, %.2f]\n",
                        p.name,
                        static_cast<double>(p.defaultVec3.x),
                        static_cast<double>(p.defaultVec3.y),
                        static_cast<double>(p.defaultVec3.z));
            break;
        case uz::ParameterType::Int:
            std::printf("    %-26s  Int     default=%d\n", p.name, p.defaultInt);
            break;
        case uz::ParameterType::Bool:
            std::printf("    %-26s  Bool    default=%s\n", p.name, p.defaultBool ? "true" : "false");
            break;
        default:
            std::printf("    %-26s  (type=%d)\n", p.name, static_cast<int>(p.type));
            break;
        }
    }
}

/// Returns true if a parameter with the given name exists in the parameter list.
[[nodiscard]] bool HasParameter(const std::vector<uz::ParameterDef>& params,
                                const char* name) noexcept {
    for (const auto& p : params) {
        if (std::string_view{p.name} == name) {
            return true;
        }
    }
    return false;
}

} // anonymous namespace

// ─────────────────────────────────────────────────────────────────────────────
// Main
// ─────────────────────────────────────────────────────────────────────────────

int main() {
    std::printf("==========================================================\n");
    std::printf("      UzEngine -- Flipbook Animation Demo\n");
    std::printf("  Headless VFX flipbook sprite sheet (no GPU required)\n");
    std::printf("==========================================================\n\n");

    std::printf("[Config] Sprite sheet: %ux%u grid, %u frames @ %.1f FPS\n",
                kFlipbookRows, kFlipbookCols,
                kFlipbookTotalFrames,
                static_cast<double>(kFlipbookFPS));
    std::printf("[Config] Blend mode: Linear\n\n");

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
    emitter.gravity       = {0.0f, -2.0f, 0.0f};
    emitter.damping       = 0.97f;
    emitter.spawnVelocity = {0.0f, 5.0f, 0.0f};
    emitter.spreadAngle   = 45.0f;
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

    // Spawn module with fire-sprite parameters.
    auto spawnMod = std::make_unique<uz::SpawnModule>();
    spawnMod->rate        = kEmissionRate;
    spawnMod->spreadAngle = 45.0f;
    spawnMod->velocity    = {0.0f, 5.0f, 0.0f};
    spawnMod->lifespanMin = kLifetimeMin;
    spawnMod->lifespanMax = kLifetimeMax;
    stack.AddSpawnModule(std::move(spawnMod));

    // Update module with gentle upward motion and drag.
    auto updateMod = std::make_unique<uz::UpdateModule>();
    updateMod->gravity = {0.0f, -2.0f, 0.0f};
    updateMod->drag    = 0.97f;
    stack.AddUpdateModule(std::move(updateMod));

    // Decay must be last in update order.
    stack.AddUpdateModule(std::make_unique<uz::DecayModule>());

    // Sprite render module with flipbook configured for a 4x4 fire sprite sheet.
    // @ref Nguyen 2007 "GPU Gems 3" ch. 23 -- SubUV animation
    auto spriteMod = std::make_unique<uz::SpriteRenderModule>();
    spriteMod->spriteWidth         = 1.0f;
    spriteMod->spriteHeight        = 1.0f;
    spriteMod->flipbookRows        = kFlipbookRows;
    spriteMod->flipbookCols        = kFlipbookCols;
    spriteMod->flipbookTotalFrames = kFlipbookTotalFrames;
    spriteMod->flipbookFPS         = kFlipbookFPS;
    spriteMod->flipbookBlendMode   = uz::FlipbookBlendMode::Linear;
    stack.SetRenderModule(std::move(spriteMod));

    std::printf("[Init] Module stack: %zu spawn, %zu update, render=%s\n",
                stack.spawnModules.size(),
                stack.updateModules.size(),
                stack.renderModule ? stack.renderModule->GetName() : "none");

    // ── Verify flipbook state on the render module ─────────────────────
    // HasFlipbook() must return true for a 4x4 sheet.
    const auto* spriteModPtr =
        static_cast<const uz::SpriteRenderModule*>(stack.renderModule.get());

    std::printf("[Flipbook] HasFlipbook(): %s\n",
                spriteModPtr->HasFlipbook() ? "true" : "false");
    std::printf("[Flipbook] Rows=%u Cols=%u TotalFrames=%u FPS=%.1f BlendMode=Linear\n",
                spriteModPtr->flipbookRows,
                spriteModPtr->flipbookCols,
                spriteModPtr->flipbookTotalFrames,
                static_cast<double>(spriteModPtr->flipbookFPS));

    // ── Register emitter and run simulation ────────────────────────────
    vfxSystem.RegisterEmitter(&emitter);

    std::printf("\n%-8s  %-12s  %-12s  %-14s  %-12s\n",
                "Frame", "Spawned", "Accumulator", "Budget Used", "FlipFrame");
    std::printf("-------- ------------ ------------ -------------- ------------\n");

    uint32_t totalSpawned      = 0;
    uint32_t peakSpawnPerFrame = 0;

    // Simulated particle ages for demonstrating flipbook frame calculation.
    // We track 4 representative ages at fixed intervals within the lifetime range.
    const float kSampleAges[4] = {0.0f, kLifetimeMax * 0.25f,
                                   kLifetimeMax * 0.5f, kLifetimeMax * 0.75f};

    const auto startTime = std::chrono::high_resolution_clock::now();

    for (int frame = 0; frame < kTotalFrames; ++frame) {
        const uint32_t spawnCount = emitter.CalculateSpawnCount(kDt);
        totalSpawned += spawnCount;
        peakSpawnPerFrame = std::max(peakSpawnPerFrame, spawnCount);

        vfxSystem.Tick(kDt);

        if (frame == 0 || frame == kTotalFrames - 1 ||
            (frame + 1) % kReportInterval == 0) {
            // Compute flipbook frame for a particle at the median age (half lifetime).
            const float  medianAge   = kLifetimeMax * 0.5f;
            const float  currentAge  = medianAge + frame * kDt;
            const uint32_t flipFrame = ComputeFlipbookFrame(
                currentAge, kFlipbookFPS, kFlipbookTotalFrames);

            std::printf("%-8d  %-12u  %-12.6f  %-14u  %-12u\n",
                        frame,
                        spawnCount,
                        static_cast<double>(emitter.spawnAccumulator),
                        vfxSystem.GetBudget().GetUsed(),
                        flipFrame);
        }
    }

    const auto endTime = std::chrono::high_resolution_clock::now();
    const double elapsedMs =
        std::chrono::duration<double, std::milli>(endTime - startTime).count();

    // ── Per-age flipbook frame report ──────────────────────────────────
    std::printf("\nFLIPBOOK FRAME SAMPLE (at end of simulation)\n");
    std::printf("--------------------------------------------------\n");
    std::printf("  %-12s  %-10s\n", "Age (sec)", "Frame Index");
    std::printf("  %-12s  %-10s\n", "-----------", "----------");
    for (const float age : kSampleAges) {
        const uint32_t frameIdx = ComputeFlipbookFrame(age, kFlipbookFPS, kFlipbookTotalFrames);
        std::printf("  %-12.4f  %-10u\n", static_cast<double>(age), frameIdx);
    }

    // ── Summary ────────────────────────────────────────────────────────
    std::printf("\n--------------------------------------------------\n");
    std::printf("SIMULATION SUMMARY\n");
    std::printf("--------------------------------------------------\n");
    std::printf("  Frames simulated:        %d (%.1f seconds)\n",
                kTotalFrames, kTotalFrames * kDt);
    std::printf("  Total particles spawned: %u\n", totalSpawned);
    std::printf("  Peak spawn/frame:        %u\n", peakSpawnPerFrame);
    std::printf("  Final accumulator:       %.6f\n",
                static_cast<double>(emitter.spawnAccumulator));
    std::printf("  Budget used:             %u / %u\n",
                vfxSystem.GetBudget().GetUsed(),
                vfxSystem.GetBudget().GetMax());
    std::printf("  CPU wall time:           %.2f ms\n", elapsedMs);
    std::printf("\n  Flipbook configuration:\n");
    std::printf("    Rows x Cols:           %u x %u\n",
                spriteModPtr->flipbookRows, spriteModPtr->flipbookCols);
    std::printf("    Total frames:          %u\n", spriteModPtr->flipbookTotalFrames);
    std::printf("    Playback FPS:          %.1f\n", static_cast<double>(spriteModPtr->flipbookFPS));
    std::printf("    Blend mode:            Linear\n");
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

    // HasFlipbook() must return true for a 4x4 sheet.
    if (!spriteModPtr->HasFlipbook()) {
        std::fprintf(stderr, "[FAIL] HasFlipbook() returned false for %ux%u sheet\n",
                     kFlipbookRows, kFlipbookCols);
        pass = false;
    }

    // Module stack must have the sprite_render render module.
    if (!stack.renderModule) {
        std::fprintf(stderr, "[FAIL] Render module is null\n");
        pass = false;
    } else if (std::string_view{stack.renderModule->GetName()} != "sprite_render") {
        std::fprintf(stderr, "[FAIL] Render module name: expected 'sprite_render', got '%s'\n",
                     stack.renderModule->GetName());
        pass = false;
    }

    // Module stack counts.
    if (stack.spawnModules.size() != 1) {
        std::fprintf(stderr, "[FAIL] Expected 1 spawn module, got %zu\n",
                     stack.spawnModules.size());
        pass = false;
    }
    if (stack.updateModules.size() != 2) {
        std::fprintf(stderr, "[FAIL] Expected 2 update modules, got %zu\n",
                     stack.updateModules.size());
        pass = false;
    }

    // GetParameters() must expose all flipbook parameters.
    const auto spriteParams = spriteModPtr->GetParameters();
    const char* kRequiredParams[] = {
        "flipbook_rows",
        "flipbook_cols",
        "flipbook_total_frames",
        "flipbook_fps",
        "flipbook_blend_mode",
    };
    for (const char* pname : kRequiredParams) {
        if (!HasParameter(spriteParams, pname)) {
            std::fprintf(stderr, "[FAIL] SpriteRenderModule missing parameter: '%s'\n", pname);
            pass = false;
        }
    }

    // Flipbook fields must match what was configured.
    if (spriteModPtr->flipbookRows != kFlipbookRows) {
        std::fprintf(stderr, "[FAIL] flipbookRows: expected %u, got %u\n",
                     kFlipbookRows, spriteModPtr->flipbookRows);
        pass = false;
    }
    if (spriteModPtr->flipbookCols != kFlipbookCols) {
        std::fprintf(stderr, "[FAIL] flipbookCols: expected %u, got %u\n",
                     kFlipbookCols, spriteModPtr->flipbookCols);
        pass = false;
    }
    if (spriteModPtr->flipbookTotalFrames != kFlipbookTotalFrames) {
        std::fprintf(stderr, "[FAIL] flipbookTotalFrames: expected %u, got %u\n",
                     kFlipbookTotalFrames, spriteModPtr->flipbookTotalFrames);
        pass = false;
    }
    if (spriteModPtr->flipbookBlendMode != uz::FlipbookBlendMode::Linear) {
        std::fprintf(stderr, "[FAIL] flipbookBlendMode: expected Linear\n");
        pass = false;
    }

    // Flipbook frame index must wrap correctly.
    // At age=0, fps=24, totalFrames=16: frame 0.
    // At age=1.0, fps=24, totalFrames=16: floor(24) % 16 = 8.
    {
        const uint32_t frame0 = ComputeFlipbookFrame(0.0f, kFlipbookFPS, kFlipbookTotalFrames);
        if (frame0 != 0u) {
            std::fprintf(stderr, "[FAIL] Frame at age=0 expected 0, got %u\n", frame0);
            pass = false;
        }

        const uint32_t frame1s = ComputeFlipbookFrame(1.0f, kFlipbookFPS, kFlipbookTotalFrames);
        // floor(1.0 * 24) = 24; 24 % 16 = 8
        const uint32_t expected1s = 24u % kFlipbookTotalFrames;
        if (frame1s != expected1s) {
            std::fprintf(stderr, "[FAIL] Frame at age=1.0s expected %u, got %u\n",
                         expected1s, frame1s);
            pass = false;
        }
    }

    // Spawn count sanity: at 200/sec x 2sec = 400 expected spawns.
    const uint32_t expectedSpawns =
        static_cast<uint32_t>(kEmissionRate * kTotalFrames * kDt);
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
        std::printf("\n[PASS] Flipbook animation demo completed successfully.\n");
        return 0;
    } else {
        std::fprintf(stderr, "\n[FAIL] One or more validation checks failed.\n");
        return 1;
    }
}
