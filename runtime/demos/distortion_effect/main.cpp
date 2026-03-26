/// Distortion Effect Demo — Headless screen-space distortion VFX showcase.
///
/// Loads two emitter configurations from .uzfx assets (heat shimmer, glass sphere),
/// validates distortion struct layouts, and runs 120 frames of simulation with
/// per-frame spawn statistics.

#include "mock_device.h"
#include "engine/vfx/vfx_system.h"
#include "engine/vfx/components/particle_emitter.h"
#include "engine/vfx/core/particle_budget.h"
#include "engine/vfx/modules/emitter_module_stack.h"
#include "engine/vfx/asset/uzfx_loader.h"

// Distortion pass struct definitions (no GPU execution -- header types only).
#include "engine/vfx/passes/distortion_pass.h"
#include "engine/vfx/passes/distortion_resolve_pass.h"

#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <memory>
#include <string>

static std::string ReadFile(const char* path) {
    std::ifstream f(path);
    if (!f.is_open()) return {};
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

// ─────────────────────────────────────────────────────────────────────────────
// Demo parameters
// ─────────────────────────────────────────────────────────────────────────────

static constexpr float    kDt             = 1.0f / 60.0f;
static constexpr int      kTotalFrames    = 120;
static constexpr int      kReportInterval = 30;
static constexpr uint32_t kBudgetMax      = 500000u;

// ─────────────────────────────────────────────────────────────────────────────
// Main
// ─────────────────────────────────────────────────────────────────────────────

int main() {
    std::printf("==========================================================\n");
    std::printf("      UzEngine -- Distortion Effect Demo\n");
    std::printf("  Heat shimmer + glass sphere (headless, no GPU)\n");
    std::printf("==========================================================\n\n");

    // ── Load .uzfx assets ────────────────────────────────────────────────
    auto heatJson = ReadFile("assets/distortion_heat_shimmer.uzfx");
    if (heatJson.empty()) {
        std::fprintf(stderr, "[Error] Cannot read assets/distortion_heat_shimmer.uzfx\n");
        return 1;
    }
    auto heatDesc = uz::UzfxLoader::Load(heatJson);
    if (heatDesc.name.empty()) {
        std::fprintf(stderr, "[Error] Failed to parse distortion_heat_shimmer.uzfx\n");
        return 1;
    }
    std::printf("[Asset] Loaded '%s' (version %u)\n", heatDesc.name.c_str(), heatDesc.version);

    auto glassJson = ReadFile("assets/distortion_glass_sphere.uzfx");
    if (glassJson.empty()) {
        std::fprintf(stderr, "[Error] Cannot read assets/distortion_glass_sphere.uzfx\n");
        return 1;
    }
    auto glassDesc = uz::UzfxLoader::Load(glassJson);
    if (glassDesc.name.empty()) {
        std::fprintf(stderr, "[Error] Failed to parse distortion_glass_sphere.uzfx\n");
        return 1;
    }
    std::printf("[Asset] Loaded '%s' (version %u)\n\n", glassDesc.name.c_str(), glassDesc.version);

    // ── Setup ──────────────────────────────────────────────────────────
    uz::DemoMockDevice device;

    uz::VFXSystem vfxSystem;
    vfxSystem.Initialize(&device, kBudgetMax);
    std::printf("[Init] VFXSystem initialized -- budget: %u particles\n\n", kBudgetMax);

    // ── Instantiate emitter A: heat_shimmer ─────────────────────────────
    uz::ParticleEmitter heatEmitter;
    if (!uz::UzfxLoader::Instantiate(heatDesc, heatEmitter, &device, vfxSystem.GetBudget())) {
        std::fprintf(stderr, "[Error] Failed to instantiate heat_shimmer emitter\n");
        vfxSystem.Shutdown(&device);
        return 1;
    }

    std::printf("[Init] heat_shimmer emitter initialized\n");
    std::printf("       capacity=%u  rate=%.1f/s  lifetime=%.2fs\n",
                heatEmitter.allocatedFromBudget, heatDesc.emissionRate, heatDesc.lifetime);

    // ── Instantiate emitter B: glass_sphere ─────────────────────────────
    uz::ParticleEmitter glassEmitter;
    if (!uz::UzfxLoader::Instantiate(glassDesc, glassEmitter, &device, vfxSystem.GetBudget())) {
        std::fprintf(stderr, "[Error] Failed to instantiate glass_sphere emitter\n");
        heatEmitter.Shutdown(&device, vfxSystem.GetBudget());
        vfxSystem.Shutdown(&device);
        return 1;
    }

    std::printf("[Init] glass_sphere emitter initialized\n");
    std::printf("       capacity=%u  rate=%.1f/s  lifetime=%.2fs\n\n",
                glassEmitter.allocatedFromBudget, glassDesc.emissionRate, glassDesc.lifetime);

    // ── Register emitters ────────────────────────────────────────────────
    vfxSystem.RegisterEmitter(&heatEmitter);
    vfxSystem.RegisterEmitter(&glassEmitter);

    std::printf("[Init] Both emitters registered. Active count: %u\n",
                vfxSystem.GetActiveEmitterCount());

    // ── Distortion struct verification ───────────────────────────────────

    std::printf("\n── Distortion Struct Layout ─────────────────────────────────────\n");

    static_assert(sizeof(uz::DistortionConstants) == 16,
                  "DistortionConstants must be 16 bytes");
    {
        uz::DistortionConstants dc;
        std::printf("  DistortionConstants:\n");
        std::printf("    sizeof = %zu  (expected 16)\n", sizeof(dc));
        std::printf("    distortionStrength = %.4f  (expected 1.0000)\n",
                    static_cast<double>(dc.distortionStrength));
        std::printf("    normalMapSrvIndex  = %u   (expected UINT32_MAX=%u)\n",
                    dc.normalMapSrvIndex, UINT32_MAX);
        std::printf("    _pad0 = %.4f  _pad1 = %.4f\n",
                    static_cast<double>(dc._pad0),
                    static_cast<double>(dc._pad1));
    }

    static_assert(sizeof(uz::DistortionResolveConstants) == 16,
                  "DistortionResolveConstants must be 16 bytes");
    {
        uz::DistortionResolveConstants drc;
        std::printf("  DistortionResolveConstants:\n");
        std::printf("    sizeof = %zu  (expected 16)\n", sizeof(drc));
        std::printf("    colorTextureSrvIndex      = %u\n",
                    drc.colorTextureSrvIndex);
        std::printf("    distortionTextureSrvIndex = %u\n",
                    drc.distortionTextureSrvIndex);
        std::printf("    _pad0 = %u  _pad1 = %u\n",
                    drc._pad0, drc._pad1);
    }

    // DistortionEmitterData: populate with mock handles.
    {
        uz::DistortionEmitterData ded;
        ded.buffer = &heatEmitter.buffer;
        ded.pipeline = device.CreateGraphicsPipeline({});
        ded.billboardCB  = {nullptr, 0x1000, {0, 0}, 0, 256};
        ded.distortionCB = {nullptr, 0x2000, {0, 0}, 0, 256};
        ded.readSetSrvIndex = 42u;
        ded.distanceToCamera = 350.0f;

        std::printf("  DistortionEmitterData (heat_shimmer mock):\n");
        std::printf("    buffer ptr      = %s\n", ded.buffer ? "valid" : "null");
        std::printf("    readSetSrvIndex = %u\n", ded.readSetSrvIndex);
        std::printf("    distanceToCamera= %.2f cm\n",
                    static_cast<double>(ded.distanceToCamera));
        std::printf("    billboardCB.gpuAddr  = 0x%llX\n",
                    static_cast<unsigned long long>(ded.billboardCB.gpuAddr));
        std::printf("    distortionCB.gpuAddr = 0x%llX\n",
                    static_cast<unsigned long long>(ded.distortionCB.gpuAddr));
    }

    std::printf("─────────────────────────────────────────────────────────────────\n\n");

    // ── Simulation loop ──────────────────────────────────────────────────
    std::printf("%-8s  %-14s  %-14s  %-14s  %-14s  %-10s\n",
                "Frame",
                "Heat Spawned",
                "Glass Spawned",
                "Heat Accum",
                "Glass Accum",
                "BudgetUsed");
    std::printf("-------- -------------- -------------- -------------- -------------- ----------\n");

    uint32_t totalHeatSpawned  = 0;
    uint32_t totalGlassSpawned = 0;
    uint32_t peakHeatPerFrame  = 0;
    uint32_t peakGlassPerFrame = 0;

    const auto startTime = std::chrono::high_resolution_clock::now();

    for (int frame = 0; frame < kTotalFrames; ++frame) {
        const uint32_t heatSpawn  = heatEmitter.CalculateSpawnCount(kDt);
        const uint32_t glassSpawn = glassEmitter.CalculateSpawnCount(kDt);

        totalHeatSpawned  += heatSpawn;
        totalGlassSpawned += glassSpawn;
        peakHeatPerFrame   = std::max(peakHeatPerFrame,  heatSpawn);
        peakGlassPerFrame  = std::max(peakGlassPerFrame, glassSpawn);

        vfxSystem.Tick(kDt);

        if (frame == 0 || frame == kTotalFrames - 1 ||
            (frame + 1) % kReportInterval == 0) {
            std::printf("%-8d  %-14u  %-14u  %-14.6f  %-14.6f  %-10u\n",
                        frame,
                        heatSpawn,
                        glassSpawn,
                        static_cast<double>(heatEmitter.spawnAccumulator),
                        static_cast<double>(glassEmitter.spawnAccumulator),
                        vfxSystem.GetBudget().GetUsed());
        }
    }

    const auto endTime = std::chrono::high_resolution_clock::now();
    const double elapsedMs =
        std::chrono::duration<double, std::milli>(endTime - startTime).count();

    // ── Summary ────────────────────────────────────────────────────────
    std::printf("\n==========================================================\n");
    std::printf("DISTORTION EFFECT DEMO -- SUMMARY\n");
    std::printf("==========================================================\n");
    std::printf("  Frames simulated:          %d (%.1f seconds @ 60 FPS)\n",
                kTotalFrames, kTotalFrames * kDt);
    std::printf("\n");
    std::printf("  heat_shimmer emitter:\n");
    std::printf("    Total particles spawned: %u\n",  totalHeatSpawned);
    std::printf("    Peak spawn/frame:        %u\n",  peakHeatPerFrame);
    std::printf("    Final accumulator:       %.6f\n",
                static_cast<double>(heatEmitter.spawnAccumulator));
    std::printf("\n");
    std::printf("  glass_sphere emitter:\n");
    std::printf("    Total particles spawned: %u\n",  totalGlassSpawned);
    std::printf("    Peak spawn/frame:        %u\n",  peakGlassPerFrame);
    std::printf("    Final accumulator:       %.6f\n",
                static_cast<double>(glassEmitter.spawnAccumulator));
    std::printf("\n");
    std::printf("  Budget used:               %u / %u\n",
                vfxSystem.GetBudget().GetUsed(),
                vfxSystem.GetBudget().GetMax());
    std::printf("  CPU wall time:             %.2f ms\n", elapsedMs);
    std::printf("\n");
    std::printf("  DistortionConstants sizeof:        %zu bytes (expected 16)\n",
                sizeof(uz::DistortionConstants));
    std::printf("  DistortionResolveConstants sizeof: %zu bytes (expected 16)\n",
                sizeof(uz::DistortionResolveConstants));
    std::printf("==========================================================\n");

    // ── Validation ─────────────────────────────────────────────────────
    bool pass = true;

    // Emitters must have initialized.
    if (!heatEmitter.initialized) {
        std::fprintf(stderr, "[FAIL] heat_shimmer emitter not initialized\n");
        pass = false;
    }
    if (!glassEmitter.initialized) {
        std::fprintf(stderr, "[FAIL] glass_sphere emitter not initialized\n");
        pass = false;
    }

    // Both emitters must have spawned particles over the 2-second run.
    {
        const uint32_t expectedHeat  = static_cast<uint32_t>(heatDesc.emissionRate  * kTotalFrames * kDt);
        const uint32_t expectedGlass = static_cast<uint32_t>(glassDesc.emissionRate * kTotalFrames * kDt);
        const uint32_t tolerance = 10;

        if (totalHeatSpawned < expectedHeat - tolerance ||
            totalHeatSpawned > expectedHeat + tolerance) {
            std::fprintf(stderr, "[FAIL] heat_shimmer: expected ~%u spawns, got %u\n",
                         expectedHeat, totalHeatSpawned);
            pass = false;
        }
        if (totalGlassSpawned < expectedGlass - tolerance ||
            totalGlassSpawned > expectedGlass + tolerance) {
            std::fprintf(stderr, "[FAIL] glass_sphere: expected ~%u spawns, got %u\n",
                         expectedGlass, totalGlassSpawned);
            pass = false;
        }
    }

    // Spawn accumulators must be in [0, 1).
    if (heatEmitter.spawnAccumulator < 0.0f || heatEmitter.spawnAccumulator >= 1.0f) {
        std::fprintf(stderr, "[FAIL] heat_shimmer accumulator out of range: %.6f\n",
                     static_cast<double>(heatEmitter.spawnAccumulator));
        pass = false;
    }
    if (glassEmitter.spawnAccumulator < 0.0f || glassEmitter.spawnAccumulator >= 1.0f) {
        std::fprintf(stderr, "[FAIL] glass_sphere accumulator out of range: %.6f\n",
                     static_cast<double>(glassEmitter.spawnAccumulator));
        pass = false;
    }

    // DistortionConstants: verify default field values.
    {
        const uz::DistortionConstants dc;
        if (dc.distortionStrength != 1.0f) {
            std::fprintf(stderr, "[FAIL] DistortionConstants.distortionStrength = %.4f, expected 1.0\n",
                         static_cast<double>(dc.distortionStrength));
            pass = false;
        }
        if (dc.normalMapSrvIndex != UINT32_MAX) {
            std::fprintf(stderr, "[FAIL] DistortionConstants.normalMapSrvIndex = %u, expected UINT32_MAX\n",
                         dc.normalMapSrvIndex);
            pass = false;
        }
        if (dc._pad0 != 0.0f || dc._pad1 != 0.0f) {
            std::fprintf(stderr, "[FAIL] DistortionConstants padding is non-zero\n");
            pass = false;
        }
        if (sizeof(dc) != 16) {
            std::fprintf(stderr, "[FAIL] DistortionConstants sizeof=%zu, expected 16\n",
                         sizeof(dc));
            pass = false;
        }
    }

    // DistortionResolveConstants: verify default field values and size.
    {
        const uz::DistortionResolveConstants drc;
        if (drc.colorTextureSrvIndex != UINT32_MAX) {
            std::fprintf(stderr,
                         "[FAIL] DistortionResolveConstants.colorTextureSrvIndex = %u, expected UINT32_MAX\n",
                         drc.colorTextureSrvIndex);
            pass = false;
        }
        if (drc.distortionTextureSrvIndex != UINT32_MAX) {
            std::fprintf(stderr,
                         "[FAIL] DistortionResolveConstants.distortionTextureSrvIndex = %u, expected UINT32_MAX\n",
                         drc.distortionTextureSrvIndex);
            pass = false;
        }
        if (sizeof(drc) != 16) {
            std::fprintf(stderr, "[FAIL] DistortionResolveConstants sizeof=%zu, expected 16\n",
                         sizeof(drc));
            pass = false;
        }
    }

    // Budget must account for both emitters.
    {
        const uint32_t expectedBudgetUsed =
            heatEmitter.allocatedFromBudget + glassEmitter.allocatedFromBudget;
        if (vfxSystem.GetBudget().GetUsed() != expectedBudgetUsed) {
            std::fprintf(stderr,
                         "[FAIL] Budget used=%u, expected %u (heat=%u + glass=%u)\n",
                         vfxSystem.GetBudget().GetUsed(),
                         expectedBudgetUsed,
                         heatEmitter.allocatedFromBudget,
                         glassEmitter.allocatedFromBudget);
            pass = false;
        }
    }

    // ── Cleanup ────────────────────────────────────────────────────────
    vfxSystem.UnregisterEmitter(&heatEmitter);
    vfxSystem.UnregisterEmitter(&glassEmitter);

    heatEmitter.Shutdown(&device, vfxSystem.GetBudget());
    glassEmitter.Shutdown(&device, vfxSystem.GetBudget());

    if (vfxSystem.GetBudget().GetUsed() != 0) {
        std::fprintf(stderr, "[FAIL] Budget not fully returned after shutdown: %u\n",
                     vfxSystem.GetBudget().GetUsed());
        pass = false;
    }

    vfxSystem.Shutdown(&device);

    // ── Result ─────────────────────────────────────────────────────────
    std::printf("\n[OK] Distortion effect: 2 emitters (heat=%u, glass=%u), %d frames\n",
                heatEmitter.allocatedFromBudget, glassEmitter.allocatedFromBudget, kTotalFrames);

    if (pass) {
        std::printf("[PASS] Distortion effect demo completed successfully.\n");
        return 0;
    } else {
        std::fprintf(stderr, "\n[FAIL] One or more validation checks failed.\n");
        return 1;
    }
}
