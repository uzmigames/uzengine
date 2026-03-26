/// UzEngine VfxEmitterComponent — ECS data for GPU particle emitters.
///
/// Stores the configuration for a single VFX particle emitter. The SceneRenderer
/// gathers all VfxEmitterComponent entities during frame setup, creates/destroys
/// ParticleEmitter runtime instances as needed, and registers them with the
/// VFXSystem for per-frame simulation and rendering.
///
/// # Asset-Driven Workflow
/// The `assetPath` field points to a `.uzfx` file that declaratively defines
/// the module stack (spawn, update, turbulence, decay, render, etc.). The
/// engine loads and parses this asset on first use, constructing the
/// ParticleEmitter + ModuleCompositor internally.
///
/// # Usage
/// ```cpp
/// auto entity = world.CreateEntity();
/// auto& emitter = world.AddComponent<VfxEmitterComponent>(entity);
/// emitter.assetPath = "effects/fire.uzfx";
/// emitter.emitting = true;
/// ```

#pragma once

#include "vector3.h"

#include <cstdint>
#include <string>

namespace uz {

/// GPU particle emitter component.
///
/// Attached to scene entities to spawn VFX particle effects at the entity's
/// world position. The engine manages the underlying ParticleEmitter lifetime.
struct VfxEmitterComponent {
    // ── Asset Reference ──────────────────────────────────────────────────

    /// Path to the `.uzfx` emitter definition asset (loaded via VFS).
    /// Empty string means no asset — emitter uses inline parameters below.
    std::string assetPath;

    // ── Inline Parameters (used when assetPath is empty) ─────────────────

    /// Maximum particles this emitter can allocate from the global budget.
    uint32_t maxParticles = 10000;

    /// Particles spawned per second when `emitting` is true.
    float emissionRate = 100.0f;

    /// Particle lifetime in seconds.
    float lifetime = 2.0f;

    /// Gravity acceleration applied to all particles (cm/s^2, Z-up).
    Vec3 gravity = {0.0f, 0.0f, -981.0f};

    /// Velocity damping per frame (0 = full stop, 1 = no damping).
    float damping = 0.98f;

    /// Initial spawn velocity in local space (cm/s).
    Vec3 spawnVelocity = {0.0f, 0.0f, 200.0f};

    // ── Runtime State ────────────────────────────────────────────────────

    /// Whether the emitter is actively spawning particles.
    bool emitting = true;

    /// Whether the emitter should loop (restart after all particles die).
    bool looping = true;

    // ── Internal (set by engine, do not modify) ──────────────────────────

    /// Runtime emitter handle index (-1 = not yet created).
    /// Used by SceneRenderer to track the ParticleEmitter instance.
    int32_t runtimeEmitterIdx = -1;
};

} // namespace uz
