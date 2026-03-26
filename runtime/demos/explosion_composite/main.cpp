/// Explosion Composite Demo — Headless multi-emitter VFX simulation.
///
/// Loads three emitter configurations from .uzfx assets (fire, smoke, debris),
/// runs 180 frames of simulation with a burst event at frame 30, and validates
/// multi-emitter spawn statistics and budget management.

#include "mock_device.h"
#include "engine/vfx/vfx_system.h"
#include "engine/vfx/components/particle_emitter.h"
#include "engine/vfx/core/particle_budget.h"
#include "engine/vfx/events/vfx_event.h"
#include "engine/vfx/modules/emitter_module_stack.h"
#include "engine/vfx/modules/module_registry.h"
#include "engine/vfx/asset/uzfx_loader.h"

#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <string>
#include <vector>

static std::string ReadFile(const char* path) {
    std::ifstream f(path);
    if (!f.is_open()) return {};
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

// ─────────────────────────────────────────────────────────────────────────────
// Demo constants
// ─────────────────────────────────────────────────────────────────────────────

/// Global budget for all emitters combined.
static constexpr uint32_t kBudgetMax = 500000u;

/// Simulation timestep (60 FPS).
static constexpr float kDt = 1.0f / 60.0f;

/// Total simulation duration in frames (3 seconds at 60 FPS).
static constexpr int kTotalFrames = 180;

/// Print stats every N frames.
static constexpr int kReportInterval = 30;

/// Frame on which to fire the "explode" burst event.
static constexpr int kBurstFrame = 30;

/// Number of particles in the burst event.
static constexpr uint32_t kBurstCount = 500;

/// Velocity multiplier for burst particles.
static constexpr float kBurstVelocityScale = 3.0f;

// ─────────────────────────────────────────────────────────────────────────────
// Helpers
// ─────────────────────────────────────────────────────────────────────────────

namespace {

/// Prints a separator line to stdout.
void PrintSeparator() {
    std::printf("──────────────────────────────────────────────────────────────\n");
}

/// Prints per-emitter stats for a single frame.
void PrintFrameStats(int frame,
                     const std::vector<uz::ParticleEmitter>& emitters,
                     const std::vector<uz::UzfxDesc>& descs,
                     const uz::ParticleBudget& budget) {
    std::printf("[Frame %3d | t=%.2fs]  budget=%u/%u\n",
                frame,
                static_cast<double>(frame * kDt),
                budget.GetUsed(),
                budget.GetMax());

    for (size_t i = 0; i < emitters.size(); ++i) {
        const auto& e = emitters[i];
        const char* state = "emitting";
        if (!e.initialized) {
            state = "not-init";
        } else if (!e.isEmitting) {
            state = "stopped";
        }

        std::printf("  %-8s: accum=%.4f  allocated=%5u  events=%zu  [%s]\n",
                    descs[i].name.c_str(),
                    static_cast<double>(e.spawnAccumulator),
                    e.allocatedFromBudget,
                    e.eventQueue.GetPendingCount(),
                    state);
    }
}

} // anonymous namespace

// ─────────────────────────────────────────────────────────────────────────────
// Main
// ─────────────────────────────────────────────────────────────────────────────

int main() {
    std::printf("==========================================================\n");
    std::printf("       UzEngine -- Explosion Composite Demo\n");
    std::printf("   Headless multi-emitter VFX system (no GPU required)\n");
    std::printf("==========================================================\n\n");

    // ── Load .uzfx assets ────────────────────────────────────────────────
    struct AssetEntry {
        const char* path;
    };
    const AssetEntry kAssets[] = {
        {"assets/explosion_fire.uzfx"},
        {"assets/explosion_smoke.uzfx"},
        {"assets/explosion_debris.uzfx"},
    };

    std::vector<uz::UzfxDesc> descs;
    for (const auto& asset : kAssets) {
        auto json = ReadFile(asset.path);
        if (json.empty()) {
            std::fprintf(stderr, "[Error] Cannot read %s\n", asset.path);
            return 1;
        }
        auto desc = uz::UzfxLoader::Load(json);
        if (desc.name.empty()) {
            std::fprintf(stderr, "[Error] Failed to parse %s\n", asset.path);
            return 1;
        }
        std::printf("[Asset] Loaded '%s' (version %u) from %s\n",
                    desc.name.c_str(), desc.version, asset.path);
        descs.push_back(std::move(desc));
    }
    std::printf("\n");

    // ── Create VFXSystem ────────────────────────────────────────────────
    uz::DemoMockDevice device;

    uz::VFXSystem vfxSystem;
    vfxSystem.Initialize(&device, kBudgetMax);
    std::printf("[Init] VFXSystem initialized -- budget: %u particles\n", kBudgetMax);

    const auto& registry = vfxSystem.GetModuleRegistry();
    const auto moduleNames = registry.GetRegisteredNames();
    std::printf("[Init] ModuleRegistry has %zu built-in modules:",
                moduleNames.size());
    for (const auto& n : moduleNames) {
        std::printf(" %s", n.c_str());
    }
    std::printf("\n\n");

    // ── Instantiate emitters from .uzfx descs ────────────────────────────
    std::vector<uz::ParticleEmitter> emitters(descs.size());

    for (size_t i = 0; i < descs.size(); ++i) {
        if (!uz::UzfxLoader::Instantiate(descs[i], emitters[i], &device, vfxSystem.GetBudget())) {
            std::fprintf(stderr, "[Error] Failed to instantiate emitter '%s'\n",
                         descs[i].name.c_str());
            vfxSystem.Shutdown(&device);
            return 1;
        }

        std::printf("[Init] Emitter '%s' initialized -- capacity=%u, budget=%u/%u\n",
                    descs[i].name.c_str(),
                    emitters[i].allocatedFromBudget,
                    vfxSystem.GetBudget().GetUsed(),
                    vfxSystem.GetBudget().GetMax());
    }

    // ── Register emitters with VFXSystem ──────────────────────────────
    for (auto& emitter : emitters) {
        vfxSystem.RegisterEmitter(&emitter);
    }

    std::printf("\n[Sim] Starting simulation: %d frames (%.1f seconds at %.0f FPS)\n",
                kTotalFrames,
                static_cast<double>(kTotalFrames * kDt),
                static_cast<double>(1.0f / kDt));
    std::printf("[Sim] Burst event 'explode' scheduled at frame %d (%.2fs)\n\n",
                kBurstFrame,
                static_cast<double>(kBurstFrame * kDt));
    PrintSeparator();

    // ── Simulation loop ─────────────────────────────────────────────────
    uint32_t totalSpawned = 0;
    const auto startTime = std::chrono::high_resolution_clock::now();

    for (int frame = 0; frame < kTotalFrames; ++frame) {
        // Fire "explode" burst at the designated frame.
        if (frame == kBurstFrame) {
            std::printf("\n  >>> BURST EVENT: 'explode' -- %u particles, %.1fx velocity\n\n",
                        kBurstCount,
                        static_cast<double>(kBurstVelocityScale));

            for (auto& emitter : emitters) {
                emitter.eventQueue.Emit("explode", kBurstCount, kBurstVelocityScale);
            }
        }

        // Calculate spawn counts before ticking (for stats).
        uint32_t frameSpawned = 0;
        for (auto& emitter : emitters) {
            frameSpawned += emitter.CalculateSpawnCount(kDt);
        }
        totalSpawned += frameSpawned;

        // Advance the simulation.
        vfxSystem.Tick(kDt);

        // Log frame stats at the reporting interval.
        const bool isReportFrame =
            frame == 0 ||
            frame == kBurstFrame ||
            frame == kBurstFrame + 1 ||
            frame == kTotalFrames - 1 ||
            (frame + 1) % kReportInterval == 0;

        if (isReportFrame) {
            PrintFrameStats(frame, emitters, descs, vfxSystem.GetBudget());
        }
    }

    const auto endTime = std::chrono::high_resolution_clock::now();
    const double elapsedMs =
        std::chrono::duration<double, std::milli>(endTime - startTime).count();

    // ── Summary ───────────────────────────────────────────────────────
    PrintSeparator();
    std::printf("\nSIMULATION SUMMARY\n");
    PrintSeparator();
    std::printf("  Frames simulated:        %d (%.1f seconds)\n",
                kTotalFrames, static_cast<double>(kTotalFrames * kDt));
    std::printf("  Total particles spawned:  %u\n", totalSpawned);
    std::printf("  Active emitters:          %u\n", vfxSystem.GetActiveEmitterCount());
    std::printf("  Budget used:              %u / %u\n",
                vfxSystem.GetBudget().GetUsed(),
                vfxSystem.GetBudget().GetMax());
    std::printf("  CPU wall time:            %.2f ms\n", elapsedMs);

    std::printf("\nPer-emitter breakdown:\n");
    for (size_t i = 0; i < descs.size(); ++i) {
        const auto& e = emitters[i];
        std::printf("  %-8s: allocated=%5u  accum=%.4f  modules=%zu+%zu\n",
                    descs[i].name.c_str(),
                    e.allocatedFromBudget,
                    static_cast<double>(e.spawnAccumulator),
                    e.moduleStack.spawnModules.size(),
                    e.moduleStack.updateModules.size());
    }
    PrintSeparator();

    // ── Validation ────────────────────────────────────────────────────
    bool pass = true;

    // Must have loaded 3 emitters.
    if (descs.size() != 3) {
        std::fprintf(stderr, "[FAIL] Expected 3 .uzfx assets, loaded %zu\n", descs.size());
        pass = false;
    }

    // All emitters must be initialized.
    for (size_t i = 0; i < emitters.size(); ++i) {
        if (!emitters[i].initialized) {
            std::fprintf(stderr, "[FAIL] Emitter '%s' not initialized\n",
                         descs[i].name.c_str());
            pass = false;
        }
    }

    // Total spawned must be reasonable.
    if (totalSpawned < 100) {
        std::fprintf(stderr, "[FAIL] Expected meaningful spawn count, got %u\n",
                     totalSpawned);
        pass = false;
    }

    // Active emitter count must match.
    if (vfxSystem.GetActiveEmitterCount() != 3) {
        std::fprintf(stderr, "[FAIL] Expected 3 active emitters, got %u\n",
                     vfxSystem.GetActiveEmitterCount());
        pass = false;
    }

    // ── Cleanup ───────────────────────────────────────────────────────
    for (auto& emitter : emitters) {
        vfxSystem.UnregisterEmitter(&emitter);
    }

    for (auto& emitter : emitters) {
        emitter.Shutdown(&device, vfxSystem.GetBudget());
    }

    // Budget must be fully returned.
    if (vfxSystem.GetBudget().GetUsed() != 0) {
        std::fprintf(stderr, "[FAIL] Budget not fully returned after shutdown: %u\n",
                     vfxSystem.GetBudget().GetUsed());
        pass = false;
    }

    vfxSystem.Shutdown(&device);

    // ── Result ────────────────────────────────────────────────────────
    std::printf("\n[OK] Explosion composite: %zu emitters, %u total spawned, %d frames\n",
                descs.size(), totalSpawned, kTotalFrames);

    if (pass) {
        std::printf("[PASS] Explosion composite demo completed successfully.\n");
        return 0;
    } else {
        std::fprintf(stderr, "\n[FAIL] One or more validation checks failed.\n");
        return 1;
    }
}
