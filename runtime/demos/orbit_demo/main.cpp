/// Orbit Demo — Headless VFX ring-orbit particle effect proof-of-concept.
///
/// Loads emitter configuration from assets/orbit_demo.uzfx,
/// runs 5 frames of simulation, and validates orbit/pin/distance_constraint
/// module configuration, parameter exposure, and shader code content.

#include "mock_device.h"
#include "engine/vfx/vfx_system.h"
#include "engine/vfx/components/particle_emitter.h"
#include "engine/vfx/core/particle_budget.h"
#include "engine/vfx/modules/emitter_module_stack.h"
#include "engine/vfx/modules/module_compositor.h"
#include "engine/vfx/modules/module_registry.h"
#include "engine/vfx/modules/orbit_module.h"
#include "engine/vfx/modules/pin_module.h"
#include "engine/vfx/modules/distance_constraint_module.h"
#include "engine/asset/formats/uzfx_loader.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <string>
#include <string_view>
#include "file_system.h"

static std::string uz::FileReadAllText(const char* path) {
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

/// Prints the first `maxChars` characters of a shader snippet.
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

    // ── Load .uzfx asset ──────────────────────────────────────────────
    auto json = uz::FileReadAllText("assets/orbit_demo.uzfx");
    if (json.empty()) {
        std::fprintf(stderr, "[Error] Cannot read assets/orbit_demo.uzfx\n");
        return 1;
    }
    auto desc = uz::UzfxLoader::Load(json);
    if (desc.name.empty()) {
        std::fprintf(stderr, "[Error] Failed to parse orbit_demo.uzfx\n");
        return 1;
    }
    std::printf("[Asset] Loaded '%s' (version %u)\n\n", desc.name.c_str(), desc.version);

    // ── Setup ──────────────────────────────────────────────────────────────
    uz::DemoMockDevice device;

    constexpr uint32_t kBudgetMax = 500000u;
    uz::VFXSystem vfxSystem;
    vfxSystem.Initialize(&device, kBudgetMax);
    std::printf("[Init] VFXSystem initialized -- budget: %u particles\n", kBudgetMax);

    // ── Instantiate emitter from .uzfx ────────────────────────────────────
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

    std::printf("[Init] Module stack: %zu spawn, %zu update modules\n",
                emitter.moduleStack.spawnModules.size(),
                emitter.moduleStack.updateModules.size());

    // ── Retrieve module pointers for inspection ─────────────────────────────
    const uz::OrbitModule*             orbitPtr      = nullptr;
    const uz::PinModule*               pinPtr        = nullptr;
    const uz::DistanceConstraintModule* constraintPtr = nullptr;

    for (const auto& m : emitter.moduleStack.updateModules) {
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

    constexpr float kDt       = 0.016f;
    constexpr int   kSimFrames = 5;

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
    for (const auto& m : emitter.moduleStack.spawnModules) {
        PrintModuleParams(m.get());
    }
    std::printf("Update modules:\n");
    for (const auto& m : emitter.moduleStack.updateModules) {
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

    // OrbitModule validation.
    if (!orbitPtr) {
        std::fprintf(stderr, "[FAIL] OrbitModule not found in update stack\n");
        pass = false;
    } else {
        if (std::string_view{orbitPtr->GetName()} != "orbit") {
            std::fprintf(stderr, "[FAIL] OrbitModule::GetName() expected 'orbit', got '%s'\n",
                         orbitPtr->GetName());
            pass = false;
        }

        const auto orbitParams = orbitPtr->GetParameters();
        if (orbitParams.size() != 4u) {
            std::fprintf(stderr, "[FAIL] OrbitModule::GetParameters() size expected 4, got %zu\n",
                         orbitParams.size());
            pass = false;
        }

        const char* kOrbitRequired[] = {"center", "speed", "orbit_radius", "axis"};
        for (const char* pname : kOrbitRequired) {
            if (!HasParameter(orbitParams, pname)) {
                std::fprintf(stderr, "[FAIL] OrbitModule missing parameter: '%s'\n", pname);
                pass = false;
            }
        }

        const std::string_view orbitShader = orbitPtr->GetShaderCode();
        if (!ShaderContains(orbitShader, "cross")) {
            std::fprintf(stderr, "[FAIL] OrbitModule shader does not contain 'cross'\n");
            pass = false;
        } else {
            std::printf("[OK]   OrbitModule shader contains 'cross' (tangent computation)\n");
        }
    }

    // PinModule validation.
    if (!pinPtr) {
        std::fprintf(stderr, "[FAIL] PinModule not found in update stack\n");
        pass = false;
    } else {
        if (std::string_view{pinPtr->GetName()} != "pin") {
            std::fprintf(stderr, "[FAIL] PinModule::GetName() expected 'pin', got '%s'\n",
                         pinPtr->GetName());
            pass = false;
        }

        const auto pinParams = pinPtr->GetParameters();
        if (pinParams.size() != 2u) {
            std::fprintf(stderr, "[FAIL] PinModule::GetParameters() size expected 2, got %zu\n",
                         pinParams.size());
            pass = false;
        }

        const char* kPinRequired[] = {"pin_position", "spring_strength"};
        for (const char* pname : kPinRequired) {
            if (!HasParameter(pinParams, pname)) {
                std::fprintf(stderr, "[FAIL] PinModule missing parameter: '%s'\n", pname);
                pass = false;
            }
        }

        const std::string_view pinShader = pinPtr->GetShaderCode();
        if (!ShaderContains(pinShader, "force")) {
            std::fprintf(stderr, "[FAIL] PinModule shader does not contain 'force'\n");
            pass = false;
        } else {
            std::printf("[OK]   PinModule shader contains 'force' (spring force application)\n");
        }
    }

    // DistanceConstraintModule validation.
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

        const auto dcParams = constraintPtr->GetParameters();
        if (dcParams.size() != 4u) {
            std::fprintf(stderr, "[FAIL] DistanceConstraintModule::GetParameters() size expected 4, "
                         "got %zu\n", dcParams.size());
            pass = false;
        }

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

    // Budget must still reflect the emitter's allocation.
    if (vfxSystem.GetBudget().GetUsed() != desc.maxParticles) {
        std::fprintf(stderr, "[FAIL] Budget used=%u, expected %u\n",
                     vfxSystem.GetBudget().GetUsed(), desc.maxParticles);
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
    std::printf("\n[OK] %s: %u particles allocated, %d frames simulated\n",
                desc.name.c_str(), emitter.allocatedFromBudget, kSimFrames);

    if (pass) {
        std::printf("PASS\n");
        return 0;
    } else {
        std::fprintf(stderr, "\n[FAIL] One or more validation checks failed.\n");
        return 1;
    }
}
