#pragma once

/// Bridge between VFXEmitterConfig (demo/editor POD struct) and ParticleEmitter
/// (engine ECS component).
///
/// # Purpose
/// VFXEmitterConfig is the flat, demo-facing description of an emitter effect.
/// It carries every knob an artist would tune: spawn shapes, curl noise,
/// vortex forces, collision, orbit, etc.
///
/// ParticleEmitter is the ECS component the engine VFXSystem simulates.
/// It holds only the fields that the engine's compute shaders and budget system
/// understand today.  The two structs are intentionally separate so the demo
/// can evolve independently of the engine component.
///
/// This bridge converts between them so that:
/// - Demos can be ported to the full ECS pipeline by calling ApplyConfigToEmitter.
/// - Preset files can be re-exported from ECS entities via ExtractConfigFromEmitter.
///
/// # Fields Not Bridged (demo-only, require ParticleEmitter extension)
/// The following VFXEmitterConfig fields have no current equivalent in
/// ParticleEmitter and are documented here as a gap registry for future tasks:
///
/// ## Spawn Shape
///   - spawnRadius, spawnShape, spawnOnSurface, shapeParam1/2/3
///   → Requires: SpawnShapeParams struct in ParticleEmitter.
///
/// ## Size / Color Over Lifetime
///   - initialSize, sizeOverLifeStart/Peak/End/PeakTime
///   - initialColor[4], colorLifeStart/End (R/G/B/A)
///   → Requires: SizeLifetimeCurve + ColorLifetimeCurve fields.
///
/// ## Billboard Rendering
///   - softFadeDistance, emissiveIntensity, facingMode, customFacing[XYZ]
///   - stretchFactor, blendMode, cameraOffset, pivotOffsetX/Y
///   - screenSpaceSizeEnabled, screenSpacePixelSize
///   → Requires: BillboardParams struct in ParticleEmitter.
///
/// ## Curl Noise Force
///   - curlStrength, curlFrequency, curlSpeed
///   → Requires: CurlNoiseParams struct in ParticleEmitter.
///
/// ## Vortex Force
///   - vortexAxisOrigin[3], vortexAxisDir[3]
///   - vortexTangentialStrength, vortexRadialStrength, vortexMaxRadius
///   → Requires: VortexParams struct in ParticleEmitter.
///
/// ## Wind
///   - windDir[3], windStrength
///   → Requires: WindOverrideParams struct in ParticleEmitter
///     (or integration with the engine's global WindZone system).
///
/// ## Lighting
///   - litMode
///   - particleLightEnabled, particleLightRadius, particleLightIntensity
///   - particleLightFlicker, particleLightFlickerSpeed
///   → Requires: ParticleLightParams struct in ParticleEmitter.
///
/// ## Collision
///   - groundCollision, restitution, collisionResponse, collisionFriction
///   - terrainCollisionEnabled, terrainAmplitude, terrainFrequency, terrainBaseHeight
///   - depthCollisionEnabled
///   - collisionSpawnEnabled, collisionSpawnCount
///   → Requires: CollisionParams struct in ParticleEmitter.
///
/// ## Orbit
///   - orbitEnabled, orbitRadius, orbitSpeed, orbitAxis[XYZ]
///   → Requires: OrbitParams struct in ParticleEmitter.
///
/// ## Drag / Acceleration
///   - dragCoefficient, accelX/Y/Z
///   → Requires: ForceParams struct in ParticleEmitter.
///
/// ## Size by Speed / Color by Speed
///   - sizeBySpeedEnabled, sizeBySpeedMin/Max, speedRangeMax, colorBySpeedEnabled
///   → Requires: SpeedResponseParams struct in ParticleEmitter.
///
/// ## Death Spawn
///   - deathSpawnEnabled, deathSpawnCount, deathSpawnSpread, deathSpawnSpeedScale
///   → Requires: DeathSpawnParams struct in ParticleEmitter.
///
/// ## Rotation
///   - rotationRate, initialRotationMin/Max
///   - rotationOverLifeStart/End
///   → Requires: RotationParams struct in ParticleEmitter.
///
/// ## Velocity Inheritance
///   - velocityInheritance, emitterVelocity[3]
///   → Requires: VelocityInheritanceParams struct in ParticleEmitter.
///
/// ## Dynamic Material Params
///   - dynamicParam0/1/2/3
///   → Requires: DynamicMaterialParams in ParticleEmitter.
///
/// ## Vector Field
///   - vectorFieldEnabled, vectorFieldStrength/Tightness/Scale
///   → Requires: VectorFieldParams struct in ParticleEmitter.
///
/// ## Point Force
///   - pointForceEnabled, pointForce[XYZ], pointForceStrength/Radius
///   → Requires: PointForceParams struct in ParticleEmitter.
///
/// ## Sort
///   - sortEnabled
///   → Requires: sortEnabled bool in ParticleEmitter (currently render-pass-controlled).
///
/// ## Spawn Rate Curves
///   - spawnRateRampUpTime, spawnRateRampDownTime
///   → Requires: SpawnRateCurveParams struct in ParticleEmitter.
///
/// ## Emitter Lifecycle
///   - emitterDuration, loopCount
///   → Requires: EmitterLifecycleParams struct in ParticleEmitter.
///
/// ## Deterministic Seed
///   - emitterSeed
///   → Requires: uint32_t emitterSeed field in ParticleEmitter.
///
/// ## Fixed Timestep
///   - fixedTimestep
///   → Requires: float fixedTimestep field in ParticleEmitter.
///
/// ## Local Space
///   - localSpaceEnabled
///   → Requires: bool localSpaceEnabled in ParticleEmitter.
///
/// ## Misc
///   - meshParticleEnabled
///   - deathDecalEnabled, renderTargetFeedbackEnabled
///   → Requires: feature-specific flags in ParticleEmitter.
///
/// # Thread Safety
/// Not thread-safe. Call from the main thread only.

#include "engine/vfx/components/particle_emitter.h"

namespace uz {

// Forward declaration — defined in demos/vfx_viewport, not in the engine.
struct VFXEmitterConfig;

// ─────────────────────────────────────────────────────────────────────────────
// ApplyConfigToEmitter
// ─────────────────────────────────────────────────────────────────────────────

/// Copies the engine-bridgeable fields from a VFXEmitterConfig into a
/// ParticleEmitter component.
///
/// Only the fields that have a current ParticleEmitter equivalent are
/// transferred.  All demo-only fields (curl noise, vortex, collision, etc.)
/// are silently ignored — see the gap registry in this header for details.
///
/// This does NOT call ParticleEmitter::Initialize().  The caller must allocate
/// GPU buffers separately after applying the config.
///
/// # Parameters
/// - `emitter`:   Target ECS component to fill. Must be default-initialised or
///                previously shut down (GPU buffers must not be live yet).
/// - `config`:    Source demo config struct. Lifetime must exceed this call.
/// - `spawnRate`: Particle spawn rate (particles/second) from the VFXPreset or
///                UI slider.  Stored in emitter.emissionRate.
void ApplyConfigToEmitter(ParticleEmitter& emitter,
                          const VFXEmitterConfig& config,
                          float spawnRate) noexcept;

// ─────────────────────────────────────────────────────────────────────────────
// ExtractConfigFromEmitter
// ─────────────────────────────────────────────────────────────────────────────

/// Copies the engine-understood fields from a ParticleEmitter back into a
/// VFXEmitterConfig.
///
/// Fields with no ParticleEmitter equivalent are left at their VFXEmitterConfig
/// defaults (the caller is responsible for default-constructing `config` before
/// calling this).
///
/// # Parameters
/// - `config`:    Target demo config struct to fill.  Should be
///                default-constructed before this call to avoid stale data in
///                the demo-only fields.
/// - `spawnRate`: Receives the emitter's current emissionRate.
/// - `emitter`:   Source ECS component to read from.
void ExtractConfigFromEmitter(VFXEmitterConfig& config,
                              float& spawnRate,
                              const ParticleEmitter& emitter) noexcept;

} // namespace uz
