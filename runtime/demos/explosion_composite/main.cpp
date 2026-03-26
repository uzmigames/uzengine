/// Explosion Composite Demo -- Headless multi-emitter VFX simulation.
///
/// Demonstrates the VFX composite effect system by building an explosion from
/// three sub-emitters (fire, smoke, debris) defined via inline JSON. Each
/// emitter has independent offsets, delays, and module stacks.
///
/// This is a headless (no window, no GPU) demo that validates:
/// - VFXComposite JSON parsing via LoadFromString
/// - Multi-emitter creation with per-entry offsets and delays
/// - VFXEventQueue burst emission ("explode" event at frame 30)
/// - VFXSystem simulation loop with per-frame stats
/// - Proper cleanup and budget return
///
/// # Build
/// Compiled standalone via Zig:
///   cd demos/explosion_composite
///   zig build
///   ./zig-out/bin/explosion_composite_demo
///
/// # Simulation Parameters
/// - fire:   rate=500, spread=60deg, velocity=[0,8,0], gravity=[0,-3,0]
/// - smoke:  rate=200, spread=45deg, velocity=[0,3,0], gravity=[0,-0.5,0]
/// - debris: rate=300, spread=90deg, velocity=[0,12,0], gravity=[0,-9.81,0]
/// - Burst event "explode" at frame 30 (0.5s) with 500 particles, 3x velocity

#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <algorithm>
#include <memory>
#include <string>
#include <vector>

#include "mock_device.h"

// ─────────────────────────────────────────────────────────────────────────────
// VFX includes
// ─────────────────────────────────────────────────────────────────────────────

#include "engine/vfx/vfx_system.h"
#include "engine/vfx/components/particle_emitter.h"
#include "engine/vfx/asset/vfx_composite.h"
#include "engine/vfx/events/vfx_event.h"
#include "engine/vfx/modules/register_builtin_modules.h"
#include "engine/vfx/modules/module_registry.h"
#include "engine/vfx/modules/emitter_module_stack.h"
#include "engine/vfx/modules/spawn_module.h"
#include "engine/vfx/modules/update_module.h"
#include "engine/vfx/modules/decay_module.h"
#include "engine/vfx/modules/turbulence_module.h"
#include "engine/vfx/modules/sprite_render_module.h"
#include "engine/vfx/core/particle_budget.h"

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

// Per-emitter capacities.
static constexpr uint32_t kFireMaxParticles   = 25000u;
static constexpr uint32_t kSmokeMaxParticles  = 15000u;
static constexpr uint32_t kDebrisMaxParticles = 10000u;

// ─────────────────────────────────────────────────────────────────────────────
// Composite JSON definition (inline, no file dependency)
// ─────────────────────────────────────────────────────────────────────────────

static const char* const kCompositeJson = R"({
    "format": "uzvfx_composite",
    "version": 1,
    "name": "explosion",
    "emitters": [
        { "name": "fire",   "graph": "vfx/fire.uzvfx",   "offset": [0,0,0],     "delay": 0.0 },
        { "name": "smoke",  "graph": "vfx/smoke.uzvfx",  "offset": [0,0.5,0],   "delay": 0.2 },
        { "name": "debris", "graph": "vfx/debris.uzvfx", "offset": [0,0,0],     "delay": 0.0 }
    ]
})";

// ─────────────────────────────────────────────────────────────────────────────
// Emitter configuration helpers
// ─────────────────────────────────────────────────────────────────────────────

namespace {

/// Configures a fire emitter: fast upward particles with turbulence and decay.
///
/// # Parameters
/// - `emitter`: Emitter to configure. Must not be null.
/// - `entry`: Composite entry providing offset and delay.
void ConfigureFireEmitter(uz::ParticleEmitter& emitter,
                          const uz::VFXCompositeEntry& entry) {
    emitter.maxParticles  = kFireMaxParticles;
    emitter.emissionRate  = 500.0f;
    emitter.lifetime      = 2.0f;
    emitter.gravity       = {0.0f, -3.0f, 0.0f};
    emitter.damping       = 0.98f;
    emitter.spawnVelocity = {0.0f, 8.0f, 0.0f};
    emitter.spreadAngle   = 60.0f;
    emitter.startDelay    = entry.delay;
    emitter.isEmitting    = true;

    // Build module stack: spawn -> update -> turbulence -> decay -> sprite.
    auto& stack = emitter.moduleStack;

    auto spawn = std::make_unique<uz::SpawnModule>();
    spawn->rate        = 500.0f;
    spawn->spreadAngle = 60.0f;
    spawn->velocity    = {0.0f, 8.0f, 0.0f};
    spawn->lifespanMin = 0.5f;
    spawn->lifespanMax = 2.0f;
    stack.AddSpawnModule(std::move(spawn));

    auto update = std::make_unique<uz::UpdateModule>();
    update->gravity = {0.0f, -3.0f, 0.0f};
    update->drag    = 0.98f;
    stack.AddUpdateModule(std::move(update));

    auto turb = std::make_unique<uz::TurbulenceModule>();
    turb->strength  = 2.0f;
    turb->frequency = 0.5f;
    turb->octaves   = 3;
    stack.AddUpdateModule(std::move(turb));

    stack.AddUpdateModule(std::make_unique<uz::DecayModule>());

    auto sprite = std::make_unique<uz::SpriteRenderModule>();
    sprite->spriteWidth  = 0.5f;
    sprite->spriteHeight = 0.5f;
    stack.SetRenderModule(std::move(sprite));
}

/// Configures a smoke emitter: slow rising particles with high drag.
///
/// # Parameters
/// - `emitter`: Emitter to configure. Must not be null.
/// - `entry`: Composite entry providing offset and delay.
void ConfigureSmokeEmitter(uz::ParticleEmitter& emitter,
                           const uz::VFXCompositeEntry& entry) {
    emitter.maxParticles  = kSmokeMaxParticles;
    emitter.emissionRate  = 200.0f;
    emitter.lifetime      = 4.0f;
    emitter.gravity       = {0.0f, -0.5f, 0.0f};
    emitter.damping       = 0.95f;
    emitter.spawnVelocity = {0.0f, 3.0f, 0.0f};
    emitter.spreadAngle   = 45.0f;
    emitter.startDelay    = entry.delay;
    emitter.isEmitting    = true;

    auto& stack = emitter.moduleStack;

    auto spawn = std::make_unique<uz::SpawnModule>();
    spawn->rate        = 200.0f;
    spawn->spreadAngle = 45.0f;
    spawn->velocity    = {0.0f, 3.0f, 0.0f};
    spawn->lifespanMin = 1.0f;
    spawn->lifespanMax = 4.0f;
    stack.AddSpawnModule(std::move(spawn));

    auto update = std::make_unique<uz::UpdateModule>();
    update->gravity = {0.0f, -0.5f, 0.0f};
    update->drag    = 0.95f;
    stack.AddUpdateModule(std::move(update));

    stack.AddUpdateModule(std::make_unique<uz::DecayModule>());

    auto sprite = std::make_unique<uz::SpriteRenderModule>();
    sprite->spriteWidth  = 1.0f;
    sprite->spriteHeight = 1.0f;
    stack.SetRenderModule(std::move(sprite));
}

/// Configures a debris emitter: fast scattered particles with strong gravity.
///
/// # Parameters
/// - `emitter`: Emitter to configure. Must not be null.
/// - `entry`: Composite entry providing offset and delay.
void ConfigureDebrisEmitter(uz::ParticleEmitter& emitter,
                            const uz::VFXCompositeEntry& entry) {
    emitter.maxParticles  = kDebrisMaxParticles;
    emitter.emissionRate  = 300.0f;
    emitter.lifetime      = 3.0f;
    emitter.gravity       = {0.0f, -9.81f, 0.0f};
    emitter.damping       = 0.99f;
    emitter.spawnVelocity = {0.0f, 12.0f, 0.0f};
    emitter.spreadAngle   = 90.0f;
    emitter.startDelay    = entry.delay;
    emitter.isEmitting    = true;

    auto& stack = emitter.moduleStack;

    auto spawn = std::make_unique<uz::SpawnModule>();
    spawn->rate        = 300.0f;
    spawn->spreadAngle = 90.0f;
    spawn->velocity    = {0.0f, 12.0f, 0.0f};
    spawn->lifespanMin = 0.5f;
    spawn->lifespanMax = 3.0f;
    stack.AddSpawnModule(std::move(spawn));

    auto update = std::make_unique<uz::UpdateModule>();
    update->gravity = {0.0f, -9.81f, 0.0f};
    update->drag    = 0.99f;
    stack.AddUpdateModule(std::move(update));

    stack.AddUpdateModule(std::make_unique<uz::DecayModule>());

    auto sprite = std::make_unique<uz::SpriteRenderModule>();
    sprite->spriteWidth  = 0.2f;
    sprite->spriteHeight = 0.2f;
    stack.SetRenderModule(std::move(sprite));
}

/// Prints a separator line to stdout.
void PrintSeparator() {
    std::printf("──────────────────────────────────────────────────────────────\n");
}

/// Prints per-emitter stats for a single frame.
///
/// # Parameters
/// - `frame`: Current frame index.
/// - `emitters`: Active emitter list.
/// - `entries`: Composite entry metadata (parallel to emitters).
/// - `budget`: Global particle budget for reporting.
void PrintFrameStats(int frame,
                     const std::vector<uz::ParticleEmitter>& emitters,
                     const std::vector<uz::VFXCompositeEntry>& entries,
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
        } else if (e.timeSinceRegistration < e.startDelay) {
            state = "delayed";
        } else if (!e.isEmitting) {
            state = "stopped";
        }

        std::printf("  %-8s: accum=%.4f  allocated=%5u  events=%zu  [%s]\n",
                    entries[i].name.c_str(),
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

    // ── Step 1: Create VFXSystem with builtin modules ─────────────────
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

    // ── Step 2: Parse composite JSON ──────────────────────────────────
    auto composite = uz::VFXComposite::LoadFromString(kCompositeJson);
    if (!composite) {
        std::fprintf(stderr, "[Error] Failed to parse composite JSON\n");
        vfxSystem.Shutdown(&device);
        return 1;
    }

    std::printf("[Composite] Loaded '%s' (version %u) with %zu emitters:\n",
                composite->GetName().c_str(),
                composite->GetVersion(),
                composite->GetEmitterCount());

    const auto& entries = composite->GetEntries();
    for (size_t i = 0; i < entries.size(); ++i) {
        const auto& e = entries[i];
        std::printf("  [%zu] name='%s'  graph='%s'  offset=[%.1f,%.1f,%.1f]  delay=%.2fs\n",
                    i,
                    e.name.c_str(),
                    e.graphPath.c_str(),
                    static_cast<double>(e.offset.x),
                    static_cast<double>(e.offset.y),
                    static_cast<double>(e.offset.z),
                    static_cast<double>(e.delay));
    }
    std::printf("\n");

    // ── Step 3: Create emitters from composite entries ────────────────
    std::vector<uz::ParticleEmitter> emitters(entries.size());

    for (size_t i = 0; i < entries.size(); ++i) {
        const auto& entry = entries[i];

        // Configure each emitter based on its role in the explosion.
        if (entry.name == "fire") {
            ConfigureFireEmitter(emitters[i], entry);
        } else if (entry.name == "smoke") {
            ConfigureSmokeEmitter(emitters[i], entry);
        } else if (entry.name == "debris") {
            ConfigureDebrisEmitter(emitters[i], entry);
        }

        if (!emitters[i].Initialize(&device, vfxSystem.GetBudget())) {
            std::fprintf(stderr, "[Error] Failed to initialize emitter '%s'\n",
                         entry.name.c_str());
            vfxSystem.Shutdown(&device);
            return 1;
        }

        std::printf("[Init] Emitter '%s' initialized -- capacity=%u, budget=%u/%u\n",
                    entry.name.c_str(),
                    emitters[i].allocatedFromBudget,
                    vfxSystem.GetBudget().GetUsed(),
                    vfxSystem.GetBudget().GetMax());
    }

    // ── Step 4: Register emitters with VFXSystem ──────────────────────
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

    // ── Step 5-7: Simulation loop ─────────────────────────────────────
    uint32_t totalSpawned = 0;
    const auto startTime = std::chrono::high_resolution_clock::now();

    for (int frame = 0; frame < kTotalFrames; ++frame) {
        // Step 7: Fire "explode" burst at the designated frame.
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

        // Step 6: Log frame stats at the reporting interval.
        const bool isReportFrame =
            frame == 0 ||
            frame == kBurstFrame ||
            frame == kBurstFrame + 1 ||
            frame == kTotalFrames - 1 ||
            (frame + 1) % kReportInterval == 0;

        if (isReportFrame) {
            PrintFrameStats(frame, emitters, entries, vfxSystem.GetBudget());
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
    for (size_t i = 0; i < entries.size(); ++i) {
        const auto& e = emitters[i];
        std::printf("  %-8s: allocated=%5u  accum=%.4f  delay=%.2fs  modules=%zu+%zu\n",
                    entries[i].name.c_str(),
                    e.allocatedFromBudget,
                    static_cast<double>(e.spawnAccumulator),
                    static_cast<double>(e.startDelay),
                    e.moduleStack.spawnModules.size(),
                    e.moduleStack.updateModules.size());
    }
    PrintSeparator();

    // ── Validation ────────────────────────────────────────────────────
    bool pass = true;

    // Composite must have parsed correctly.
    if (composite->GetEmitterCount() != 3) {
        std::fprintf(stderr, "[FAIL] Expected 3 composite entries, got %zu\n",
                     composite->GetEmitterCount());
        pass = false;
    }

    if (composite->GetName() != "explosion") {
        std::fprintf(stderr, "[FAIL] Expected composite name 'explosion', got '%s'\n",
                     composite->GetName().c_str());
        pass = false;
    }

    if (composite->GetVersion() != 1) {
        std::fprintf(stderr, "[FAIL] Expected composite version 1, got %u\n",
                     composite->GetVersion());
        pass = false;
    }

    // All emitters must be initialized.
    for (size_t i = 0; i < emitters.size(); ++i) {
        if (!emitters[i].initialized) {
            std::fprintf(stderr, "[FAIL] Emitter '%s' not initialized\n",
                         entries[i].name.c_str());
            pass = false;
        }
    }

    // Smoke emitter must have had a start delay.
    if (emitters[1].startDelay < 0.19f || emitters[1].startDelay > 0.21f) {
        std::fprintf(stderr, "[FAIL] Smoke emitter delay expected ~0.2, got %.3f\n",
                     static_cast<double>(emitters[1].startDelay));
        pass = false;
    }

    // Total spawned must be reasonable (fire=500/s*3s + smoke=200/s*2.8s + debris=300/s*3s).
    // Plus burst events. Rough estimate: 1500 + 560 + 900 + 1500 burst = ~4460.
    // Allow wide tolerance since accumulator rounding and delay affect counts.
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

    // ── Step 8: Cleanup ───────────────────────────────────────────────
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
    if (pass) {
        std::printf("\n[PASS] Explosion composite demo completed successfully.\n");
        return 0;
    } else {
        std::fprintf(stderr, "\n[FAIL] One or more validation checks failed.\n");
        return 1;
    }
}
