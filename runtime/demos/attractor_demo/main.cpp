/// Attractor Demo — Headless VFX black hole attractor with kill-on-arrival.
///
/// Loads emitter configuration from assets/attractor_demo.uzfx,
/// runs 5 frames of simulation, and validates attractor module configuration,
/// parameter exposure, and shader code content.

#include "mock_device.h"
#include "engine/vfx/vfx_system.h"
#include "engine/vfx/components/particle_emitter.h"
#include "engine/vfx/core/particle_budget.h"
#include "engine/vfx/modules/emitter_module_stack.h"
#include "engine/vfx/modules/module_compositor.h"
#include "engine/vfx/modules/module_registry.h"
#include "engine/vfx/modules/attractor_module.h"
#include "engine/asset/formats/uzfx_loader.h"

#include <cstdint>
#include <cstdio>
#include <fstream>
#include <sstream>
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

    // ── Load .uzfx asset ──────────────────────────────────────────────
    auto json = ReadFile("assets/attractor_demo.uzfx");
    if (json.empty()) {
        std::fprintf(stderr, "[Error] Cannot read assets/attractor_demo.uzfx\n");
        return 1;
    }
    auto desc = uz::UzfxLoader::Load(json);
    if (desc.name.empty()) {
        std::fprintf(stderr, "[Error] Failed to parse attractor_demo.uzfx\n");
        return 1;
    }
    std::printf("[Asset] Loaded '%s' (version %u)\n\n", desc.name.c_str(), desc.version);

    // ── Setup ──────────────────────────────────────────────────────────
    uz::DemoMockDevice device;

    constexpr uint32_t kBudgetMax = 500000u;
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

    // ── Find the attractor module in the instantiated stack ────────────
    const uz::AttractorModule* attractorPtr = nullptr;
    for (const auto& m : emitter.moduleStack.updateModules) {
        if (std::string_view{m->GetName()} == "attractor") {
            attractorPtr = static_cast<const uz::AttractorModule*>(m.get());
            break;
        }
    }

    // ── Register emitter and run simulation ────────────────────────────
    vfxSystem.RegisterEmitter(&emitter);

    constexpr float kDt         = 0.016f;
    constexpr int   kTotalFrames = 5;

    std::printf("\n%-8s  %-12s  %-14s\n", "Frame", "SpawnCount", "BudgetUsed");
    std::printf("-------- ------------ --------------\n");

    for (int frame = 0; frame < kTotalFrames; ++frame) {
        const uint32_t spawnCount = emitter.CalculateSpawnCount(kDt);
        vfxSystem.Tick(kDt);
        std::printf("%-8d  %-12u  %-14u\n",
                    frame, spawnCount, vfxSystem.GetBudget().GetUsed());
    }

    // ── Attractor configuration report ────────────────────────────────
    if (attractorPtr) {
        std::printf("\nATTRACTOR CONFIGURATION\n");
        std::printf("--------------------------------------------------\n");
        std::printf("  Name:            %s\n", attractorPtr->GetName());
        std::printf("  Position:        {%.1f, %.1f, %.1f}\n",
                    static_cast<double>(attractorPtr->attractorPosition.x),
                    static_cast<double>(attractorPtr->attractorPosition.y),
                    static_cast<double>(attractorPtr->attractorPosition.z));
        std::printf("  Strength:        %.1f\n", static_cast<double>(attractorPtr->strength));
        std::printf("  Radius:          %.1f\n", static_cast<double>(attractorPtr->radius));
        std::printf("  Falloff:         %d\n", static_cast<int>(attractorPtr->falloff));
        std::printf("  killOnArrival:   %s\n", attractorPtr->killOnArrival ? "true" : "false");

        // Shader code snippet.
        const std::string_view shaderCode = attractorPtr->GetShaderCode();
        const std::size_t snippetLen = std::min(shaderCode.size(), std::size_t{200});
        std::printf("\nSHADER CODE SNIPPET (first %zu chars)\n", snippetLen);
        std::printf("--------------------------------------------------\n");
        std::printf("%.*s\n", static_cast<int>(snippetLen), shaderCode.data());
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
    if (emitter.moduleStack.renderModule) {
        std::printf("Render module:\n");
        PrintModuleParams(emitter.moduleStack.renderModule.get());
    }
    std::printf("--------------------------------------------------\n");

    // ── Validation ─────────────────────────────────────────────────────
    bool pass = true;

    // Attractor module must exist in the loaded stack.
    if (!attractorPtr) {
        std::fprintf(stderr, "[FAIL] AttractorModule not found in update stack\n");
        pass = false;
    } else {
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
        const std::string_view shaderCode = attractorPtr->GetShaderCode();
        if (shaderCode.find("AttractorsBuf") == std::string_view::npos) {
            std::fprintf(stderr, "[FAIL] GetShaderCode() does not contain 'AttractorsBuf'\n");
            pass = false;
        } else {
            std::printf("[CHECK] GetShaderCode() contains \"AttractorsBuf\" -- OK\n");
        }
    }

    // Budget must still show the emitter's allocation.
    if (vfxSystem.GetBudget().GetUsed() != desc.maxParticles) {
        std::fprintf(stderr, "[FAIL] Budget used=%u, expected %u\n",
                     vfxSystem.GetBudget().GetUsed(), desc.maxParticles);
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
    std::printf("\n[OK] %s: %u particles allocated, %d frames simulated\n",
                desc.name.c_str(), emitter.allocatedFromBudget, kTotalFrames);

    if (pass) {
        std::printf("[PASS] Attractor demo completed successfully.\n");
        return 0;
    } else {
        std::fprintf(stderr, "\n[FAIL] One or more validation checks failed.\n");
        return 1;
    }
}
