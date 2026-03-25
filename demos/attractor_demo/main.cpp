/// Attractor Demo -- Headless VFX black hole attractor with kill-on-arrival.
///
/// Demonstrates the AttractorModule by pulling particles into a black hole at
/// the world origin. Uses InverseSquare falloff and killOnArrival=true so
/// particles are expired the moment they arrive at the singularity.
///
/// This is a headless (no window, no GPU) demo that validates:
/// - AttractorModule configuration (position, strength, radius, falloff, killOnArrival)
/// - GetName() returns "attractor"
/// - GetParameters() exposes all 5 defined parameters
/// - GetShaderCode() returns a snippet containing "AttractorsBuf"
/// - VFXSystem + ParticleEmitter budget allocation and tick loop
///
/// # Build
///   cd demos/attractor_demo
///   zig build
///   ./zig-out/bin/attractor_demo
///
/// # Attractor parameters (black hole at origin)
/// - position:       {0, 0, 0}
/// - strength:       50.0
/// - radius:         200.0
/// - falloff:        InverseSquare
/// - killOnArrival:  true

#include <cstdint>
#include <cstdio>
#include <memory>
#include <string>
#include <string_view>

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
#include "engine/vfx/modules/attractor_module.h"

// ─────────────────────────────────────────────────────────────────────────────
// Demo parameters
// ─────────────────────────────────────────────────────────────────────────────

static constexpr uint32_t kMaxParticles  = 5000u;
static constexpr float    kEmissionRate  = 100.0f;   // particles/sec
static constexpr float    kLifetimeMax   = 4.0f;     // seconds
static constexpr float    kDt            = 0.016f;   // ~60 FPS step
static constexpr int      kTotalFrames   = 5;
static constexpr uint32_t kBudgetMax     = 500000u;

// Attractor configuration: black hole at origin.
static constexpr float kAttractorStrength = 50.0f;
static constexpr float kAttractorRadius   = 200.0f;

// ─────────────────────────────────────────────────────────────────────────────
// Helpers
// ─────────────────────────────────────────────────────────────────────────────

namespace {

/// Returns true if a parameter with the given name exists in the list.
[[nodiscard]] bool HasParameter(const std::vector<uz::ParameterDef>& params,
                                const char* name) noexcept {
    for (const auto& p : params) {
        if (std::string_view{p.name} == name) {
            return true;
        }
    }
    return false;
}

/// Prints all parameter definitions for a module.
void PrintModuleParams(const uz::IVFXModule* mod) {
    const auto params = mod->GetParameters();
    std::printf("  [%s] (%zu params)\n", mod->GetName(), params.size());
    for (const auto& p : params) {
        switch (p.type) {
        case uz::ParameterType::Float:
            std::printf("    %-26s  Float   default=%.3f\n",
                        p.name, static_cast<double>(p.defaultFloat));
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
            std::printf("    %-26s  Bool    default=%s\n",
                        p.name, p.defaultBool ? "true" : "false");
            break;
        default:
            std::printf("    %-26s  (type=%d)\n", p.name, static_cast<int>(p.type));
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
    std::printf("      UzEngine -- Attractor Demo\n");
    std::printf("  Headless VFX black hole (no GPU required)\n");
    std::printf("==========================================================\n\n");

    std::printf("[Config] Attractor position: {0, 0, 0}\n");
    std::printf("[Config] Strength: %.1f  Radius: %.1f  Falloff: InverseSquare\n",
                static_cast<double>(kAttractorStrength),
                static_cast<double>(kAttractorRadius));
    std::printf("[Config] killOnArrival: true\n\n");

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
    emitter.gravity       = {0.0f, 0.0f, 0.0f};
    emitter.damping       = 1.0f;
    emitter.spawnVelocity = {10.0f, 0.0f, 0.0f};
    emitter.spreadAngle   = 180.0f;
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

    // Spawn module: isotropic scatter around origin, pulled by attractor.
    auto spawnMod = std::make_unique<uz::SpawnModule>();
    spawnMod->rate        = kEmissionRate;
    spawnMod->spreadAngle = 180.0f;
    spawnMod->velocity    = {10.0f, 0.0f, 0.0f};
    spawnMod->lifespanMin = 1.0f;
    spawnMod->lifespanMax = kLifetimeMax;
    stack.AddSpawnModule(std::move(spawnMod));

    // Update module: no gravity (attractor provides the only force).
    auto updateMod = std::make_unique<uz::UpdateModule>();
    updateMod->gravity = {0.0f, 0.0f, 0.0f};
    updateMod->drag    = 1.0f;
    stack.AddUpdateModule(std::move(updateMod));

    // Decay must be last in update order.
    stack.AddUpdateModule(std::make_unique<uz::DecayModule>());

    // Attractor module: black hole at origin with InverseSquare falloff.
    // @ref Newton 1687 "Philosophiae Naturalis Principia Mathematica" --
    //     gravitational force F = G*m / r^2 (InverseSquare falloff)
    auto attractorMod = std::make_unique<uz::AttractorModule>();
    attractorMod->attractorPosition = {0.0f, 0.0f, 0.0f};
    attractorMod->strength          = kAttractorStrength;
    attractorMod->radius            = kAttractorRadius;
    attractorMod->falloff           = uz::AttractorFalloff::InverseSquare;
    attractorMod->killOnArrival     = true;

    // Keep a raw pointer for validation before moving into the stack.
    const uz::AttractorModule* attractorPtr = attractorMod.get();
    stack.AddUpdateModule(std::move(attractorMod));

    std::printf("[Init] Module stack: %zu spawn, %zu update, render=%s\n",
                stack.spawnModules.size(),
                stack.updateModules.size(),
                stack.renderModule ? stack.renderModule->GetName() : "none");

    // ── Register emitter and run simulation ────────────────────────────
    vfxSystem.RegisterEmitter(&emitter);

    std::printf("\n%-8s  %-12s  %-14s\n", "Frame", "SpawnCount", "BudgetUsed");
    std::printf("-------- ------------ --------------\n");

    for (int frame = 0; frame < kTotalFrames; ++frame) {
        const uint32_t spawnCount = emitter.CalculateSpawnCount(kDt);
        vfxSystem.Tick(kDt);
        std::printf("%-8d  %-12u  %-14u\n",
                    frame, spawnCount, vfxSystem.GetBudget().GetUsed());
    }

    // ── Attractor configuration report ────────────────────────────────
    std::printf("\nATTRACTOR CONFIGURATION\n");
    std::printf("--------------------------------------------------\n");
    std::printf("  Name:            %s\n", attractorPtr->GetName());
    std::printf("  Position:        {%.1f, %.1f, %.1f}\n",
                static_cast<double>(attractorPtr->attractorPosition.x),
                static_cast<double>(attractorPtr->attractorPosition.y),
                static_cast<double>(attractorPtr->attractorPosition.z));
    std::printf("  Strength:        %.1f\n", static_cast<double>(attractorPtr->strength));
    std::printf("  Radius:          %.1f\n", static_cast<double>(attractorPtr->radius));
    std::printf("  Falloff:         InverseSquare (%d)\n",
                static_cast<int>(attractorPtr->falloff));
    std::printf("  killOnArrival:   %s\n", attractorPtr->killOnArrival ? "true" : "false");

    // ── Shader code snippet (first 200 chars) ─────────────────────────
    const std::string_view shaderCode = attractorPtr->GetShaderCode();
    const std::size_t      snippetLen = std::min(shaderCode.size(), std::size_t{200});
    std::printf("\nSHADER CODE SNIPPET (first %zu chars)\n", snippetLen);
    std::printf("--------------------------------------------------\n");
    std::printf("%.*s\n", static_cast<int>(snippetLen), shaderCode.data());

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

    // GetName() must return "attractor".
    if (std::string_view{attractorPtr->GetName()} != "attractor") {
        std::fprintf(stderr, "[FAIL] GetName(): expected 'attractor', got '%s'\n",
                     attractorPtr->GetName());
        pass = false;
    } else {
        std::printf("\n[CHECK] GetName() == \"attractor\" -- OK\n");
    }

    // GetParameters() must expose exactly 5 parameters.
    const auto params = attractorPtr->GetParameters();
    if (params.size() != 5u) {
        std::fprintf(stderr, "[FAIL] GetParameters().size(): expected 5, got %zu\n",
                     params.size());
        pass = false;
    } else {
        std::printf("[CHECK] GetParameters().size() == 5 -- OK\n");
    }

    // All required parameter names must be present.
    const char* kRequiredParams[] = {
        "attractor_position",
        "strength",
        "radius",
        "falloff",
        "kill_on_arrival",
    };
    for (const char* pname : kRequiredParams) {
        if (!HasParameter(params, pname)) {
            std::fprintf(stderr, "[FAIL] AttractorModule missing parameter: '%s'\n", pname);
            pass = false;
        }
    }

    // GetShaderCode() must contain "AttractorsBuf".
    if (shaderCode.find("AttractorsBuf") == std::string_view::npos) {
        std::fprintf(stderr, "[FAIL] GetShaderCode() does not contain 'AttractorsBuf'\n");
        pass = false;
    } else {
        std::printf("[CHECK] GetShaderCode() contains \"AttractorsBuf\" -- OK\n");
    }

    // Configuration fields must match what was set.
    if (attractorPtr->strength != kAttractorStrength) {
        std::fprintf(stderr, "[FAIL] strength: expected %.1f, got %.1f\n",
                     static_cast<double>(kAttractorStrength),
                     static_cast<double>(attractorPtr->strength));
        pass = false;
    }
    if (attractorPtr->radius != kAttractorRadius) {
        std::fprintf(stderr, "[FAIL] radius: expected %.1f, got %.1f\n",
                     static_cast<double>(kAttractorRadius),
                     static_cast<double>(attractorPtr->radius));
        pass = false;
    }
    if (attractorPtr->falloff != uz::AttractorFalloff::InverseSquare) {
        std::fprintf(stderr, "[FAIL] falloff: expected InverseSquare\n");
        pass = false;
    }
    if (!attractorPtr->killOnArrival) {
        std::fprintf(stderr, "[FAIL] killOnArrival: expected true\n");
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
        std::printf("\n[PASS] Attractor demo completed successfully.\n");
        return 0;
    } else {
        std::fprintf(stderr, "\n[FAIL] One or more validation checks failed.\n");
        return 1;
    }
}
