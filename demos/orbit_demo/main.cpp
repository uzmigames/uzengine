/// Orbit Demo -- Headless VFX ring-orbit particle effect proof-of-concept.
///
/// Demonstrates particles orbiting a central point forming a ring, using:
/// - OrbitModule: constant angular velocity around a world-space center
/// - PinModule: Hooke's-law spring attraction toward an anchor above center
/// - DistanceConstraintModule: spherical-shell radial range enforcement
///
/// This is a headless (no window, no GPU) demo that validates:
/// - OrbitModule configuration (center, speed, orbitRadius, axis)
/// - PinModule configuration (pinPosition, springStrength)
/// - DistanceConstraintModule configuration (constraintOrigin, minDistance,
///   maxDistance, constraintStrength)
/// - Module GetName() and GetParameters() contracts
/// - Shader code content checks (cross tangent, spring/force keyword)
/// - VFXSystem + ParticleEmitter budget allocation and simulation ticking
///
/// # Build
///   cd demos/orbit_demo
///   zig build
///   ./zig-out/bin/orbit_demo
///
/// # Orbit configuration
/// - center:      {0, 0, 0}    world-space orbit center
/// - speed:       2.0          radians per second
/// - orbitRadius: 15.0         desired orbit radius (cm)
/// - axis:        {0, 0, 1}    Z-up ring plane
///
/// # Pin configuration
/// - pinPosition:    {0, 0, 5}   anchor above center
/// - springStrength: 5.0         Hooke's spring constant k
///
/// # Distance constraint configuration
/// - constraintOrigin:   {0, 0, 0}
/// - minDistance:        10.0
/// - maxDistance:        20.0
/// - constraintStrength: 0.8

#include <algorithm>
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
#include "engine/vfx/modules/orbit_module.h"
#include "engine/vfx/modules/pin_module.h"
#include "engine/vfx/modules/distance_constraint_module.h"

// ─────────────────────────────────────────────────────────────────────────────
// Demo parameters
// ─────────────────────────────────────────────────────────────────────────────

/// Budget ceiling for the global particle pool.
static constexpr uint32_t kBudgetMax    = 500000u;

/// Emitter capacity -- ring uses a modest particle count.
static constexpr uint32_t kMaxParticles = 2000u;

/// Emission rate in particles per second.
static constexpr float kEmissionRate = 100.0f;

/// Particle lifetime in seconds.
static constexpr float kLifetimeMax = 4.0f;

/// Fixed time-step in seconds (matches task specification: dt=0.016).
static constexpr float kDt = 0.016f;

/// Number of simulation ticks (task specification: 5 frames).
static constexpr int kSimFrames = 5;

/// OrbitModule parameters.
static constexpr float kOrbitSpeed  = 2.0f;
static constexpr float kOrbitRadius = 15.0f;

/// PinModule parameters.
static constexpr float kSpringStrength = 5.0f;

/// DistanceConstraintModule parameters.
static constexpr float kMinDistance        = 10.0f;
static constexpr float kMaxDistance        = 20.0f;
static constexpr float kConstraintStrength = 0.8f;

// ─────────────────────────────────────────────────────────────────────────────
// Helpers
// ─────────────────────────────────────────────────────────────────────────────

namespace {

/// Prints parameter definitions for a single module.
void PrintModuleParams(const uz::IVFXModule* mod) {
    const auto params = mod->GetParameters();
    std::printf("  [%s] (%zu params)\n", mod->GetName(), params.size());
    for (const auto& p : params) {
        switch (p.type) {
        case uz::ParameterType::Float:
            std::printf("    %-26s  Float   default=%.3f\n",
                        p.name,
                        static_cast<double>(p.defaultFloat));
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

/// Returns true if the shader code snippet contains the given search term.
[[nodiscard]] bool ShaderContains(std::string_view shader,
                                  std::string_view term) noexcept {
    return shader.find(term) != std::string_view::npos;
}

/// Prints the first `maxChars` characters of a shader snippet followed by
/// "..." if the snippet is longer.
void PrintShaderSnippet(const char* label, std::string_view shader,
                        std::size_t maxChars = 150) {
    std::printf("  %s shader snippet (first %zu chars):\n", label, maxChars);
    if (shader.size() <= maxChars) {
        std::printf("    %.*s\n", static_cast<int>(shader.size()), shader.data());
    } else {
        std::printf("    %.*s...\n", static_cast<int>(maxChars), shader.data());
    }
}

} // anonymous namespace

// ─────────────────────────────────────────────────────────────────────────────
// Main
// ─────────────────────────────────────────────────────────────────────────────

int main() {
    std::printf("==========================================================\n");
    std::printf("      UzEngine -- Orbit Demo\n");
    std::printf("  Headless VFX ring-orbit particle effect (no GPU required)\n");
    std::printf("==========================================================\n\n");

    std::printf("[Config] Orbit: center={0,0,0}  speed=%.1f rad/s  radius=%.1f cm  axis={0,0,1}\n",
                static_cast<double>(kOrbitSpeed),
                static_cast<double>(kOrbitRadius));
    std::printf("[Config] Pin: pinPosition={0,0,5}  springStrength=%.1f\n",
                static_cast<double>(kSpringStrength));
    std::printf("[Config] DistanceConstraint: origin={0,0,0}  min=%.1f  max=%.1f  strength=%.2f\n\n",
                static_cast<double>(kMinDistance),
                static_cast<double>(kMaxDistance),
                static_cast<double>(kConstraintStrength));

    // ── Setup ──────────────────────────────────────────────────────────────
    DemoMockDevice device;

    uz::VFXSystem vfxSystem;
    vfxSystem.Initialize(&device, kBudgetMax);
    std::printf("[Init] VFXSystem initialized -- budget: %u particles\n", kBudgetMax);

    // ── Emitter ────────────────────────────────────────────────────────────
    uz::ParticleEmitter emitter;
    emitter.maxParticles  = kMaxParticles;
    emitter.emissionRate  = kEmissionRate;
    emitter.lifetime      = kLifetimeMax;
    emitter.gravity       = {0.0f, 0.0f, 0.0f};  // gravity off -- orbit drives motion
    emitter.damping       = 0.99f;
    emitter.spawnVelocity = {0.0f, 0.0f, 0.0f};
    emitter.spreadAngle   = 360.0f;               // full-circle spawn for ring shape
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

    // ── Build module stack ─────────────────────────────────────────────────
    auto& stack = emitter.moduleStack;

    // Spawn module -- positions particles anywhere in the orbit plane.
    auto spawnMod = std::make_unique<uz::SpawnModule>();
    spawnMod->rate        = kEmissionRate;
    spawnMod->spreadAngle = 360.0f;
    spawnMod->velocity    = {0.0f, 0.0f, 0.0f};
    spawnMod->lifespanMin = kLifetimeMax * 0.5f;
    spawnMod->lifespanMax = kLifetimeMax;
    stack.AddSpawnModule(std::move(spawnMod));

    // Update module -- minimal drag to let orbit forces dominate.
    auto updateMod = std::make_unique<uz::UpdateModule>();
    updateMod->gravity = {0.0f, 0.0f, 0.0f};
    updateMod->drag    = 0.99f;
    stack.AddUpdateModule(std::move(updateMod));

    // Orbit module -- steers particles into a ring around the world origin.
    // @ref tangential cross-product drive: tangent = cross(axis, radialDir)
    auto orbitMod = std::make_unique<uz::OrbitModule>();
    orbitMod->center      = {0.0f, 0.0f, 0.0f};
    orbitMod->speed       = kOrbitSpeed;
    orbitMod->orbitRadius = kOrbitRadius;
    orbitMod->axis        = {0.0f, 0.0f, 1.0f};
    stack.AddUpdateModule(std::move(orbitMod));

    // Pin module -- attracts particles toward a point above the orbit center.
    // @ref Hooke 1678 "De Potentia Restitutiva" -- F = k * x
    auto pinMod = std::make_unique<uz::PinModule>();
    pinMod->pinPosition    = {0.0f, 0.0f, 5.0f};
    pinMod->springStrength = kSpringStrength;
    stack.AddUpdateModule(std::move(pinMod));

    // Distance constraint module -- enforces inner/outer shell radii.
    auto constraintMod = std::make_unique<uz::DistanceConstraintModule>();
    constraintMod->constraintOrigin   = {0.0f, 0.0f, 0.0f};
    constraintMod->minDistance        = kMinDistance;
    constraintMod->maxDistance        = kMaxDistance;
    constraintMod->constraintStrength = kConstraintStrength;
    stack.AddUpdateModule(std::move(constraintMod));

    // Decay module must be last in update order.
    stack.AddUpdateModule(std::make_unique<uz::DecayModule>());

    std::printf("[Init] Module stack: %zu spawn, %zu update modules\n",
                stack.spawnModules.size(),
                stack.updateModules.size());

    // ── Retrieve module pointers for inspection ─────────────────────────────
    // Update stack order: update(0), orbit(1), pin(2), distance_constraint(3), decay(4)
    const uz::OrbitModule*             orbitPtr      = nullptr;
    const uz::PinModule*               pinPtr        = nullptr;
    const uz::DistanceConstraintModule* constraintPtr = nullptr;

    for (const auto& m : stack.updateModules) {
        if (std::string_view{m->GetName()} == "orbit") {
            orbitPtr = static_cast<const uz::OrbitModule*>(m.get());
        } else if (std::string_view{m->GetName()} == "pin") {
            pinPtr = static_cast<const uz::PinModule*>(m.get());
        } else if (std::string_view{m->GetName()} == "distance_constraint") {
            constraintPtr = static_cast<const uz::DistanceConstraintModule*>(m.get());
        }
    }

    // ── Register emitter and run simulation ────────────────────────────────
    vfxSystem.RegisterEmitter(&emitter);

    std::printf("\nRunning %d simulation ticks (dt=%.3f s)\n", kSimFrames, static_cast<double>(kDt));
    std::printf("%-8s  %-12s  %-14s\n", "Tick", "Spawn/Tick", "Budget Used");
    std::printf("-------- ------------ --------------\n");

    for (int frame = 0; frame < kSimFrames; ++frame) {
        const uint32_t spawnCount = emitter.CalculateSpawnCount(kDt);
        vfxSystem.Tick(kDt);

        std::printf("%-8d  %-12u  %-14u\n",
                    frame,
                    spawnCount,
                    vfxSystem.GetBudget().GetUsed());
    }

    // ── Module parameter summary ────────────────────────────────────────────
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

    // ── Shader code snippets ────────────────────────────────────────────────
    std::printf("\nSHADER CODE SNIPPETS\n");
    std::printf("--------------------------------------------------\n");
    if (orbitPtr) {
        PrintShaderSnippet("orbit", orbitPtr->GetShaderCode());
    }
    if (pinPtr) {
        PrintShaderSnippet("pin", pinPtr->GetShaderCode());
    }
    if (constraintPtr) {
        PrintShaderSnippet("distance_constraint", constraintPtr->GetShaderCode());
    }
    std::printf("--------------------------------------------------\n");

    // ── Validation ─────────────────────────────────────────────────────────
    bool pass = true;

    // OrbitModule: name must be "orbit".
    if (!orbitPtr) {
        std::fprintf(stderr, "[FAIL] OrbitModule not found in update stack\n");
        pass = false;
    } else {
        if (std::string_view{orbitPtr->GetName()} != "orbit") {
            std::fprintf(stderr, "[FAIL] OrbitModule::GetName() expected 'orbit', got '%s'\n",
                         orbitPtr->GetName());
            pass = false;
        }

        // OrbitModule: must expose exactly 4 parameters.
        const auto orbitParams = orbitPtr->GetParameters();
        if (orbitParams.size() != 4u) {
            std::fprintf(stderr, "[FAIL] OrbitModule::GetParameters() size expected 4, got %zu\n",
                         orbitParams.size());
            pass = false;
        }

        // OrbitModule: required parameters.
        const char* kOrbitRequired[] = {"center", "speed", "orbit_radius", "axis"};
        for (const char* pname : kOrbitRequired) {
            if (!HasParameter(orbitParams, pname)) {
                std::fprintf(stderr, "[FAIL] OrbitModule missing parameter: '%s'\n", pname);
                pass = false;
            }
        }

        // OrbitModule: shader must contain "cross" (tangent = cross(axis, radialDir)).
        const std::string_view orbitShader = orbitPtr->GetShaderCode();
        if (!ShaderContains(orbitShader, "cross")) {
            std::fprintf(stderr, "[FAIL] OrbitModule shader does not contain 'cross'\n");
            pass = false;
        } else {
            std::printf("[OK]   OrbitModule shader contains 'cross' (tangent computation)\n");
        }
    }

    // PinModule: name must be "pin".
    if (!pinPtr) {
        std::fprintf(stderr, "[FAIL] PinModule not found in update stack\n");
        pass = false;
    } else {
        if (std::string_view{pinPtr->GetName()} != "pin") {
            std::fprintf(stderr, "[FAIL] PinModule::GetName() expected 'pin', got '%s'\n",
                         pinPtr->GetName());
            pass = false;
        }

        // PinModule: must expose exactly 2 parameters.
        const auto pinParams = pinPtr->GetParameters();
        if (pinParams.size() != 2u) {
            std::fprintf(stderr, "[FAIL] PinModule::GetParameters() size expected 2, got %zu\n",
                         pinParams.size());
            pass = false;
        }

        // PinModule: required parameters.
        const char* kPinRequired[] = {"pin_position", "spring_strength"};
        for (const char* pname : kPinRequired) {
            if (!HasParameter(pinParams, pname)) {
                std::fprintf(stderr, "[FAIL] PinModule missing parameter: '%s'\n", pname);
                pass = false;
            }
        }

        // PinModule: shader must contain "force" (spring restoring force application).
        const std::string_view pinShader = pinPtr->GetShaderCode();
        if (!ShaderContains(pinShader, "force")) {
            std::fprintf(stderr, "[FAIL] PinModule shader does not contain 'force'\n");
            pass = false;
        } else {
            std::printf("[OK]   PinModule shader contains 'force' (spring force application)\n");
        }
    }

    // DistanceConstraintModule: name must be "distance_constraint".
    if (!constraintPtr) {
        std::fprintf(stderr, "[FAIL] DistanceConstraintModule not found in update stack\n");
        pass = false;
    } else {
        if (std::string_view{constraintPtr->GetName()} != "distance_constraint") {
            std::fprintf(stderr, "[FAIL] DistanceConstraintModule::GetName() expected "
                         "'distance_constraint', got '%s'\n",
                         constraintPtr->GetName());
            pass = false;
        }

        // DistanceConstraintModule: must expose exactly 4 parameters.
        const auto dcParams = constraintPtr->GetParameters();
        if (dcParams.size() != 4u) {
            std::fprintf(stderr, "[FAIL] DistanceConstraintModule::GetParameters() size expected 4, "
                         "got %zu\n", dcParams.size());
            pass = false;
        }

        // DistanceConstraintModule: required parameters.
        const char* kDcRequired[] = {
            "constraint_origin",
            "min_distance",
            "max_distance",
            "constraint_strength",
        };
        for (const char* pname : kDcRequired) {
            if (!HasParameter(dcParams, pname)) {
                std::fprintf(stderr, "[FAIL] DistanceConstraintModule missing parameter: '%s'\n",
                             pname);
                pass = false;
            }
        }
    }

    // Module stack structure sanity.
    if (stack.spawnModules.size() != 1u) {
        std::fprintf(stderr, "[FAIL] Expected 1 spawn module, got %zu\n",
                     stack.spawnModules.size());
        pass = false;
    }
    // Update stack: UpdateModule, OrbitModule, PinModule, DistanceConstraintModule, DecayModule = 5.
    if (stack.updateModules.size() != 5u) {
        std::fprintf(stderr, "[FAIL] Expected 5 update modules, got %zu\n",
                     stack.updateModules.size());
        pass = false;
    }

    // Budget must still reflect the emitter's allocation.
    if (vfxSystem.GetBudget().GetUsed() != kMaxParticles) {
        std::fprintf(stderr, "[FAIL] Budget used=%u, expected %u\n",
                     vfxSystem.GetBudget().GetUsed(), kMaxParticles);
        pass = false;
    }

    // ── Cleanup ────────────────────────────────────────────────────────────
    vfxSystem.UnregisterEmitter(&emitter);
    emitter.Shutdown(&device, vfxSystem.GetBudget());

    if (vfxSystem.GetBudget().GetUsed() != 0u) {
        std::fprintf(stderr, "[FAIL] Budget not fully returned after shutdown: %u\n",
                     vfxSystem.GetBudget().GetUsed());
        pass = false;
    }

    vfxSystem.Shutdown(&device);

    // ── Result ─────────────────────────────────────────────────────────────
    if (pass) {
        std::printf("\nPASS\n");
        return 0;
    } else {
        std::fprintf(stderr, "\n[FAIL] One or more validation checks failed.\n");
        return 1;
    }
}
