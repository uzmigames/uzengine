/// Particle Fountain Demo — Headless VFX proof-of-concept.
///
/// Loads emitter configuration from assets/particle_fountain.uzfx,
/// runs 180 frames of simulation, and validates spawn statistics.

#include "mock_device.h"
#include "engine/vfx/vfx_system.h"
#include "engine/vfx/components/particle_emitter.h"
#include "engine/vfx/core/particle_budget.h"
#include "engine/vfx/core/gpu_particle_buffer.h"
#include "engine/vfx/core/gpu_particle_sort.h"
#include "engine/asset/formats/uzfx_loader.h"

#include <chrono>
#include <cstdio>
#include <algorithm>
#include "file_system.h"

static std::string uz::FileReadAllText(const char* path) {
    std::ifstream f(path);
    if (!f.is_open()) return {};
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

int main() {
    std::printf("╔══════════════════════════════════════════════════════╗\n");
    std::printf("║         UzEngine — Particle Fountain Demo           ║\n");
    std::printf("║   Headless VFX + GPU Sort proof-of-concept          ║\n");
    std::printf("╚══════════════════════════════════════════════════════╝\n\n");

    // ── Load .uzfx asset ────────────────────────────────────────────────
    auto json = uz::FileReadAllText("assets/particle_fountain.uzfx");
    if (json.empty()) {
        std::fprintf(stderr, "[Error] Cannot read assets/particle_fountain.uzfx\n");
        return 1;
    }
    auto desc = uz::UzfxLoader::Load(json);
    if (desc.name.empty()) {
        std::fprintf(stderr, "[Error] Failed to parse particle_fountain.uzfx\n");
        return 1;
    }
    std::printf("[Asset] Loaded '%s' (version %u)\n", desc.name.c_str(), desc.version);

    // ── Setup ──────────────────────────────────────────────────────────
    uz::DemoMockDevice device;

    uz::VFXSystem vfxSystem;
    constexpr uint32_t kBudgetMax = 500000u;
    vfxSystem.Initialize(&device, kBudgetMax);
    std::printf("[Init] VFXSystem initialized — budget: %u particles\n", kBudgetMax);

    // ── Instantiate emitter from .uzfx ─────────────────────────────────
    uz::ParticleEmitter emitter;
    if (!uz::UzfxLoader::Instantiate(desc, emitter, &device, vfxSystem.GetBudget())) {
        std::fprintf(stderr, "[Error] Failed to instantiate emitter\n");
        vfxSystem.Shutdown(&device);
        return 1;
    }

    std::printf("[Init] Emitter initialized — capacity: %u, budget used: %u/%u\n",
                emitter.allocatedFromBudget,
                vfxSystem.GetBudget().GetUsed(),
                vfxSystem.GetBudget().GetMax());
    std::printf("[Init] Parameters: rate=%.0f/s, lifetime=%.1fs\n",
                desc.emissionRate, desc.lifetime);

    // ── Sort buffer validation ─────────────────────────────────────────
    bool pass = true;

    if (!emitter.buffer.sort.IsValid()) {
        std::fprintf(stderr, "[FAIL] Sort buffers not created\n");
        pass = false;
    } else {
        std::printf("[Init] Sort enabled — paddedCount: %u (capacity: %u)\n",
                    emitter.buffer.sort.GetPaddedCount(),
                    emitter.buffer.sort.capacity);
    }

    const uint32_t expectedPadded = uz::NextPowerOfTwo(emitter.allocatedFromBudget);
    if (emitter.buffer.sort.GetPaddedCount() != expectedPadded) {
        std::fprintf(stderr, "[FAIL] Padded count %u != expected %u\n",
                     emitter.buffer.sort.GetPaddedCount(), expectedPadded);
        pass = false;
    } else {
        std::printf("[Init] Padded count verified: %u == NextPowerOfTwo(%u)\n\n",
                    emitter.buffer.sort.GetPaddedCount(), emitter.allocatedFromBudget);
    }

    vfxSystem.RegisterEmitter(&emitter);

    if (!pass) {
        vfxSystem.UnregisterEmitter(&emitter);
        emitter.Shutdown(&device, vfxSystem.GetBudget());
        vfxSystem.Shutdown(&device);
        std::fprintf(stderr, "\n[FAIL] Sort buffer validation failed — aborting.\n");
        return 1;
    }

    // ── Simulation loop ────────────────────────────────────────────────
    constexpr float kDt          = 1.0f / 60.0f;
    constexpr int   kTotalFrames = 180;
    constexpr int   kReportInterval = 30;

    std::printf("%-8s  %-12s  %-12s  %-14s  %-10s\n",
                "Frame", "Spawned", "Accumulator", "Budget Used", "Emitters");
    std::printf("──────── ──────────── ──────────── ────────────── ──────────\n");

    uint32_t totalSpawned = 0;
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
    const double elapsedMs = std::chrono::duration<double, std::milli>(endTime - startTime).count();

    // ── Summary ────────────────────────────────────────────────────────
    std::printf("\n──────────────────────────────────────────────\n");
    std::printf("SIMULATION SUMMARY\n");
    std::printf("──────────────────────────────────────────────\n");
    std::printf("  Frames simulated:       %d (%.1f seconds)\n",
                kTotalFrames, kTotalFrames * kDt);
    std::printf("  Total particles spawned: %u\n", totalSpawned);
    std::printf("  Peak spawn/frame:        %u\n", peakSpawnPerFrame);
    std::printf("  Final accumulator:       %.6f\n",
                static_cast<double>(emitter.spawnAccumulator));
    std::printf("  Budget used:             %u / %u\n",
                vfxSystem.GetBudget().GetUsed(),
                vfxSystem.GetBudget().GetMax());
    std::printf("  Sort buffer padded:     %u\n", emitter.buffer.sort.GetPaddedCount());
    std::printf("  Sort buffer memory:     %uKB\n",
                emitter.buffer.sort.GetPaddedCount() * 4 * 2 / 1024);
    std::printf("  CPU wall time:           %.2f ms\n", elapsedMs);
    std::printf("──────────────────────────────────────────────\n");

    // ── Validation ─────────────────────────────────────────────────────
    // At 1000/sec x 3sec = 3000 expected spawns (may vary by +/-1 due to accumulator).
    if (totalSpawned < 2990 || totalSpawned > 3010) {
        std::fprintf(stderr, "[FAIL] Expected ~3000 total spawns, got %u\n", totalSpawned);
        pass = false;
    }

    if (emitter.spawnAccumulator < 0.0f || emitter.spawnAccumulator >= 1.0f) {
        std::fprintf(stderr, "[FAIL] Accumulator out of range: %.6f\n",
                     static_cast<double>(emitter.spawnAccumulator));
        pass = false;
    }

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
        std::printf("[PASS] Particle fountain demo completed successfully.\n");
        return 0;
    } else {
        std::fprintf(stderr, "\n[FAIL] One or more validation checks failed.\n");
        return 1;
    }
}
