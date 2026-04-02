/// Fire Effect Demo — Headless VFX module system proof-of-concept.
///
/// Loads emitter configuration from assets/fire_effect.uzfx,
/// runs 120 frames of simulation, and validates the module stack
/// composition and spawn statistics.

#include "mock_device.h"
#include "engine/vfx/vfx_system.h"
#include "engine/vfx/components/particle_emitter.h"
#include "engine/vfx/core/particle_budget.h"
#include "engine/vfx/modules/emitter_module_stack.h"
#include "engine/vfx/modules/module_compositor.h"
#include "engine/vfx/modules/module_registry.h"
#include "engine/asset/formats/uzfx_loader.h"

#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <algorithm>
#include <memory>
#include <string>
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

    // ── Load .uzfx asset ──────────────────────────────────────────────
    auto json = uz::FileReadAllText("assets/fire_effect.uzfx");
    if (json.empty()) {
        std::fprintf(stderr, "[Error] Cannot read assets/fire_effect.uzfx\n");
        return 1;
    }
    auto desc = uz::UzfxLoader::Load(json);
    if (desc.name.empty()) {
        std::fprintf(stderr, "[Error] Failed to parse fire_effect.uzfx\n");
        return 1;
    }
    std::printf("[Asset] Loaded '%s' (version %u)\n", desc.name.c_str(), desc.version);

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

    // ── Compose HLSL via ModuleCompositor ──────────────────────────────
    uz::ModuleCompositor compositor;
    const auto updatePtrs = emitter.moduleStack.GetUpdateModulePtrs();
    const auto composed   = compositor.ComposeUpdateShader(updatePtrs);

    PrintComposedHlsl(composed.hlslSource);
    std::printf("[Compose] Cache key: 0x%016llX\n",
                static_cast<unsigned long long>(composed.cacheKey));

    // ── Register emitter and run simulation ────────────────────────────
    vfxSystem.RegisterEmitter(&emitter);

    constexpr float kDt          = 1.0f / 60.0f;
    constexpr int   kTotalFrames = 120;
    constexpr int   kReportInterval = 30;

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

    // Module stack must have correct counts (loaded from .uzfx).
    if (emitter.moduleStack.spawnModules.empty()) {
        std::fprintf(stderr, "[FAIL] Expected at least 1 spawn module\n");
        pass = false;
    }
    if (emitter.moduleStack.updateModules.empty()) {
        std::fprintf(stderr, "[FAIL] Expected at least 1 update module\n");
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

    // Spawn count sanity: emissionRate * totalTime with tolerance.
    const uint32_t expectedSpawns = static_cast<uint32_t>(desc.emissionRate * kTotalFrames * kDt);
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
        std::printf("[PASS] Fire effect demo completed successfully.\n");
        return 0;
    } else {
        std::fprintf(stderr, "\n[FAIL] One or more validation checks failed.\n");
        return 1;
    }
}
