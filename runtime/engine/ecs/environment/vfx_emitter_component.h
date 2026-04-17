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
#include "vector4.h"

#include <cstdint>
#include <string>

namespace uz {

/// GPU particle emitter component.
///
/// Attached to scene entities to spawn VFX particle effects at the entity's
/// world position. The engine manages the underlying ParticleEmitter lifetime.
/// All configurable ParticleEmitter fields are mirrored here so presets and
/// Lua scripts can fully configure emitters through the ECS.
struct VfxEmitterComponent {
    // ── Asset Reference ──────────────────────────────────────────────────

    /// Path to the `.uzfx` emitter definition asset (loaded via VFS).
    /// Empty string means no asset — emitter uses inline parameters below.
    std::string assetPath;

    // ── Core Emission ────────────────────────────────────────────────────

    uint32_t maxParticles   = 10000;  ///< Maximum particle pool capacity.
    float emissionRate      = 100.0f; ///< Particles spawned per second (particles/s).
    float lifetime          = 2.0f;   ///< Base particle lifespan (seconds).
    float lifetimeVariation = 0.0f;   ///< Random lifespan subtraction (seconds).
    Vec3 spawnVelocity      = {0.0f, 0.0f, 200.0f}; ///< Initial velocity (cm/s, Z-up).
    float velocityVariation = 1.0f;   ///< Random per-component velocity spread (cm/s).
    float spreadAngle       = 30.0f;  ///< Cone half-angle (degrees). 0=laser, 90=hemisphere.

    // ── Sprite Texture ──────────────────────────────────────────────────

    /// Path to sprite sheet (PNG/TGA/JPEG). Empty = procedural circle.
    std::string spriteTexturePath;

    /// Flipbook grid dimensions (auto-detected from filename if using VFXMaterial).
    uint32_t flipbookRows        = 1;  ///< Rows in sprite sheet. 1 = no flipbook.
    uint32_t flipbookCols        = 1;  ///< Columns in sprite sheet. 1 = no flipbook.
    uint32_t flipbookTotalFrames = 1;  ///< Total valid frames in the sheet.

    /// Flipbook playback rate. 0 = lifetime-driven (one full cycle per lifetime, Niagara default).
    /// >0 = fixed FPS (may cycle multiple times or clip within lifetime).
    float flipbookFPS = 0.0f;

    /// Sprite texture channel mask: 0=RGBA, 1=R, 2=G, 3=B, 4=A.
    uint32_t spriteChannelMask = 0;

    // ── Spawn Shape ──────────────────────────────────────────────────────

    uint32_t spawnShape     = 0;      ///< 0=Point, 1=Sphere, 2=Box, 3=Ring, 4=Cone, 5=Cylinder.
    float spawnRadius       = 0.0f;   ///< Shape radius (cm). 0=point spawn.

    // ── Initial Appearance ───────────────────────────────────────────────

    float initialSize       = 10.0f;  ///< Billboard diameter (cm).
    Vec4 initialColor       = {1.0f, 0.9f, 0.6f, 1.0f}; ///< RGBA (linear).
    float emissiveIntensity = 0.0f;   ///< Additive glow multiplier. 0=disabled.
    uint32_t litMode        = 0;      ///< 0=Unlit, 1=Lit (NdotL directional + ambient).
    float softParticleFadeRange = 100.0f; ///< Depth fade distance (cm). 0=disabled.

    // ── Billboard ────────────────────────────────────────────────────────

    uint32_t facingMode     = 0;      ///< 0=FaceCamera, 1=VelocityAligned, 2=CameraPlane.
    float stretchFactor     = 0.0f;   ///< Velocity stretch (use /100 for cm-scale speeds).
    uint32_t blendMode      = 0;      ///< 0=AlphaBlend, 1=Additive, 2=PremulAlpha.
    float cameraOffset      = 0.0f;   ///< Push/pull along camera forward (cm).
    float pivotOffsetX      = 0.0f;   ///< Sprite pivot X [-1,1]. 0=center.
    float pivotOffsetY      = 0.0f;   ///< Sprite pivot Y [-1,1]. 0=center.

    // ── Size Over Lifetime ───────────────────────────────────────────────

    float sizeOverLifeStart    = 1.0f;  ///< Size multiplier at birth.
    float sizeOverLifePeak     = 1.0f;  ///< Size multiplier at peak time.
    float sizeOverLifeEnd      = 0.0f;  ///< Size multiplier at death.
    float sizeOverLifePeakTime = 0.3f;  ///< Normalized age [0,1] where peak occurs.

    // ── Color Over Lifetime ──────────────────────────────────────────────

    float colorLifeStartR = 1.0f, colorLifeStartG = 1.0f; ///< Start color RG.
    float colorLifeStartB = 1.0f, colorLifeStartA = 1.0f; ///< Start color BA.
    float colorLifeEndR   = 1.0f, colorLifeEndG   = 1.0f; ///< End color RG.
    float colorLifeEndB   = 1.0f, colorLifeEndA   = 0.0f; ///< End color BA (fade out).

    // ── Size/Color by Speed ──────────────────────────────────────────────

    uint32_t sizeBySpeedEnabled = 0;    ///< 0=off, 1=map speed to size multiplier.
    float sizeBySpeedMin   = 0.5f;      ///< Size multiplier at zero speed.
    float sizeBySpeedMax   = 2.0f;      ///< Size multiplier at speedRangeMax.
    float speedRangeMax    = 1000.0f;   ///< Speed (cm/s) that maps to t=1.
    uint32_t colorBySpeedEnabled = 0;   ///< 0=off, 1=speed replaces age for color lerp.
    uint32_t screenSpaceSizeEnabled = 0;///< 0=world-space, 1=constant pixel size.
    float screenSpacePixelSize = 8.0f;  ///< Target diameter in pixels (screen-space mode).

    // ── Simulation Forces ────────────────────────────────────────────────

    Vec3 gravity            = {0.0f, 0.0f, -981.0f}; ///< Gravity acceleration (cm/s², Z-up).
    float damping           = 0.98f;  ///< Velocity damping coefficient. 0=heavy, 1=none.
    float curlStrength      = 0.0f;   ///< Curl noise force multiplier. 0=disabled.
    float curlFrequency     = 0.5f;   ///< Curl noise spatial frequency.
    float curlSpeed         = 0.2f;   ///< Curl noise animation speed.
    float dragCoefficient   = 0.0f;   ///< Quadratic drag. 0=disabled.
    float windDirX = 1.0f, windDirY = 0.0f, windDirZ = 0.0f; ///< Wind direction (normalized).
    float windStrength      = 0.0f;   ///< Wind speed (cm/s). 0=disabled.
    float accelX = 0.0f, accelY = 0.0f, accelZ = 0.0f; ///< Per-axis acceleration (cm/s²).

    // ── Vortex ───────────────────────────────────────────────────────────

    float vortexTangentialStrength = 0.0f; ///< Swirl force (cm/s²). 0=disabled.
    float vortexRadialStrength     = 0.0f; ///< Radial force (cm/s²). >0=inward.
    float vortexMaxRadius          = 500.0f; ///< Cutoff radius (cm).

    // ── Orbit ────────────────────────────────────────────────────────────

    uint32_t orbitEnabled   = 0;      ///< 0=disabled, 1=orbit around emitter origin.
    float orbitRadius       = 0.0f;   ///< Orbit circle radius (cm).
    float orbitSpeed        = 0.0f;   ///< Angular speed (rad/s).

    // ── Collision ────────────────────────────────────────────────────────

    uint32_t groundCollision    = 0;    ///< 0=disabled, 1=bounce off Z=0 ground plane.
    float restitution           = 0.3f; ///< Bounce coefficient [0,1]. 0=inelastic.
    uint32_t collisionResponse  = 0;    ///< 0=Bounce, 1=Stick, 2=Kill.
    float collisionFriction     = 0.8f; ///< Friction coefficient [0,1].

    // ── Rotation ─────────────────────────────────────────────────────────

    float rotationRate              = 0.0f; ///< Continuous spin speed (rad/s).
    float rotationOverLifeStart     = 0.0f; ///< Rotation rate at birth (rad/s).
    float rotationOverLifeEnd       = 0.0f; ///< Rotation rate at death (rad/s).

    // ── Particle Light ────────────────────────────────────────────────────

    uint32_t particleLightEnabled     = 0;     ///< 0=off, 1=inject point light at emitter pos.
    float particleLightRadius         = 500.0f;///< Point light radius (cm).
    float particleLightIntensity      = 2.0f;  ///< Point light intensity multiplier.
    float particleLightFlicker        = 0.0f;  ///< Flicker amplitude [0,1]. 0=steady.
    float particleLightFlickerSpeed   = 5.0f;  ///< Flicker frequency (Hz).

    // ── Lifecycle ────────────────────────────────────────────────────────

    float emitterDuration   = 0.0f;   ///< Auto-disable after N seconds. 0=infinite.
    uint32_t loopCount      = 0;      ///< Restart count before stopping. 0=infinite.
    float startDelay        = 0.0f;   ///< Delay before first spawn (seconds).

    // ── Runtime State ────────────────────────────────────────────────────

    bool emitting = true;
    bool looping  = true;

    /// Set to true to request a full GPU reset (kill all particles, re-upload
    /// free list). Cleared automatically by SyncVfxEmitters after processing.
    bool restartRequested = false;

    // ── Culling state (phase 10.5.0) ─────────────────────────────────────

    /// Mirrors `uz::EmitterCullState` — kept as POD fields here to avoid
    /// pulling the full cull header into the component interface. The
    /// scene renderer copies these to a real `EmitterCullState`, updates,
    /// and copies back. 2-frame hysteresis across `culledPrevFrame`.
    ///
    /// @ref engine/vfx/lod/vfx_frustum_cull.h
    /// @ref docs/engine/25-vfx/42-frustum-cull-integration.md
    bool     cullCulledThisFrame   = false;
    bool     cullCulledPrevFrame   = false;
    uint32_t cullFramesSinceCulled = 0;

    // ── HZB occlusion state (phase 9.14.0) ──────────────────────────────

    /// Per-emitter HZB occlusion hysteresis. Mirrors
    /// `VfxOcclusionHysteresis` as POD fields — the scene renderer
    /// constructs a transient `VfxOcclusionHysteresis` from these,
    /// calls `Update(rawVote)`, and writes back.
    ///
    /// @ref engine/vfx/lod/vfx_hzb_occlusion.h
    /// @ref docs/engine/25-vfx/37-hzb-occlusion.md
    bool     hzbOccluded     = false;  ///< Committed occlusion state.
    uint32_t hzbDissentCount = 0;      ///< Frames since last state flip vote.

    // ── Internal (set by engine, do not modify) ──────────────────────────

    int32_t runtimeEmitterIdx = -1;
};

} // namespace uz
