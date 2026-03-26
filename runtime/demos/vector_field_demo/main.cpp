/// Vector Field Demo -- Headless VFX wind tunnel with vector field velocity perturbation.
///
/// Demonstrates the VectorFieldModule feature by building a wind-tunnel particle
/// effect: particles are blown through a visible 3D flow field (intensity=5.0,
/// tightness=0.3). The field volume is 100x100x100 cm centred at {0,10,0} with
/// animated W-axis scrolling at timeScale=1.5 to simulate continuous wind flow.
///
/// This is a headless (no window, no GPU) demo that validates:
/// - VectorFieldModule configuration (intensity, tightness, bounds, fieldOffset, timeScale)
/// - GetName() returns "vector_field"
/// - GetParameters() exposes all 6 parameters
/// - GetShaderCode() contains the expected HLSL constant references
/// - Animated field: shader code contains "cbVectorFieldTimeScale" and "frac"
/// - VFXSystem + ParticleEmitter budget allocation and tick accumulator
///
/// # Build
///   cd demos/vector_field_demo
///   zig build
///   ./zig-out/bin/vector_field_demo
///
/// # Vector field parameters (wind tunnel)
/// - intensity:    5.0  (strong directional push)
/// - tightness:    0.3  (partial velocity blending)
/// - bounds:       {50, 50, 50} half-extents (100x100x100 cm box)
/// - fieldOffset:  {0, 10, 0}  (centred 10 cm above origin)
/// - timeScale:    1.5  (W-axis scroll for animated flow)

#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <algorithm>
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
#include "engine/vfx/modules/vector_field_module.h"

// ─────────────────────────────────────────────────────────────────────────────
// Demo parameters
// ─────────────────────────────────────────────────────────────────────────────

static constexpr uint32_t kMaxParticles = 5000u;
static constexpr float    kEmissionRate = 100.0f;   // particles/sec
static constexpr float    kLifetimeMin  = 1.0f;     // seconds
static constexpr float    kLifetimeMax  = 3.0f;     // seconds
static constexpr float    kDt           = 0.016f;   // ~60 FPS frame step
static constexpr int      kFrameCount   = 5;        // wind tunnel tick steps
static constexpr uint32_t kBudgetMax    = 500000u;

// Wind tunnel vector field parameters.
static constexpr float kFieldIntensity  = 5.0f;
static constexpr float kFieldTightness  = 0.3f;
static constexpr float kBoundsX        = 50.0f;
static constexpr float kBoundsY        = 50.0f;
static constexpr float kBoundsZ        = 50.0f;
static constexpr float kFieldOffsetX   = 0.0f;
static constexpr float kFieldOffsetY   = 10.0f;
static constexpr float kFieldOffsetZ   = 0.0f;
static constexpr float kFieldTimeScale = 1.5f;

// ─────────────────────────────────────────────────────────────────────────────
// Helpers
// ─────────────────────────────────────────────────────────────────────────────

namespace {

/// Returns true when any parameter in `params` matches `name`.
[[nodiscard]] bool HasParameter(const std::vector<uz::ParameterDef>& params,
                                const char* name) noexcept {
    for (const auto& p : params) {
        if (std::string_view{p.name} == name) {
            return true;
        }
    }
    return false;
}

/// Prints a formatted parameter list for one module.
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
            std::printf("    %-26s  (type=%d)\n",
                        p.name, static_cast<int>(p.type));
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
    std::printf("      UzEngine -- Vector Field Demo\n");
    std::printf("  Headless wind tunnel VFX (no GPU required)\n");
    std::printf("==========================================================\n\n");

    std::printf("[Config] Vector field: intensity=%.1f tightness=%.1f\n",
                static_cast<double>(kFieldIntensity),
                static_cast<double>(kFieldTightness));
    std::printf("[Config] Bounds: {%.0f, %.0f, %.0f} cm half-extents\n",
                static_cast<double>(kBoundsX),
                static_cast<double>(kBoundsY),
                static_cast<double>(kBoundsZ));
    std::printf("[Config] Field offset: {%.0f, %.0f, %.0f} cm\n",
                static_cast<double>(kFieldOffsetX),
                static_cast<double>(kFieldOffsetY),
                static_cast<double>(kFieldOffsetZ));
    std::printf("[Config] Time scale (animated wind): %.1f\n\n",
                static_cast<double>(kFieldTimeScale));

    // ── Setup ──────────────────────────────────────────────────────────
    DemoMockDevice device;

    uz::VFXSystem vfxSystem;
    vfxSystem.Initialize(&device, kBudgetMax);
    std::printf("[Init] VFXSystem initialized -- budget: %u particles\n", kBudgetMax);

    const auto& registry = vfxSystem.GetModuleRegistry();
    const auto  names    = registry.GetRegisteredNames();
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
    emitter.gravity       = {0.0f, 0.0f, 0.0f};  // wind field provides motion
    emitter.damping       = 0.99f;
    emitter.spawnVelocity = {1.0f, 0.0f, 0.0f};  // initial rightward drift
    emitter.spreadAngle   = 20.0f;
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

    // Spawn module: continuous stream of wind-tunnel particles.
    auto spawnMod = std::make_unique<uz::SpawnModule>();
    spawnMod->rate        = kEmissionRate;
    spawnMod->spreadAngle = 20.0f;
    spawnMod->velocity    = {1.0f, 0.0f, 0.0f};
    spawnMod->lifespanMin = kLifetimeMin;
    spawnMod->lifespanMax = kLifetimeMax;
    stack.AddSpawnModule(std::move(spawnMod));

    // Update module: minimal drag so the wind field dominates.
    auto updateMod = std::make_unique<uz::UpdateModule>();
    updateMod->gravity = {0.0f, 0.0f, 0.0f};
    updateMod->drag    = 0.99f;
    stack.AddUpdateModule(std::move(updateMod));

    // Decay must be last in update order.
    stack.AddUpdateModule(std::make_unique<uz::DecayModule>());

    // Vector field module: wind tunnel configuration.
    // @ref Stam 1999 "Stable Fluids" -- velocity field advection
    // @ref Bridson 2007 "Fluid Simulation for Computer Graphics" -- field sampling
    auto vfMod = std::make_unique<uz::VectorFieldModule>();
    vfMod->intensity   = kFieldIntensity;
    vfMod->tightness   = kFieldTightness;
    vfMod->bounds      = {kBoundsX, kBoundsY, kBoundsZ};
    vfMod->fieldOffset = {kFieldOffsetX, kFieldOffsetY, kFieldOffsetZ};
    vfMod->timeScale   = kFieldTimeScale;

    // Store a raw pointer for validation before moving ownership into the stack.
    const uz::VectorFieldModule* vfModPtr = vfMod.get();
    stack.AddUpdateModule(std::move(vfMod));

    std::printf("[Init] Module stack: %zu spawn, %zu update, render=%s\n",
                stack.spawnModules.size(),
                stack.updateModules.size(),
                stack.renderModule ? stack.renderModule->GetName() : "none");

    // ── Register emitter and run wind tunnel simulation ────────────────
    vfxSystem.RegisterEmitter(&emitter);

    std::printf("\n[Tick] Running %d frames at dt=%.3f s...\n\n",
                kFrameCount, static_cast<double>(kDt));

    std::printf("%-8s  %-12s  %-12s\n", "Frame", "SpawnCount", "BudgetUsed");
    std::printf("-------- ------------ ------------\n");

    for (int frame = 0; frame < kFrameCount; ++frame) {
        const uint32_t spawnCount = emitter.CalculateSpawnCount(kDt);
        vfxSystem.Tick(kDt);

        std::printf("%-8d  %-12u  %-12u\n",
                    frame,
                    spawnCount,
                    vfxSystem.GetBudget().GetUsed());
    }

    // ── Vector field configuration report ─────────────────────────────
    std::printf("\nVECTOR FIELD CONFIGURATION\n");
    std::printf("--------------------------------------------------\n");
    std::printf("  GetName():     %s\n",        vfModPtr->GetName());
    std::printf("  intensity:     %.1f\n",      static_cast<double>(vfModPtr->intensity));
    std::printf("  tightness:     %.1f\n",      static_cast<double>(vfModPtr->tightness));
    std::printf("  bounds:        {%.0f, %.0f, %.0f}\n",
                static_cast<double>(vfModPtr->bounds.x),
                static_cast<double>(vfModPtr->bounds.y),
                static_cast<double>(vfModPtr->bounds.z));
    std::printf("  fieldOffset:   {%.0f, %.0f, %.0f}\n",
                static_cast<double>(vfModPtr->fieldOffset.x),
                static_cast<double>(vfModPtr->fieldOffset.y),
                static_cast<double>(vfModPtr->fieldOffset.z));
    std::printf("  timeScale:     %.1f\n",      static_cast<double>(vfModPtr->timeScale));

    // ── Shader code snippet ────────────────────────────────────────────
    const std::string_view shaderCode = vfModPtr->GetShaderCode();
    const size_t snippetLen = std::min(shaderCode.size(), static_cast<size_t>(200));
    std::printf("\nSHADER CODE SNIPPET (first %zu chars):\n", snippetLen);
    std::printf("--------------------------------------------------\n");
    std::printf("%.*s\n",
                static_cast<int>(snippetLen),
                shaderCode.data());
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
    std::printf("--------------------------------------------------\n");
    std::printf("Parameter count: %zu\n", vfModPtr->GetParameters().size());
    std::printf("--------------------------------------------------\n");

    // ── Validation ─────────────────────────────────────────────────────
    bool pass = true;

    // GetName() must return "vector_field".
    if (std::string_view{vfModPtr->GetName()} != "vector_field") {
        std::fprintf(stderr, "[FAIL] GetName(): expected 'vector_field', got '%s'\n",
                     vfModPtr->GetName());
        pass = false;
    }

    // GetParameters() must expose exactly 6 parameters.
    const auto vfParams = vfModPtr->GetParameters();
    if (vfParams.size() != 6u) {
        std::fprintf(stderr, "[FAIL] GetParameters().size(): expected 6, got %zu\n",
                     vfParams.size());
        pass = false;
    }

    // All expected parameter names must be present.
    const char* kRequiredParams[] = {
        "field_texture",
        "intensity",
        "tightness",
        "bounds",
        "field_offset",
        "time_scale",
    };
    for (const char* pname : kRequiredParams) {
        if (!HasParameter(vfParams, pname)) {
            std::fprintf(stderr,
                         "[FAIL] VectorFieldModule missing parameter: '%s'\n",
                         pname);
            pass = false;
        }
    }

    // Shader must reference the main intensity constant.
    if (shaderCode.find("cbVectorFieldIntensity") == std::string_view::npos) {
        std::fprintf(stderr,
                     "[FAIL] Shader code does not contain 'cbVectorFieldIntensity'\n");
        pass = false;
    }

    // Animated field: shader must reference time scale and frac() for W-scroll.
    if (shaderCode.find("cbVectorFieldTimeScale") == std::string_view::npos) {
        std::fprintf(stderr,
                     "[FAIL] Shader code does not contain 'cbVectorFieldTimeScale'\n");
        pass = false;
    }
    if (shaderCode.find("frac") == std::string_view::npos) {
        std::fprintf(stderr,
                     "[FAIL] Shader code does not contain 'frac' (animated W-axis scroll)\n");
        pass = false;
    }

    // Configured field values must match what was set.
    if (vfModPtr->intensity != kFieldIntensity) {
        std::fprintf(stderr, "[FAIL] intensity: expected %.1f, got %.1f\n",
                     static_cast<double>(kFieldIntensity),
                     static_cast<double>(vfModPtr->intensity));
        pass = false;
    }
    if (vfModPtr->tightness != kFieldTightness) {
        std::fprintf(stderr, "[FAIL] tightness: expected %.1f, got %.1f\n",
                     static_cast<double>(kFieldTightness),
                     static_cast<double>(vfModPtr->tightness));
        pass = false;
    }
    if (vfModPtr->bounds.x != kBoundsX ||
        vfModPtr->bounds.y != kBoundsY ||
        vfModPtr->bounds.z != kBoundsZ) {
        std::fprintf(stderr,
                     "[FAIL] bounds: expected {%.0f, %.0f, %.0f}, got {%.0f, %.0f, %.0f}\n",
                     static_cast<double>(kBoundsX),
                     static_cast<double>(kBoundsY),
                     static_cast<double>(kBoundsZ),
                     static_cast<double>(vfModPtr->bounds.x),
                     static_cast<double>(vfModPtr->bounds.y),
                     static_cast<double>(vfModPtr->bounds.z));
        pass = false;
    }
    if (vfModPtr->fieldOffset.x != kFieldOffsetX ||
        vfModPtr->fieldOffset.y != kFieldOffsetY ||
        vfModPtr->fieldOffset.z != kFieldOffsetZ) {
        std::fprintf(stderr,
                     "[FAIL] fieldOffset: expected {%.0f, %.0f, %.0f}, got {%.0f, %.0f, %.0f}\n",
                     static_cast<double>(kFieldOffsetX),
                     static_cast<double>(kFieldOffsetY),
                     static_cast<double>(kFieldOffsetZ),
                     static_cast<double>(vfModPtr->fieldOffset.x),
                     static_cast<double>(vfModPtr->fieldOffset.y),
                     static_cast<double>(vfModPtr->fieldOffset.z));
        pass = false;
    }
    if (vfModPtr->timeScale != kFieldTimeScale) {
        std::fprintf(stderr, "[FAIL] timeScale: expected %.1f, got %.1f\n",
                     static_cast<double>(kFieldTimeScale),
                     static_cast<double>(vfModPtr->timeScale));
        pass = false;
    }

    // Budget must still reflect the emitter's allocation.
    if (vfxSystem.GetBudget().GetUsed() != kMaxParticles) {
        std::fprintf(stderr, "[FAIL] Budget used=%u, expected %u\n",
                     vfxSystem.GetBudget().GetUsed(), kMaxParticles);
        pass = false;
    }

    // ── Cleanup ────────────────────────────────────────────────────────
    vfxSystem.UnregisterEmitter(&emitter);
    emitter.Shutdown(&device, vfxSystem.GetBudget());

    if (vfxSystem.GetBudget().GetUsed() != 0u) {
        std::fprintf(stderr,
                     "[FAIL] Budget not fully returned after shutdown: %u\n",
                     vfxSystem.GetBudget().GetUsed());
        pass = false;
    }

    vfxSystem.Shutdown(&device);

    // ── Result ─────────────────────────────────────────────────────────
    if (pass) {
        std::printf("\n[PASS] Vector field demo completed successfully.\n");
        return 0;
    }

    std::fprintf(stderr, "\n[FAIL] One or more validation checks failed.\n");
    return 1;
}
