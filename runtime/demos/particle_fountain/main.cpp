/// Particle Fountain Demo — Headless VFX system proof-of-concept.
///
/// Demonstrates the GPU particle system's CPU-side logic by creating a single
/// emitter and ticking the VFX system for 180 frames (3 seconds at 60 FPS).
/// Prints per-frame spawn statistics and a final summary.
///
/// This is a headless (no window, no GPU) demo that validates:
/// - VFXSystem lifecycle (Initialize, Tick, Shutdown)
/// - ParticleEmitter budget allocation and spawn accumulator
/// - ParticleBudget lock-free allocation and return
/// - GPUParticleBuffer creation via mock RHI device
///
/// # Build
/// Compiled as part of the engine test suite or standalone:
///   zig build -Ddemo=particle_fountain
///
/// # Parameters (matching task spec)
/// - velocity:      [0, 10, 0]  (upward fountain)
/// - gravity:       [0, -9.81, 0]
/// - damping:       0.98
/// - lifetime:      3.0 s
/// - emission_rate: 1000 particles/sec
/// - max_particles: 50000

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <algorithm>

#include "mock_device.h"

// ─────────────────────────────────────────────────────────────────────────────
// VFX includes
// ─────────────────────────────────────────────────────────────────────────────

#include "engine/vfx/vfx_system.h"
#include "engine/vfx/components/particle_emitter.h"
#include "engine/vfx/core/particle_budget.h"
#include "engine/vfx/core/gpu_particle_buffer.h"
#include "engine/vfx/core/gpu_particle_sort.h"

// ─────────────────────────────────────────────────────────────────────────────
// Demo parameters
// ─────────────────────────────────────────────────────────────────────────────

static constexpr uint32_t kMaxParticles   = 50000u;
static constexpr float    kEmissionRate   = 1000.0f;   // particles/sec
static constexpr float    kLifetime       = 3.0f;      // seconds
static constexpr float    kDamping        = 0.98f;
static constexpr float    kDt             = 1.0f / 60.0f;
static constexpr int      kTotalFrames    = 180;       // 3 seconds at 60 FPS
static constexpr int      kReportInterval = 30;        // print stats every N frames
static constexpr uint32_t kBudgetMax      = 500000u;

// ─────────────────────────────────────────────────────────────────────────────
// Main
// ─────────────────────────────────────────────────────────────────────────────

int main() {
    std::printf("╔══════════════════════════════════════════════════════╗\n");
    std::printf("║         UzEngine — Particle Fountain Demo           ║\n");
    std::printf("║   Headless VFX + GPU Sort proof-of-concept          ║\n");
    std::printf("╚══════════════════════════════════════════════════════╝\n\n");

    // ── Setup ──────────────────────────────────────────────────────────
    uz::DemoMockDevice device;

    uz::VFXSystem vfxSystem;
    vfxSystem.Initialize(&device, kBudgetMax);
    std::printf("[Init] VFXSystem initialized — budget: %u particles\n", kBudgetMax);

    uz::ParticleEmitter emitter;
    emitter.maxParticles  = kMaxParticles;
    emitter.emissionRate  = kEmissionRate;
    emitter.lifetime      = kLifetime;
    emitter.gravity       = {0.0f, -9.81f, 0.0f};
    emitter.damping       = kDamping;
    emitter.spawnVelocity = {0.0f, 10.0f, 0.0f};
    emitter.isEmitting    = true;

    if (!emitter.Initialize(&device, vfxSystem.GetBudget())) {
        std::fprintf(stderr, "[Error] Failed to initialize emitter\n");
        vfxSystem.Shutdown(&device);
        return 1;
    }

    std::printf("[Init] Emitter initialized — capacity: %u, budget used: %u/%u\n",
                emitter.allocatedFromBudget,
                vfxSystem.GetBudget().GetUsed(),
                vfxSystem.GetBudget().GetMax());
    std::printf("[Init] Parameters: rate=%.0f/s, lifetime=%.1fs, damping=%.2f\n",
                kEmissionRate, kLifetime, kDamping);
    std::printf("[Init] Velocity: [%.1f, %.1f, %.1f], Gravity: [%.1f, %.2f, %.1f]\n\n",
                emitter.spawnVelocity.x, emitter.spawnVelocity.y, emitter.spawnVelocity.z,
                emitter.gravity.x, emitter.gravity.y, emitter.gravity.z);

    // ── Sort buffer validation ─────────────────────────────────────────
    bool pass = true;

    // Verify sort buffers were created by GPUParticleBuffer::Create().
    if (!emitter.buffer.sort.IsValid()) {
        std::fprintf(stderr, "[FAIL] Sort buffers not created\n");
        pass = false;
    } else {
        std::printf("[Init] Sort enabled — paddedCount: %u (capacity: %u)\n",
                    emitter.buffer.sort.GetPaddedCount(),
                    emitter.buffer.sort.capacity);
    }

    // Verify padded count is the correct power-of-two for the allocated capacity.
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

    // If sort buffer validation already failed, abort early before simulation.
    if (!pass) {
        vfxSystem.UnregisterEmitter(&emitter);
        emitter.Shutdown(&device, vfxSystem.GetBudget());
        vfxSystem.Shutdown(&device);
        std::fprintf(stderr, "\n[FAIL] Sort buffer validation failed — aborting.\n");
        return 1;
    }

    // ── Simulation loop ────────────────────────────────────────────────
    std::printf("%-8s  %-12s  %-12s  %-14s  %-10s\n",
                "Frame", "Spawned", "Accumulator", "Budget Used", "Emitters");
    std::printf("──────── ──────────── ──────────── ────────────── ──────────\n");

    uint32_t totalSpawned = 0;
    uint32_t peakSpawnPerFrame = 0;

    const auto startTime = std::chrono::high_resolution_clock::now();

    for (int frame = 0; frame < kTotalFrames; ++frame) {
        // Calculate spawn count (same as what VFXSystem::Tick does internally).
        const uint32_t spawnCount = emitter.CalculateSpawnCount(kDt);
        totalSpawned += spawnCount;
        peakSpawnPerFrame = std::max(peakSpawnPerFrame, spawnCount);

        // Tick the system (this also calls CalculateSpawnCount internally,
        // but since we already consumed this frame's spawn, the second call
        // will return 0 — the accumulator was already drained).
        vfxSystem.Tick(kDt);

        // Print stats at intervals and on first/last frames.
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
    // (pass was initialized in the sort buffer validation section above)

    // At 1000/sec × 3sec = 3000 expected spawns (may vary by ±1 due to accumulator).
    if (totalSpawned < 2990 || totalSpawned > 3010) {
        std::fprintf(stderr, "[FAIL] Expected ~3000 total spawns, got %u\n", totalSpawned);
        pass = false;
    }

    // Accumulator must be in [0, 1).
    if (emitter.spawnAccumulator < 0.0f || emitter.spawnAccumulator >= 1.0f) {
        std::fprintf(stderr, "[FAIL] Accumulator out of range: %.6f\n",
                     static_cast<double>(emitter.spawnAccumulator));
        pass = false;
    }

    // Budget must still show the emitter's allocation.
    if (vfxSystem.GetBudget().GetUsed() != kMaxParticles) {
        std::fprintf(stderr, "[FAIL] Budget used=%u, expected %u\n",
                     vfxSystem.GetBudget().GetUsed(), kMaxParticles);
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
    if (pass) {
        std::printf("\n[PASS] Particle fountain demo completed successfully.\n");
        return 0;
    } else {
        std::fprintf(stderr, "\n[FAIL] One or more validation checks failed.\n");
        return 1;
    }
}
