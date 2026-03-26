/// Vector Field Demo — Headless VFX wind tunnel with vector field velocity perturbation.
///
/// Loads emitter configuration from assets/vector_field_demo.uzfx,
/// runs 5 frames of simulation, and validates vector field module configuration,
/// parameter exposure, and shader code content.

#include "mock_device.h"
#include "engine/vfx/vfx_system.h"
#include "engine/vfx/components/particle_emitter.h"
#include "engine/vfx/core/particle_budget.h"
#include "engine/vfx/modules/emitter_module_stack.h"
#include "engine/vfx/modules/module_compositor.h"
#include "engine/vfx/modules/module_registry.h"
#include "engine/vfx/modules/vector_field_module.h"
#include "engine/vfx/asset/uzfx_loader.h"

#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <memory>
#include <string>
#include <string_view>

static std::string ReadFile(const char* path) {
    std::ifstream f(path);
    if (!f.is_open()) return {};
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

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

    // ── Load .uzfx asset ──────────────────────────────────────────────
    auto json = ReadFile("assets/vector_field_demo.uzfx");
    if (json.empty()) {
        std::fprintf(stderr, "[Error] Cannot read assets/vector_field_demo.uzfx\n");
        return 1;
    }
    auto desc = uz::UzfxLoader::Load(json);
    if (desc.name.empty()) {
        std::fprintf(stderr, "[Error] Failed to parse vector_field_demo.uzfx\n");
        return 1;
    }
    std::printf("[Asset] Loaded '%s' (version %u)\n\n", desc.name.c_str(), desc.version);

    // ── Setup ──────────────────────────────────────────────────────────
    uz::DemoMockDevice device;

    constexpr uint32_t kBudgetMax = 500000u;
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

    // ── Instantiate emitter from .uzfx ────────────────────────────────
    uz::ParticleEmitter emitter;
    if (!uz::UzfxLoader::Instantiate(desc, emitter, &device, vfxSystem.GetBudget())) {
        std::fprintf(stderr, "[Error] Failed to instantiate emitter\n");
        vfxSystem.Shutdown(&device);
        return 1;
    }

    std::printf("[Init] Emitter initialized -- capacity: %u, budget used: %u/%u\n",
                emitter.allocatedFromBudget,
                vfxSystem.GetBudget().GetUsed(),
                vfxSystem.GetBudget().GetMax());

    std::printf("[Init] Module stack: %zu spawn, %zu update, render=%s\n",
                emitter.moduleStack.spawnModules.size(),
                emitter.moduleStack.updateModules.size(),
                emitter.moduleStack.renderModule ? emitter.moduleStack.renderModule->GetName() : "none");

    // ── Find the vector field module in the instantiated stack ─────────
    const uz::VectorFieldModule* vfModPtr = nullptr;
    for (const auto& m : emitter.moduleStack.updateModules) {
        if (std::string_view{m->GetName()} == "vector_field") {
            vfModPtr = static_cast<const uz::VectorFieldModule*>(m.get());
            break;
        }
    }

    // ── Register emitter and run simulation ────────────────────────────
    vfxSystem.RegisterEmitter(&emitter);

    constexpr float kDt         = 0.016f;
    constexpr int   kFrameCount = 5;

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
    if (vfModPtr) {
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

        // Shader code snippet.
        const std::string_view shaderCode = vfModPtr->GetShaderCode();
        const size_t snippetLen = std::min(shaderCode.size(), static_cast<size_t>(200));
        std::printf("\nSHADER CODE SNIPPET (first %zu chars):\n", snippetLen);
        std::printf("--------------------------------------------------\n");
        std::printf("%.*s\n",
                    static_cast<int>(snippetLen),
                    shaderCode.data());
        std::printf("--------------------------------------------------\n");
    }

    // ── Parameter summary ──────────────────────────────────────────────
    std::printf("\nMODULE PARAMETER SUMMARY\n");
    std::printf("--------------------------------------------------\n");
    std::printf("Spawn modules:\n");
    for (const auto& m : emitter.moduleStack.spawnModules) {
        PrintModuleParams(m.get());
    }
    std::printf("Update modules:\n");
    for (const auto& m : emitter.moduleStack.updateModules) {
        PrintModuleParams(m.get());
    }
    std::printf("--------------------------------------------------\n");
    if (vfModPtr) {
        std::printf("Parameter count: %zu\n", vfModPtr->GetParameters().size());
    }
    std::printf("--------------------------------------------------\n");

    // ── Validation ─────────────────────────────────────────────────────
    bool pass = true;

    if (!vfModPtr) {
        std::fprintf(stderr, "[FAIL] VectorFieldModule not found in update stack\n");
        pass = false;
    } else {
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
        const std::string_view shaderCode = vfModPtr->GetShaderCode();
        if (shaderCode.find("cbVectorFieldIntensity") == std::string_view::npos) {
            std::fprintf(stderr,
                         "[FAIL] Shader code does not contain 'cbVectorFieldIntensity'\n");
            pass = false;
        }

        // Animated field: shader must reference time scale and frac().
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
    }

    // Budget must still reflect the emitter's allocation.
    if (vfxSystem.GetBudget().GetUsed() != desc.maxParticles) {
        std::fprintf(stderr, "[FAIL] Budget used=%u, expected %u\n",
                     vfxSystem.GetBudget().GetUsed(), desc.maxParticles);
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
    std::printf("\n[OK] %s: %u particles allocated, %d frames simulated\n",
                desc.name.c_str(), emitter.allocatedFromBudget, kFrameCount);

    if (pass) {
        std::printf("[PASS] Vector field demo completed successfully.\n");
        return 0;
    }

    std::fprintf(stderr, "\n[FAIL] One or more validation checks failed.\n");
    return 1;
}
