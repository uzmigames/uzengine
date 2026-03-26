/// Flipbook Animation Demo — Headless VFX flipbook (SubUV) sprite sheet proof-of-concept.
///
/// Loads emitter configuration from assets/flipbook_animation.uzfx,
/// runs 120 frames of simulation, and validates flipbook frame selection,
/// module stack configuration, and spawn statistics.

#include "mock_device.h"
#include "engine/vfx/vfx_system.h"
#include "engine/vfx/components/particle_emitter.h"
#include "engine/vfx/core/particle_budget.h"
#include "engine/vfx/modules/emitter_module_stack.h"
#include "engine/vfx/modules/module_compositor.h"
#include "engine/vfx/modules/module_registry.h"
#include "engine/vfx/modules/sprite_render_module.h"
#include "engine/asset/formats/uzfx_loader.h"

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

    // ── Load .uzfx asset ──────────────────────────────────────────────
    auto json = ReadFile("assets/flipbook_animation.uzfx");
    if (json.empty()) {
        std::fprintf(stderr, "[Error] Cannot read assets/flipbook_animation.uzfx\n");
        return 1;
    }
    auto desc = uz::UzfxLoader::Load(json);
    if (desc.name.empty()) {
        std::fprintf(stderr, "[Error] Failed to parse flipbook_animation.uzfx\n");
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

    // ── Verify flipbook state on the render module ─────────────────────
    const auto* spriteModPtr =
        emitter.moduleStack.renderModule
            ? static_cast<const uz::SpriteRenderModule*>(emitter.moduleStack.renderModule.get())
            : nullptr;

    if (spriteModPtr) {
        std::printf("[Flipbook] HasFlipbook(): %s\n",
                    spriteModPtr->HasFlipbook() ? "true" : "false");
        std::printf("[Flipbook] Rows=%u Cols=%u TotalFrames=%u FPS=%.1f\n",
                    spriteModPtr->flipbookRows,
                    spriteModPtr->flipbookCols,
                    spriteModPtr->flipbookTotalFrames,
                    static_cast<double>(spriteModPtr->flipbookFPS));
    }

    // ── Register emitter and run simulation ────────────────────────────
    vfxSystem.RegisterEmitter(&emitter);

    constexpr float kDt          = 1.0f / 60.0f;
    constexpr int   kTotalFrames = 120;
    constexpr int   kReportInterval = 30;

    std::printf("\n%-8s  %-12s  %-12s  %-14s  %-12s\n",
                "Frame", "Spawned", "Accumulator", "Budget Used", "FlipFrame");
    std::printf("-------- ------------ ------------ -------------- ------------\n");

    uint32_t totalSpawned      = 0;
    uint32_t peakSpawnPerFrame = 0;

    const auto startTime = std::chrono::high_resolution_clock::now();

    for (int frame = 0; frame < kTotalFrames; ++frame) {
        const uint32_t spawnCount = emitter.CalculateSpawnCount(kDt);
        totalSpawned += spawnCount;
        peakSpawnPerFrame = std::max(peakSpawnPerFrame, spawnCount);

        vfxSystem.Tick(kDt);

        if (frame == 0 || frame == kTotalFrames - 1 ||
            (frame + 1) % kReportInterval == 0) {
            // Compute flipbook frame for a particle at the median age (half lifetime).
            const float  medianAge   = desc.lifetime * 0.5f;
            const float  currentAge  = medianAge + frame * kDt;
            const uint32_t flipFrame = spriteModPtr
                ? ComputeFlipbookFrame(currentAge, spriteModPtr->flipbookFPS, spriteModPtr->flipbookTotalFrames)
                : 0u;

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
    if (spriteModPtr) {
        std::printf("\n  Flipbook configuration:\n");
        std::printf("    Rows x Cols:           %u x %u\n",
                    spriteModPtr->flipbookRows, spriteModPtr->flipbookCols);
        std::printf("    Total frames:          %u\n", spriteModPtr->flipbookTotalFrames);
        std::printf("    Playback FPS:          %.1f\n", static_cast<double>(spriteModPtr->flipbookFPS));
    }
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

    // Render module must exist and be sprite_render.
    if (!spriteModPtr) {
        std::fprintf(stderr, "[FAIL] Render module is null\n");
        pass = false;
    } else {
        // HasFlipbook() must return true if rows or cols > 1.
        if (spriteModPtr->flipbookRows > 1 || spriteModPtr->flipbookCols > 1) {
            if (!spriteModPtr->HasFlipbook()) {
                std::fprintf(stderr, "[FAIL] HasFlipbook() returned false for %ux%u sheet\n",
                             spriteModPtr->flipbookRows, spriteModPtr->flipbookCols);
                pass = false;
            }
        }

        if (std::string_view{emitter.moduleStack.renderModule->GetName()} != "sprite_render") {
            std::fprintf(stderr, "[FAIL] Render module name: expected 'sprite_render', got '%s'\n",
                         emitter.moduleStack.renderModule->GetName());
            pass = false;
        }

        // GetParameters() must expose flipbook parameters.
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

        // Flipbook frame index must wrap correctly.
        {
            const uint32_t totalFrames = spriteModPtr->flipbookTotalFrames;
            const float fps = spriteModPtr->flipbookFPS;

            const uint32_t frame0 = ComputeFlipbookFrame(0.0f, fps, totalFrames);
            if (frame0 != 0u) {
                std::fprintf(stderr, "[FAIL] Frame at age=0 expected 0, got %u\n", frame0);
                pass = false;
            }

            if (totalFrames > 0 && fps > 0.0f) {
                const uint32_t frame1s = ComputeFlipbookFrame(1.0f, fps, totalFrames);
                const uint32_t expected1s = static_cast<uint32_t>(fps) % totalFrames;
                if (frame1s != expected1s) {
                    std::fprintf(stderr, "[FAIL] Frame at age=1.0s expected %u, got %u\n",
                                 expected1s, frame1s);
                    pass = false;
                }
            }
        }
    }

    // Spawn count sanity.
    const uint32_t expectedSpawns =
        static_cast<uint32_t>(desc.emissionRate * kTotalFrames * kDt);
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
        std::printf("[PASS] Flipbook animation demo completed successfully.\n");
        return 0;
    } else {
        std::fprintf(stderr, "\n[FAIL] One or more validation checks failed.\n");
        return 1;
    }
}
