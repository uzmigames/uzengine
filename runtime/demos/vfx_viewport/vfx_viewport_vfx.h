/// VFX Viewport Demo — GPU Particle System
///
/// Manages GPU-driven particle buffers, compute pipelines (spawn, update,
/// indirect args, sort), and billboard rendering. Uses engine's ShaderManager
/// for shader compilation and RHI abstraction for all GPU resources.
///
/// Particle data is stored in Structure-of-Arrays (SoA) format with ping-pong
/// double buffering: each frame reads from set A and writes to set B (or vice
/// versa). This avoids read-write hazards without explicit synchronization.

#pragma once

#include "rhi_types.h"
#include "vector3.h"
#include "mat4.h"
#include "engine/vfx/curves/vfx_color_ramp.h"
#include "engine/vfx/modules/wind_zone.h"
#include "engine/vfx/modules/kill_volume.h"

#include <cstdint>

namespace uz {

class IRHIDevice;
class IRHICommandList;
class ShaderManager;
class PipelineLibrary;
class CBRingAllocator;
class UploadContext;

// ─────────────────────────────────────────────────────────────────────────────
// Death Event — GPU sub-emitter spawn trigger
// ─────────────────────────────────────────────────────────────────────────────

/// Single death event written by the update CS when a particle expires.
/// Packed as three float4 for efficient GPU StructuredBuffer layout.
///
/// @ref Shopf 2008 "GPU Particles" — death event buffer for sub-emitter spawning
struct DeathEvent {
    float position[3]; ///< World-space position at death (cm).
    float _pad0;       ///< Alignment padding.
    float velocity[3]; ///< Velocity at death (cm/s) — seed for child spawn direction.
    float _pad1;       ///< Alignment padding.
    float color[4];    ///< RGBA color at death — inherited by child particles.
};
static_assert(sizeof(DeathEvent) == 48, "DeathEvent must be 48 bytes (3 x float4)");

/// Hard cap on death events per emitter per frame. 4096 events x 48 bytes = 192 KB GPU.
/// Chosen to cover worst-case simultaneous particle deaths without overflow.
///
/// @ref Shopf 2008 "GPU Particles" — fixed-size event ring buffer pattern
static constexpr uint32_t VFX_MAX_DEATH_EVENTS = 4096u;

// ─────────────────────────────────────────────────────────────────────────────
// VFX Emitter Configuration
// ─────────────────────────────────────────────────────────────────────────────

/// CPU-side emitter configuration. Controls particle spawn behavior and
/// billboard rendering parameters (flipbook, soft fade, emissive).
struct VFXEmitterConfig {
    float position[3]       = {0.0f, 1.5f, 0.0f};
    float spawnRadius       = 0.1f;
    float spawnVelocity[3]  = {0.0f, 3.0f, 0.0f};
    float velocityVariation = 0.5f;
    float spreadAngle       = 0.3f;
    float minLifetime       = 1.0f;
    float maxLifetime        = 3.0f;
    float initialSize       = 0.05f;
    float initialColor[4]   = {1.0f, 0.6f, 0.2f, 0.8f};
    float gravity[3]        = {0.0f, -9.81f, 0.0f};
    float damping           = 0.1f;
    // Billboard rendering parameters
    float    softFadeDistance  = 0.3f;  ///< World-space soft particle fade distance.
    float    emissiveIntensity = 0.0f; ///< Additive emissive multiplier.
    // Size over lifetime (analytical piecewise-linear curve)
    float    sizeOverLifeStart    = 1.0f;  ///< Size multiplier at birth (t=0).
    float    sizeOverLifePeak     = 1.0f;  ///< Size multiplier at peak time.
    float    sizeOverLifeEnd      = 0.0f;  ///< Size multiplier at death (t=1).
    float    sizeOverLifePeakTime = 0.3f;  ///< Normalized age [0,1] where peak occurs.
    // Color over lifetime (linear interpolation start→end)
    float    colorLifeStartR = 1.0f;  ///< Start color red.
    float    colorLifeStartG = 1.0f;  ///< Start color green.
    float    colorLifeStartB = 1.0f;  ///< Start color blue.
    float    colorLifeStartA = 1.0f;  ///< Start color alpha.
    float    colorLifeEndR   = 1.0f;  ///< End color red.
    float    colorLifeEndG   = 1.0f;  ///< End color green.
    float    colorLifeEndB   = 1.0f;  ///< End color blue.
    float    colorLifeEndA   = 0.0f;  ///< End color alpha (fade to transparent).
    // Billboard facing mode and stretching
    /// @ref Latta 2004 "Inside Geometry Instancing" — camera-position billboard
    /// @ref Valve 2006 "Rendering in Left 4 Dead" — axis-aligned billboard
    uint32_t facingMode      = 0;     ///< 0=FaceCamera, 1=VelocityAligned, 2=FaceCameraPlane, 3=CustomVector, 4=FaceCameraPosition.
    float    customFacingX   = 0.0f;  ///< Custom facing direction X (mode 3 only).
    float    customFacingY   = 0.0f;  ///< Custom facing direction Y (mode 3 only).
    float    customFacingZ   = 1.0f;  ///< Custom facing direction Z (mode 3 only). Default: world Z-up.
    float    stretchFactor   = 0.0f;  ///< Velocity stretch multiplier. 0=no stretch, >0=elongate along velocity.
    // Spawn shape parameters
    uint32_t spawnShape      = 0;     ///< 0=Point, 1=Sphere, 2=Box, 3=Ring, 4=Cone, 5=Cylinder, 6=Disc, 7=Line.
    uint32_t spawnOnSurface  = 0;     ///< 0=volume fill, 1=surface only.
    float    shapeParam1     = 0.0f;  ///< Ring: ring width, Cone: half-angle (rad), Cylinder: height, Line: length.
    float    shapeParam2     = 0.0f;  ///< Cone: height, Box: (unused).
    float    shapeParam3     = 0.0f;  ///< Reserved for future shapes.
    // Material blend mode
    uint32_t blendMode       = 2;     ///< 0=AlphaBlend (SrcAlpha/InvSrcAlpha), 1=Additive (One/One), 2=PremulAlpha (One/InvSrcAlpha).
    // Curl noise force parameters
    /// @ref Bridson 2007 "Curl-Noise for Procedural Fluid Flow"
    float    curlStrength    = 0.0f;  ///< Curl force multiplier. 0 = disabled. Typical smoke: 1.5-3.0.
    float    curlFrequency   = 0.5f;  ///< Spatial scale. Higher = tighter swirls.
    float    curlSpeed       = 0.2f;  ///< Animation scroll speed (world units/second).
    // ── Vortex force parameters ──────────────────────────────────────────────
    /// @ref Stomakhin 2013 "A Material Point Method for Snow Simulation" App.A — vortex field
    float    vortexAxisOrigin[3]      = {0.0f, 0.0f, 0.0f};  ///< World-space origin of the vortex axis.
    float    vortexAxisDir[3]         = {0.0f, 1.0f, 0.0f};  ///< World-space axis direction (normalized). Default: +Y up.
    float    vortexTangentialStrength = 0.0f;  ///< Swirl force (m/s^2). 0 = disabled. Positive = CCW viewed from axis tip.
    float    vortexRadialStrength     = 0.0f;  ///< Radial force (m/s^2). >0 pulls inward, <0 pushes outward.
    float    vortexMaxRadius          = 5.0f;  ///< World-space cutoff radius. Particles beyond receive no force.
    // ── Wind zone parameters (per-emitter simple wind override) ─────────────
    /// @ref Stam 1999 "Stable Fluids" — global wind field
    float    windDir[3]               = {1.0f, 0.0f, 0.0f};  ///< Normalized wind direction.
    float    windStrength             = 0.0f;  ///< Base wind speed (m/s). 0 = disabled.
    // ── Lit particle mode ────────────────────────────────────────────────
    /// @ref Pharr 2023 "Physically Based Rendering" Ch.12 — Lambertian diffuse NdotL
    uint32_t litMode                  = 0;     ///< 0=Unlit (default), 1=Lit (NdotL directional + ambient).
    // ── Ground plane collision ───────────────────────────────────────────────
    /// @ref Müller 2006 "Position Based Dynamics" — restitution + friction coefficients
    /// @ref Bender 2014 "Position-Based Simulation Methods in Computer Graphics" — friction model
    uint32_t groundCollision          = 0;     ///< 0=disabled, 1=bounce off Z=0 ground plane.
    float    restitution              = 0.3f;  ///< Bounce coefficient [0,1]. 0=inelastic, 1=elastic.
    uint32_t collisionResponse        = 0;     ///< 0=Bounce, 1=Stick (stop on surface), 2=Kill (instant death).
    float    collisionFriction        = 0.8f;  ///< Friction coefficient [0,1] applied to tangential (XY) velocity on contact.
    // ── Orbit around point ───────────────────────────────────────────────────
    /// @ref Christensen 2018 "Fibonacci Sphere Sampling" — golden angle for uniform spread
    uint32_t orbitEnabled             = 0;     ///< 0=disabled, 1=particles orbit around emitter origin.
    float    orbitRadius              = 0.0f;  ///< Orbit circle radius in world units.
    float    orbitSpeed               = 0.0f;  ///< Angular speed in radians per second.
    float    orbitAxisX               = 0.0f;  ///< Orbit plane normal X. Default axis: Z-up (XY plane).
    float    orbitAxisY               = 0.0f;  ///< Orbit plane normal Y.
    float    orbitAxisZ               = 1.0f;  ///< Orbit plane normal Z.
    // ── Quadratic drag (air resistance) ─────────────────────────────────────
    /// @ref Witkin & Baraff 2001 "Physically Based Modeling" — quadratic drag F=-k|v|^2*dir
    float    dragCoefficient          = 0.0f;  ///< Quadratic drag coefficient. 0=disabled. Higher=faster deceleration.
    // ── Camera offset & pivot offset (billboard VS) ─────────────────────────
    /// @ref Latta 2004 "Inside Geometry Instancing" — camera offset for depth layering
    float    cameraOffset             = 0.0f;  ///< Push/pull along camera forward (world units). 0=disabled.
    float    pivotOffsetX             = 0.0f;  ///< Sprite-local X pivot shift [-1,1]. 0=center.
    float    pivotOffsetY             = 0.0f;  ///< Sprite-local Y pivot shift [-1,1]. 0=center. -1=bottom anchor.
    // ── Size by speed (billboard VS) ─────────────────────────────────────────
    uint32_t sizeBySpeedEnabled       = 0;     ///< 0=off, 1=on. Maps speed to size multiplier.
    float    sizeBySpeedMin           = 0.5f;  ///< Size multiplier at zero speed.
    float    sizeBySpeedMax           = 2.0f;  ///< Size multiplier at speedRangeMax.
    float    speedRangeMax            = 10.0f; ///< Speed that maps to t=1 for size-by-speed.
    // ── Color by speed (billboard VS→PS) ────────────────────────────────────
    /// When enabled, the normalized speed factor (speed / speedRangeMax) replaces
    /// normalizedAge as the parameter for the color-over-lifetime lerp in the PS.
    /// This reuses the existing colorLifeStart/colorLifeEnd fields without adding
    /// extra float4 color parameters.
    uint32_t colorBySpeedEnabled      = 0;     ///< 0=off, 1=on. PS uses speed instead of age for color lerp.
    // ── Particle light emission ─────────────────────────────────────────────
    /// When enabled, the emitter injects a dynamic point light at the emitter
    /// position using the emitter's initial color and emissive intensity.
    /// The light is written into PerFrameCB point light slots (max 3 total).
    uint32_t particleLightEnabled     = 0;     ///< 0=off, 1=emit a point light at emitter position.
    float    particleLightRadius      = 500.0f; ///< Point light radius in world units (cm).
    float    particleLightIntensity   = 2.0f;  ///< Point light intensity multiplier.
    // ── Death spawn (sub-emitter on particle death) ───────────────────────────
    /// When enabled, dying particles write a DeathEvent to the GPU buffer.
    /// A secondary spawn pass reads these events and emits child particles.
    ///
    /// @ref Shopf 2008 "GPU Particles" — death event driven sub-emitter spawn
    uint32_t deathSpawnEnabled        = 0;     ///< 0=off, 1=spawn children at each particle death. Default: off.
    uint32_t deathSpawnCount          = 4;     ///< Children emitted per dead particle [1..32]. Default: 4.
    float    deathSpawnSpread         = 0.5f;  ///< Cone half-angle (rad) for child velocity spread [0, pi]. Default: 0.5.
    float    deathSpawnSpeedScale     = 0.3f;  ///< Child speed = parent death speed * scale [0, 2]. Default: 0.3.
    // ── Rotation rate (continuous spin) ──────────────────────────────────────
    float    rotationRate             = 0.0f;  ///< Rotation speed in radians/sec. 0 = no spin.
    // ── Initial rotation range ───────────────────────────────────────────────
    float    initialRotationMin       = 0.0f;  ///< Minimum initial rotation (radians). Default: 0.
    float    initialRotationMax       = 6.28318f; ///< Maximum initial rotation (radians). Default: 2*pi.
    // ── Velocity inheritance ─────────────────────────────────────────────────
    float    velocityInheritance      = 0.0f;  ///< Fraction of emitter velocity added to spawn velocity [0, 1]. 0 = none.
    float    emitterVelocity[3]       = {0.0f, 0.0f, 0.0f}; ///< Transient: emitter movement velocity (m/s), set by OnFrame each frame.
    // ── Acceleration force (independent of gravity) ─────────────────────────
    float    accelX                   = 0.0f;  ///< Per-axis acceleration X (m/s^2). 0 = none.
    float    accelY                   = 0.0f;  ///< Per-axis acceleration Y (m/s^2). 0 = none.
    float    accelZ                   = 0.0f;  ///< Per-axis acceleration Z (m/s^2). 0 = none.
    // ── Rotation over lifetime ───────────────────────────────────────────────
    float    rotationOverLifeStart    = 0.0f;  ///< Rotation rate at birth (rad/s). Lerped to end over lifetime.
    float    rotationOverLifeEnd      = 0.0f;  ///< Rotation rate at death (rad/s). Lerped from start over lifetime.
    // ── Screen-space size (billboard VS) ────────────────────────────────────
    /// When enabled the particle always covers a fixed number of pixels on screen
    /// regardless of its world-space distance from the camera.  The VS computes
    /// the world-space size needed to cover screenSpacePixelSize pixels at the
    /// particle's clip-space depth, replacing the normal world-space finalSize.
    ///
    /// @ref Valve 2006 "Rendering in Left 4 Dead" — constant-screen-size sprites
    uint32_t screenSpaceSizeEnabled = 0;     ///< 0=world-space (default), 1=screen-space constant pixel size.
    float    screenSpacePixelSize   = 8.0f;  ///< Target diameter in pixels when screen-space mode is enabled.
    // ── Emitter lifecycle ────────────────────────────────────────────────────
    /// Controls automatic disable/restart of the emitter after a fixed duration.
    /// emitterDuration==0 means the emitter runs forever (default).  loopCount==0
    /// means infinite restarts.  Age and loop counters are tracked in
    /// VFXEmitterInstance so that the config itself remains stateless.
    ///
    /// @ref GDC 2019 "Niagara: Building the Future of FX in Unreal Engine" — emitter lifetime
    float    emitterDuration = 0.0f;  ///< Auto-disable after this many seconds. 0 = infinite (default).
    uint32_t loopCount       = 0;     ///< Restart count before stopping. 0 = infinite (default).
    // ── Mesh particle mode (Task 4.12.0) ────────────────────────────────────
    uint32_t meshParticleEnabled = 0; ///< 0=billboard (default), 1=mesh particles (future).
    // ── Dynamic material parameters (Task 4.14.0) ───────────────────────────
    /// Per-particle float4 that flows spawn→update→VS→PS for shader-driven effects.
    /// X component is auto-set to normalizedAge during update; Y/Z/W user-defined.
    float    dynamicParam0 = 0.0f;    ///< Default dynamic param channel 0 (overwritten to normalizedAge in update).
    float    dynamicParam1 = 0.0f;    ///< Default dynamic param channel 1 (user-defined).
    float    dynamicParam2 = 0.0f;    ///< Default dynamic param channel 2 (user-defined).
    float    dynamicParam3 = 0.0f;    ///< Default dynamic param channel 3 (user-defined).
    // ── Vector field (procedural 3D flow) (Task 4.15.0) ─────────────────
    /// @ref Stam 1999 "Stable Fluids" — vector field advection fundamentals
    /// @ref Bridson 2007 "Fluid Simulation for Computer Graphics" — velocity field sampling
    uint32_t vectorFieldEnabled   = 0;    ///< 0=disabled, 1=procedural vector field active.
    float    vectorFieldStrength  = 0.0f; ///< Blend factor: 0=ignore field, 1=full field influence. Typical: 1-10.
    float    vectorFieldTightness = 0.0f; ///< 0=additive (field adds to velocity), 1=directive (velocity replaced by field).
    float    vectorFieldScale     = 1.0f; ///< Spatial scale of the procedural noise field. Higher=tighter detail.
};

/// Named emitter preset with a spawn rate.
struct VFXPreset {
    const char* name;
    VFXEmitterConfig config;
    float spawnRate;
};

/// Builds a fire gradient: black -> orange -> yellow -> white -> transparent.
/// @ref GDC 2019 "Niagara: Building the Future of FX in Unreal Engine"
VFXColorRamp BuildFireRamp();

/// Builds an ice gradient: dark blue -> cyan -> white -> transparent.
VFXColorRamp BuildIceRamp();

/// Builds a rainbow gradient: red -> yellow -> green -> cyan -> blue.
VFXColorRamp BuildRainbowRamp();

/// Returns the built-in emitter presets array and its count.
///
/// # Arguments
/// * `outCount` — Receives the number of presets.
///
/// # Returns
/// Pointer to the static presets array.
const VFXPreset* GetVFXPresets(int& outCount);

// ─────────────────────────────────────────────────────────────────────────────
// VFX Spawn Constant Buffer (matches particle_spawn_cs.hlsl b0 layout)
// ─────────────────────────────────────────────────────────────────────────────

/// Constant buffer uploaded each frame for the spawn compute shader.
/// Layout matches VFXSpawnConstants in particle_spawn_cs.hlsl (176 bytes).
struct VFXSpawnCB {
    float    emitterPos[3];           float spawnRadius;          // 16 bytes
    float    spawnVelocity[3];        float velocityVariation;    // 16 bytes
    float    spreadAngle;             float minLifetime;
    float    maxLifetime;             float initialSize;          // 16 bytes
    float    initialColor[4];                                     // 16 bytes
    float    dt;                      uint32_t frameNumber;
    uint32_t spawnCount;              uint32_t maxParticles;      // 16 bytes
    // Bindless descriptor indices (SM6.6 path)
    uint32_t freeListSrvIndex;
    uint32_t positionsUavIndex;       uint32_t velocitiesUavIndex;
    uint32_t agesUavIndex;                                        // 16 bytes
    uint32_t lifespansUavIndex;       uint32_t sizesUavIndex;
    uint32_t colorsUavIndex;          uint32_t rotationsUavIndex; // 16 bytes
    uint32_t freeListHeadUavIndex;    uint32_t aliveCountUavIndex;
    uint32_t spawnShape;              uint32_t spawnOnSurface;    // 16 bytes
    float    shapeParam1;             float    shapeParam2;
    float    shapeParam3;             uint32_t _pad0;             // 16 bytes
    // ── Initial rotation range + velocity inheritance ───────────────────────
    float    initialRotationMin;      float    initialRotationMax;
    float    emitterVelocityX;        float    emitterVelocityY;  // 16 bytes (offset 144)
    float    emitterVelocityZ;        float    velocityInheritance;
    uint32_t dynamicParamsUavIndex;   float    defaultDynParam0;  // 16 bytes (offset 160)
    float    defaultDynParam1;        float    defaultDynParam2;
    float    defaultDynParam3;        uint32_t _padSpawnNew1;     // 16 bytes (offset 176)
};
static_assert(sizeof(VFXSpawnCB) == 192, "VFXSpawnCB must be 192 bytes (12 x float4)");

// ─────────────────────────────────────────────────────────────────────────────
// VFX Update Constant Buffer (matches particle_update_cs.hlsl b0 layout)
// ─────────────────────────────────────────────────────────────────────────────

/// Constant buffer for the particle update compute shader.
struct VFXUpdateCB {
    float    gravity[3];              float dt;                   // 16 bytes
    float    damping;                 uint32_t aliveCount;
    uint32_t frameNumber;             uint32_t maxParticles;      // 16 bytes
    // Bindless descriptor indices (SM6.6 path)
    uint32_t readPositionsSrv;        uint32_t readVelocitiesSrv;
    uint32_t readAgesSrv;             uint32_t readLifespansSrv;  // 16 bytes
    uint32_t readSizesSrv;            uint32_t readColorsSrv;
    uint32_t readRotationsSrv;        uint32_t _padRead;          // 16 bytes
    uint32_t writePositionsUav;       uint32_t writeVelocitiesUav;
    uint32_t writeAgesUav;            uint32_t writeLifespansUav; // 16 bytes
    uint32_t writeSizesUav;           uint32_t writeColorsUav;
    uint32_t writeRotationsUav;       uint32_t _padWrite;         // 16 bytes
    uint32_t freeListUavIndex;        uint32_t freeListHeadUavIndex;
    uint32_t aliveCountUavIndex;      uint32_t _padAtomic;        // 16 bytes
    // Curl noise force parameters
    /// @ref Bridson 2007 "Curl-Noise for Procedural Fluid Flow"
    float    curlStrength;            float    curlFrequency;
    float    curlSpeed;               float    totalTime;          // 16 bytes (offset 112)
    // ── Vortex force parameters ──────────────────────────────────────────────
    /// @ref Stomakhin 2013 "A Material Point Method for Snow Simulation" App.A
    float    vortexAxisOriginX;       float vortexAxisOriginY;
    float    vortexAxisOriginZ;       float vortexTangentialStrength; // 16 bytes (offset 128)
    float    vortexAxisDirX;          float vortexAxisDirY;
    float    vortexAxisDirZ;          float vortexRadialStrength;  // 16 bytes (offset 144)
    float    vortexMaxRadius;         uint32_t orbitEnabled;
    float    orbitRadius;             float    orbitSpeed;         // 16 bytes (offset 160) — vortex pads repurposed
    float    orbitAxisX;              float    orbitAxisY;
    float    orbitAxisZ;              float    _orbitAxisPad;      // 16 bytes (offset 176)
    // ── Wind zone GPU buffer ─────────────────────────────────────────────────
    /// @ref Stam 1999 "Stable Fluids" — wind zone accumulation
    uint32_t windZoneSrvIndex;        uint32_t activeWindZoneCount;
    uint32_t _padWind0;               uint32_t _padWind1;          // 16 bytes (offset 192)
    // ── Kill volume GPU buffer ───────────────────────────────────────────────
    /// @ref Ericson 2005 "Real-Time Collision Detection" Ch.4
    uint32_t killVolumeSrvIndex;      uint32_t activeKillVolumeCount;
    // ── Ground plane collision (repurposed from _padKV0/_padKV1) ────────────
    /// @ref Müller 2006 "Position Based Dynamics" — restitution coefficients
    uint32_t groundCollision;         float    restitution;        // 16 bytes (offset 208)
    // ── Quadratic drag + death spawn parameters ───────────────────────────────
    /// @ref Witkin & Baraff 2001 "Physically Based Modeling" — quadratic drag F=-k|v|^2*dir
    float    dragCoefficient;         uint32_t deathSpawnEnabled;
    uint32_t maxDeathEvents;          float    deathSpawnSpread;    // 16 bytes (offset 224)
    // ── Death spawn child parameters ─────────────────────────────────────────
    /// @ref Shopf 2008 "GPU Particles" — death event driven sub-emitter spawn
    float    deathSpawnSpeedScale;    uint32_t deathSpawnCount;
    uint32_t deathEventBufferUavIndex; uint32_t deathEventCountUavIndex; // 16 bytes (offset 240)
    // ── Rotation rate + acceleration + rotation over lifetime ────────────────
    float    rotationRate;            float    rotationOverLifeStart;
    float    rotationOverLifeEnd;     float    accelX;                  // 16 bytes (offset 256)
    float    accelY;                  float    accelZ;
    // ── Enhanced collision response (repurposed from _padNewFeatures0/1) ────
    /// @ref Müller 2006 "Position Based Dynamics" — multi-mode collision response
    /// @ref Bender 2014 "Position-Based Simulation Methods in Computer Graphics" — friction
    uint32_t collisionResponse;       float    collisionFriction;       // 16 bytes (offset 272)
    // ── Dynamic material parameters (Task 4.14.0) ───────────────────────────
    uint32_t readDynParamsSrv;        uint32_t writeDynParamsUav;
    // ── Vector field parameters (Task 4.15.0) ───────────────────────────────
    /// @ref Stam 1999 "Stable Fluids" — vector field advection fundamentals
    /// @ref Bridson 2007 "Fluid Simulation for Computer Graphics" — velocity field sampling
    uint32_t vectorFieldEnabled;      float    vectorFieldStrength;       // 16 bytes (offset 288)
    float    vectorFieldTightness;    float    vectorFieldScale;
    uint32_t _padVF0;                 uint32_t _padVF1;                   // 16 bytes (offset 304)
};
static_assert(sizeof(VFXUpdateCB) == 320, "VFXUpdateCB must be 320 bytes (20 x float4)");

// ─────────────────────────────────────────────────────────────────────────────
// VFX Death Spawn Constant Buffer (matches particle_death_spawn_cs.hlsl b1)
// ─────────────────────────────────────────────────────────────────────────────

/// Constant buffer for the death spawn compute shader.
/// Each thread handles one death event and spawns `deathSpawnCount` children.
///
/// Layout: 64 bytes (4 x float4).
///
/// @ref Shopf 2008 "GPU Particles" — death event driven sub-emitter spawn
struct VFXDeathSpawnCB {
    // Event buffer bindless indices
    uint32_t deathEventBufferSrvIndex;  ///< SRV for StructuredBuffer<DeathEvent>.    0
    uint32_t deathEventCountSrvIndex;   ///< SRV for StructuredBuffer<uint> event counter (GPU-read count). 4
    uint32_t deathSpawnCount;           ///< Children spawned per event [1..32].       8
    float    deathSpawnSpread;          ///< Cone half-angle (rad) for child velocity. 12

    // Child particle parameters
    float    deathSpawnSpeedScale;      ///< child_speed = parent_speed * scale.       16
    uint32_t maxParticles;              ///< Total particle pool capacity.              20
    uint32_t maxDeathEvents;            ///< Hard cap on events per frame (guard).      24
    uint32_t _pad0;                     ///<                                            28

    // Bindless UAV indices (write set — same ping-pong set as update CS)
    uint32_t positionsUavIndex;         ///< RWStructuredBuffer<float4> positions.     32
    uint32_t velocitiesUavIndex;        ///< RWStructuredBuffer<float4> velocities.    36
    uint32_t agesUavIndex;              ///< RWStructuredBuffer<float> ages.            40
    uint32_t lifespansUavIndex;         ///< RWStructuredBuffer<float> lifespans.       44

    uint32_t sizesUavIndex;             ///< RWStructuredBuffer<float> sizes.           48
    uint32_t colorsUavIndex;            ///< RWStructuredBuffer<float4> colors.         52
    uint32_t rotationsUavIndex;         ///< RWStructuredBuffer<float> rotations.       56
    uint32_t freeListUavIndex;          ///< RWStructuredBuffer<uint> freeList.         60

    uint32_t freeListHeadUavIndex;      ///< RWStructuredBuffer<uint> freeListHead.     64
    uint32_t aliveCountUavIndex;        ///< RWStructuredBuffer<uint> aliveCount.       68
    uint32_t frameNumber;               ///< Frame counter (RNG seed).                  72
    uint32_t dynamicParamsUavIndex;     ///< RWStructuredBuffer<float4> dynamicParams (Task 4.14.0). 76

    // Child emitter lifetime + size (color inherited from DeathEvent)
    float    childLifetime;             ///< Lifespan for child particles (seconds).    80
    float    childSize;                 ///< Initial billboard size for children.       84
    float    _pad3;                     ///<                                            88
    float    _pad4;                     ///<                                            92
};
static_assert(sizeof(VFXDeathSpawnCB) == 96, "VFXDeathSpawnCB must be 96 bytes (6 x float4)");

// ─────────────────────────────────────────────────────────────────────────────
// VFX Billboard Root Constants (matches VFXBillboardConstants in b0)
// ─────────────────────────────────────────────────────────────────────────────

/// Unified root constants for billboard VS + PS bindless resource access.
/// Layout matches VFXBillboardConstants in particle_billboard_vs.hlsl / ps.hlsl.
/// Both stages share b0 memory so this single struct covers all indices.
struct VFXBillboardIndicesCB {
    // VS indices (bytes 0-31)
    uint32_t positionBufferIdx;
    uint32_t sizeBufferIdx;
    uint32_t colorBufferIdx;
    uint32_t rotationBufferIdx;
    uint32_t sortedIndicesSrvIndex;
    uint32_t velocityBufferIdx;
    uint32_t agesBufferIdx;
    uint32_t lifespansBufferIdx;
    // PS indices (bytes 32-47)
    uint32_t spriteTextureIdx;
    uint32_t depthBufferIdx;
    uint32_t dynamicParamsBufferIdx = 0; ///< SRV index for StructuredBuffer<float4> dynamic params (Task 4.14.0).
    uint32_t _pad1 = 0;
};
static_assert(sizeof(VFXBillboardIndicesCB) == 48, "VFXBillboardIndicesCB must be 48 bytes (12 x uint32)");

// ─────────────────────────────────────────────────────────────────────────────
// VFX Billboard CB (matches VFXBillboardCB in b2, shared by VS and PS)
// ─────────────────────────────────────────────────────────────────────────────

/// Unified per-emitter constant buffer for billboard VS + PS.
/// Layout matches VFXBillboardCB in both particle_billboard_vs.hlsl and _ps.hlsl.
struct VFXBillboardCB {
    // VS data (emitter parameters, 160 bytes)
    Mat4     invView;                              ///< Inverse view matrix (rows 0,1 = camera right/up).
    float    emitterPosition[3];                   ///< World-space emitter origin.
    float    globalSizeMultiplier = 1.0f;          ///< Size multiplier.
    float    globalColorTint[4] = {1,1,1,1};       ///< Color tint.
    Mat4     prevViewProj;                         ///< Previous frame's view-projection (TAA).
    // PS data (rendering parameters, 48 bytes)
    float    softFadeDistance   = 0.3f;            ///< Soft particle depth fade distance.
    float    alphaErosion      = 0.0f;             ///< Subtractive alpha erosion [0, 1].
    float    emissiveIntensity = 0.0f;             ///< Additive emissive multiplier.
    uint32_t litMode           = 0;               ///< 0=Unlit, 1=Lit (NdotL + ambient). @ref Pharr 2023 Ch.12.
    // ── Camera offset & pivot offset (Task 4.7.0) ─────────────────────────
    /// @ref Latta 2004 "Inside Geometry Instancing" — camera offset for depth layering
    float    cameraOffset         = 0.0f;          ///< Push/pull along camera forward (world units).
    float    pivotOffsetX         = 0.0f;          ///< Sprite-local X pivot shift [-1,1]. 0=center.
    float    pivotOffsetY         = 0.0f;          ///< Sprite-local Y pivot shift [-1,1]. 0=center.
    // NOTE: _camPivotPad repurposed as screenSpacePixelSize — no padding loss (both float, 4 bytes).
    float    screenSpacePixelSize = 8.0f;          ///< Target particle diameter in pixels (screen-space mode).
    // ── Size by speed (Task 4.8.0) ──────────────────────────────────────────
    uint32_t sizeBySpeedEnabled   = 0;             ///< 0=off, 1=on. Maps particle speed to size multiplier.
    float    sizeBySpeedMin       = 0.5f;          ///< Size multiplier at zero speed.
    float    sizeBySpeedMax       = 2.0f;          ///< Size multiplier at speedRangeMax.
    float    speedRangeMax        = 10.0f;         ///< Speed that maps to t=1 for size-by-speed.
    // ── Size over lifetime (analytical piecewise-linear curve, 16 bytes) ──
    float    sizeOverLifeStart    = 1.0f;          ///< Size multiplier at birth (t=0).
    float    sizeOverLifePeak     = 1.0f;          ///< Size multiplier at peak time.
    float    sizeOverLifeEnd      = 0.0f;          ///< Size multiplier at death (t=1).
    float    sizeOverLifePeakTime = 0.3f;          ///< Normalized age [0,1] where peak occurs.
    // ── Color over lifetime (linear interpolation, 32 bytes) ──
    float    colorLifeStart[4]    = {1,1,1,1};     ///< RGBA color at birth.
    float    colorLifeEnd[4]      = {1,1,1,0};     ///< RGBA color at death.
    // ── Billboard facing mode + custom direction (16 bytes) ──
    /// @ref Latta 2004 "Inside Geometry Instancing" — camera-position billboard
    /// @ref Valve 2006 "Rendering in Left 4 Dead" — axis-aligned billboard
    uint32_t facingMode           = 0;              ///< 0=FaceCamera, 1=VelocityAligned, 2=FaceCameraPlane, 3=CustomVector, 4=FaceCameraPosition.
    float    customFacingX        = 0.0f;           ///< Custom facing direction X (facingMode 3 only).
    float    customFacingY        = 0.0f;           ///< Custom facing direction Y (facingMode 3 only).
    float    customFacingZ        = 1.0f;           ///< Custom facing direction Z (facingMode 3 only). Default: world Z-up.
    // ── Billboard stretching + blend (16 bytes) ──
    float    stretchFactor        = 0.0f;           ///< Velocity stretch multiplier. 0=no stretch.
    uint32_t blendModeHint        = 2;              ///< 0=AlphaBlend, 1=Additive, 2=PremulAlpha — tells PS how to output alpha.
    uint32_t colorRampLutSrvIndex = UINT32_MAX;     ///< Bindless SRV index for color ramp 256x1 LUT. UINT32_MAX = legacy lerp.
    // NOTE: _alignPad repurposed as screenSpaceSizeEnabled — no padding loss (both 4 bytes, alignment preserved).
    // ── Screen-space size (Feature: constant pixel coverage) ─────────────────
    /// When non-zero, overrides finalSize so the particle covers exactly
    /// screenSpacePixelSize pixels in diameter at its clip-space depth.
    ///
    /// @ref Valve 2006 "Rendering in Left 4 Dead" — constant-screen-size sprites
    uint32_t screenSpaceSizeEnabled = 0;            ///< 0=world-space (default), 1=screen-space constant pixel size.
    // ── Color by speed (VS→PS, 16 bytes) ──────────────────────────────────
    /// When non-zero, the VS passes a speed-normalized-to-[0,1] factor to the PS
    /// which replaces normalizedAge in the colorLifeStart→End lerp.
    uint32_t colorBySpeedEnabled   = 0;             ///< 0=off, 1=on. PS uses speed instead of age for color lerp.
    uint32_t _cbsPad0              = 0;             ///< Padding for 16-byte alignment.
    uint32_t _cbsPad1              = 0;             ///< Padding for 16-byte alignment.
    uint32_t _cbsPad2              = 0;             ///< Padding for 16-byte alignment.
};
static_assert(sizeof(VFXBillboardCB) == 304, "VFXBillboardCB must be 304 bytes (19 x float4)");

// ─────────────────────────────────────────────────────────────────────────────
// VFX GPU Particle System
// ─────────────────────────────────────────────────────────────────────────────

/// Manages all GPU resources and pipelines for the particle system.
///
/// Particle data uses SoA layout with 8 channels (positions, velocities,
/// ages, lifespans, sizes, colors, rotations, dynamicParams). Each channel has two copies
/// (ping-pong) to allow simultaneous read and write without hazards.
///
/// Bookkeeping buffers: alive count, free list, free list head, indirect args.
/// Sort buffers: sort keys + sort indices for front-to-back depth sorting.
class VFXParticleSystem {
public:
    VFXParticleSystem() = default;
    ~VFXParticleSystem() = default;

    VFXParticleSystem(const VFXParticleSystem&) = delete;
    VFXParticleSystem& operator=(const VFXParticleSystem&) = delete;

    /// Initializes all GPU buffers and compile compute/graphics pipelines.
    ///
    /// # Arguments
    /// * `device` — RHI device for resource creation.
    /// * `shaderMgr` — Shader manager for compute/graphics shader compilation.
    /// * `maxParticles` — Maximum particle pool capacity.
    ///
    /// # Returns
    /// `true` on success, `false` if any resource or pipeline creation fails.
    [[nodiscard]] bool Init(IRHIDevice* device, ShaderManager* shaderMgr,
                            uint32_t maxParticles);

    /// Uploads initial free list data to GPU buffers via staging copy.
    /// Must be called once after Init(), with a command list in recording state.
    /// The command list must be submitted before the first DispatchCompute().
    void UploadInitialFreeList(IRHICommandList* cmd);

    /// Destroys all GPU resources. Safe to call multiple times.
    void Shutdown(IRHIDevice* device);

    /// Uploads wind zone descriptors to the GPU StructuredBuffer for this frame.
    ///
    /// Must be called before DispatchCompute() each frame. Wind zones are consumed
    /// by the update compute shader via AccumulateWindForce().
    ///
    /// # Arguments
    /// * `cmd` — Command list in recording state (for CopyBufferRegion).
    /// * `cbRing` — CB ring allocator used as per-frame staging source.
    /// * `zones` — Array of WindZone descriptors. May be null if count == 0.
    /// * `count` — Number of active zones [0..kMaxWindZones]. Clamped internally.
    ///
    /// @ref Stam 1999 "Stable Fluids" — wind field accumulation
    void UploadWindZones(IRHICommandList* cmd, CBRingAllocator* cbRing,
                         const WindZone* zones, uint32_t count);

    /// Uploads kill volume descriptors to the GPU StructuredBuffer for this frame.
    ///
    /// Must be called before DispatchCompute() each frame. Kill volumes are tested
    /// in the update compute shader via TestAllKillVolumes().
    ///
    /// # Arguments
    /// * `cmd` — Command list in recording state (for CopyBufferRegion).
    /// * `cbRing` — CB ring allocator used as per-frame staging source.
    /// * `volumes` — Array of KillVolume descriptors. May be null if count == 0.
    /// * `count` — Number of active volumes [0..kMaxKillVolumes]. Clamped internally.
    ///
    /// @ref Ericson 2005 "Real-Time Collision Detection" Ch.4
    void UploadKillVolumes(IRHICommandList* cmd, CBRingAllocator* cbRing,
                           const KillVolume* volumes, uint32_t count);

    /// Records spawn, update, indirect args, and sort compute dispatches.
    ///
    /// # Arguments
    /// * `cmdList` — Command list in recording state.
    /// * `cbRing` — CB ring allocator for per-frame constant buffers.
    /// * `config` — Current emitter configuration.
    /// * `spawnCount` — Number of particles to spawn this frame.
    /// * `dt` — Frame delta time in seconds.
    /// * `frameNumber` — Monotonically increasing frame counter.
    /// * `totalTime` — Total elapsed simulation time in seconds (for curl noise animation).
    void DispatchCompute(IRHICommandList* cmdList, CBRingAllocator* cbRing,
                         const VFXEmitterConfig& config, uint32_t spawnCount,
                         float dt, uint32_t frameNumber, float totalTime);

    /// Records billboard draw commands using ExecuteIndirect.
    ///
    /// The particle count comes from the GPU-written indirect args buffer.
    ///
    /// # Arguments
    /// * `cmdList` — Command list in recording state (inside a render pass).
    /// * `cbRing` — CB ring allocator for per-frame constant buffers.
    /// * `perFrameCBGpuAddr` — GPU address of the PerFrameCB for this frame.
    /// * `viewMat` — Current view matrix (for billboard orientation).
    /// * `emitterPos` — World-space emitter position.
    /// * `prevViewProj` — Previous frame's view-projection (for TAA motion vectors).
    /// * `config` — Emitter config (soft fade, emissive params).
    /// * `blendMode` — 0=AlphaBlend, 1=Additive, 2=PremulAlpha. Selects PSO and PS behavior.
    /// * `depthBufferSrvIdx` — SRV index for the scene depth buffer (soft fade).
    void DrawBillboards(IRHICommandList* cmdList, CBRingAllocator* cbRing,
                        uint64_t perFrameCBGpuAddr,
                        const Mat4& viewMat, const Vec3& emitterPos,
                        const Mat4& prevViewProj,
                        const VFXEmitterConfig& config,
                        uint32_t blendMode,
                        uint32_t depthBufferSrvIdx);

    /// Returns the billboard graphics pipeline handle for the given blend mode.
    ///
    /// # Arguments
    /// * `blendMode` — 0=AlphaBlend, 1=Additive, 2=PremulAlpha (default).
    ///
    /// # Returns
    /// Pipeline handle for the requested blend mode.
    [[nodiscard]] RHIPipelineHandle GetBillboardPSOForBlendMode(uint32_t blendMode) const;

    /// Returns the default (PremulAlpha) billboard graphics pipeline handle.
    [[nodiscard]] RHIPipelineHandle GetBillboardPSO() const { return m_billboardPSO; }

    /// Returns the maximum particle count.
    [[nodiscard]] uint32_t GetMaxParticles() const { return m_maxParticles; }

    /// Flips the ping-pong index (call once per frame after compute dispatch).
    void FlipPingPong() { m_pingPong = 1 - m_pingPong; }

    /// Returns the current ping-pong index (0 or 1).
    [[nodiscard]] int GetPingPong() const { return m_pingPong; }

    /// Uploads a color ramp LUT as a 256x1 RGBA16F texture for GPU sampling.
    ///
    /// Bakes the ramp to half-float pixel data, creates a GPU texture via the
    /// RHI device, and uploads via the provided UploadContext. The resulting
    /// SRV index is stored in `m_colorRampSrvIndex` for use in VFXBillboardCB.
    ///
    /// # Arguments
    /// * `upload` — Upload context for staging buffer management.
    /// * `cmd` — Command list in recording state for the copy command.
    /// * `ramp` — Color ramp to bake and upload.
    ///
    /// # Returns
    /// `true` on success, `false` if texture creation or upload fails.
    bool UploadColorRampLUT(UploadContext& upload, IRHICommandList* cmd,
                            const VFXColorRamp& ramp);

    /// Returns the current color ramp LUT SRV index (UINT32_MAX if none).
    [[nodiscard]] uint32_t GetColorRampSrvIndex() const { return m_colorRampSrvIndex; }

    /// Clears the current color ramp LUT (resets to legacy lerp mode).
    void ClearColorRampLUT();

    /// Updates per-emitter frustum cull state from a view-projection matrix.
    ///
    /// Extracts frustum planes via Gribb & Hartmann 2001 direct VP extraction and
    /// tests the emitter's conservative bounding sphere. Requires two consecutive
    /// cull frames before skipping simulation (prevents single-frame edge pop-in).
    ///
    /// # Arguments
    /// * `viewProj` — Current frame view-projection matrix.
    /// * `config`   — Active emitter configuration (position, lifetime, velocity).
    ///
    /// @ref Assarsson 2000 "Optimized View Frustum Culling Algorithms"
    /// @ref Gribb 2001 "Fast Extraction of Viewing Frustum Planes from the WorldView-Projection Matrix"
    void UpdateCullState(const Mat4& viewProj, const VFXEmitterConfig& config);

    /// Returns true if this emitter should be skipped (culled for 2+ consecutive frames).
    [[nodiscard]] bool IsCulled() const { return m_culledThisFrame && m_culledPrevFrame; }

    /// Returns number of frames since the emitter was last visible.
    [[nodiscard]] uint32_t GetFramesSinceCulled() const { return m_framesSinceCulled; }

private:
    static constexpr uint32_t kChannelCount = 8; // pos, vel, age, life, size, color, rotation, dynamicParams
    static constexpr uint32_t kSpawnGroupSize = 256;
    static constexpr uint32_t kUpdateGroupSize = 256;

    IRHIDevice* m_device = nullptr;
    uint32_t m_maxParticles = 0;
    int m_pingPong = 0;

    // Particle SoA buffers: [channel][pingPongSet]
    // Channels: 0=positions(float4), 1=velocities(float4), 2=ages(float),
    //           3=lifespans(float), 4=sizes(float), 5=colors(float4), 6=rotations(float),
    //           7=dynamicParams(float4)
    RHIBufferHandle m_particleBuffers[kChannelCount][2] = {};

    // Bookkeeping buffers
    RHIBufferHandle m_aliveCount;    ///< Single uint32: number of alive particles.
    RHIBufferHandle m_freeList;      ///< Array of uint32: free particle slot indices.
    RHIBufferHandle m_freeListHead;  ///< Single uint32: stack top pointer.
    RHIBufferHandle m_indirectArgs;  ///< Draw indirect args (6 uints: vertexCount, instanceCount, ...).

    // Sort buffers
    RHIBufferHandle m_sortKeys;      ///< float per particle: depth for sorting.
    RHIBufferHandle m_sortIndices;   ///< uint32 per particle: particle index.

    // Staging buffers for initial free list upload (destroyed on Shutdown)
    RHIBufferHandle m_freeListStaging;     ///< Staging buffer for free list init.
    RHIBufferHandle m_freeListHeadStaging; ///< Staging buffer for free list head init.
    RHIBufferHandle m_aliveCountStaging;   ///< Staging buffer for alive count init.
    RHIBufferHandle m_zeroInitStaging;     ///< Staging buffer for zeroing particle attributes.

    // ── Wind zone GPU buffer ──────────────────────────────────────────────
    /// @ref Stam 1999 "Stable Fluids" — wind field accumulation
    RHIBufferHandle m_windZoneBuffer;        ///< StructuredBuffer<WindZone>[kMaxWindZones] on GPU.
    uint32_t        m_windZoneSrvIndex  = 0; ///< Bindless SRV descriptor index for m_windZoneBuffer.
    uint32_t        m_activeWindZones   = 0; ///< Number of active wind zones this frame.

    // ── Kill volume GPU buffer ────────────────────────────────────────────
    /// @ref Ericson 2005 "Real-Time Collision Detection" Ch.4
    RHIBufferHandle m_killVolumeBuffer;        ///< StructuredBuffer<KillVolume>[kMaxKillVolumes] on GPU.
    uint32_t        m_killVolumeSrvIndex = 0;  ///< Bindless SRV descriptor index for m_killVolumeBuffer.
    uint32_t        m_activeKillVolumes  = 0;  ///< Number of active kill volumes this frame.

    // ── Death event GPU buffers (Task 4.11.0) ────────────────────────────
    /// @ref Shopf 2008 "GPU Particles" — death event buffer for sub-emitter spawning
    RHIBufferHandle m_deathEventBuffer;           ///< RWStructuredBuffer<DeathEvent>[VFX_MAX_DEATH_EVENTS].
    RHIBufferHandle m_deathEventCountBuffer;      ///< RWBuffer<uint> — atomic event counter (1 element).
    RHIBufferHandle m_deathEventCountZeroStaging; ///< Upload-heap zero buffer for per-frame counter reset.
    uint32_t        m_deathEventBufferUavIndex = 0; ///< UAV descriptor index for m_deathEventBuffer.
    uint32_t        m_deathEventCountUavIndex  = 0; ///< UAV descriptor index for m_deathEventCountBuffer.
    uint32_t        m_deathEventBufferSrvIndex = 0; ///< SRV descriptor index (read by death spawn CS).
    uint32_t        m_deathEventCountSrvIndex  = 0; ///< SRV descriptor index for count (read by death spawn CS).

    // Compute pipelines
    RHIPipelineHandle m_spawnPSO;
    RHIPipelineHandle m_updatePSO;
    RHIPipelineHandle m_indirectArgsPSO;
    RHIPipelineHandle m_sortPSO;
    RHIPipelineHandle m_deathSpawnPSO;            ///< Secondary spawn CS for sub-emitter children on death.

    // Graphics pipelines for billboard rendering (one per blend mode)
    RHIPipelineHandle m_billboardPSO;                ///< PremulAlpha: One/InvSrcAlpha (default).
    RHIPipelineHandle m_billboardPSO_Additive;       ///< Additive: One/One (fire, sparks).
    RHIPipelineHandle m_billboardPSO_AlphaBlend;     ///< AlphaBlend: SrcAlpha/InvSrcAlpha (smoke).
    // Lit PSO removed — litMode handled via CB flag in standard PS.

    // Color ramp LUT texture
    RHITextureHandle  m_colorRampLUT;                ///< 256x1 RGBA16F LUT for color-over-life gradient.
    uint32_t          m_colorRampSrvIndex = UINT32_MAX; ///< Bindless SRV index for the LUT.

    // ── Frustum cull state (Task 3.89.0) ─────────────────────────────────────
    /// @ref Assarsson 2000 "Optimized View Frustum Culling Algorithms" — sphere-plane test
    bool     m_culledThisFrame   = false;  ///< True when frustum culled this frame (skip dispatch+draw).
    bool     m_culledPrevFrame   = false;  ///< Previous frame's cull state (requires 2 consecutive frames to skip).
    uint32_t m_framesSinceCulled = 0;      ///< Frames elapsed since last visible (for respawn logic).

    /// Returns the element stride in bytes for the given channel index.
    [[nodiscard]] static uint32_t ChannelStride(uint32_t channel);

    /// Creates a single structured buffer for a particle channel.
    [[nodiscard]] RHIBufferHandle CreateParticleBuffer(IRHIDevice* device,
                                                        uint32_t stride,
                                                        const char* name);
};

// ─────────────────────────────────────────────────────────────────────────────
// Trail Root Constants (matches TrailRootConstants in trail_common.hlsli, b0)
// ─────────────────────────────────────────────────────────────────────────────

/// CPU-side mirror of the HLSL TrailRootConstants struct (32 bytes = 8 x uint32).
///
/// Uploaded via `SetRootConstants(0, ...)` before each trail draw call. Shared
/// by the trail vertex and pixel shaders through root parameter 0 (b0).
struct TrailRootConstants {
    float    uvScrollRate;          ///< U-axis scroll speed (UV-units/second).
    float    softDepthRange;        ///< Soft-fade radius in metres. 0 = disabled.
    float    alphaClipThreshold;    ///< Alpha discard threshold [0, 1].
    uint32_t trailTexSrvIndex;      ///< Bindless SRV index for trail texture (UINT32_MAX = none).
    uint32_t depthBufferSrvIndex;   ///< Bindless SRV index for scene depth buffer.
    uint32_t vertexBufferSrvIndex;  ///< Bindless SRV index for trail StructuredBuffer<TrailVertexData>.
    uint32_t indexBufferSrvIndex;   ///< Bindless SRV index for trail StructuredBuffer<uint>.
    uint32_t _pad0 = 0;            ///< Alignment pad — unused.
};
static_assert(sizeof(TrailRootConstants) == 32, "TrailRootConstants must be 32 bytes (8 x uint32)");

// ─────────────────────────────────────────────────────────────────────────────
// Decal Object CB (matches DecalObjectCB in geometry/decal_vs.hlsl + decal_forward_ps.hlsl, b2)
// ─────────────────────────────────────────────────────────────────────────────

/// Per-decal constant buffer uploaded at b2 for the decal projection pass.
/// Layout matches DecalObjectCB in both decal_vs.hlsl and decal_forward_ps.hlsl.
///
/// @ref Valient 2014 "Deferred Rendering in Killzone: Shadow Fall" — DBuffer decals
struct DecalObjectCB {
    Mat4     decalWorld;                          ///< World transform (pos + rot + extents).
    Mat4     decalInvWorld;                       ///< Inverse of decalWorld (world → local projection).
    float    decalOpacity       = 1.0f;           ///< Global opacity [0, 1].
    float    decalNormalBlend   = 0.8f;           ///< Normal blend intensity [0, 1] (unused in forward).
    float    decalRoughness     = 0.5f;           ///< Roughness value/multiplier.
    float    decalMetallic      = 0.0f;           ///< Metallic value/multiplier.
    float    decalFadeDistance  = 10.0f;          ///< Edge fade distance in cm.
    float    decalAngleFade    = 60.0f;           ///< Angle fade start degrees (unused in forward).
    uint32_t decalAlbedoSrv    = UINT32_MAX;      ///< Bindless SRV index for albedo texture.
    uint32_t decalNormalSrv    = UINT32_MAX;      ///< Normal texture SRV index (unused in forward).
    uint32_t decalMaterialSrv  = UINT32_MAX;      ///< Material texture SRV index (unused in forward).
    uint32_t decalDepthSrv     = UINT32_MAX;      ///< Depth buffer SRV index.
    uint32_t decalNormalGbufSrv = UINT32_MAX;     ///< GBuffer normal SRV (unused in forward).
    float    _pad0             = 0.0f;            ///< Pad to 16-byte boundary.
};
static_assert(sizeof(DecalObjectCB) == 176, "DecalObjectCB must be 176 bytes (2x Mat4 + 12 floats/uints)");

// ─────────────────────────────────────────────────────────────────────────────
// Distortion Haze Root Constants (matches DistortionHazeConstants in distortion_haze_ps.hlsl, b0)
// ─────────────────────────────────────────────────────────────────────────────

/// Root constants for the procedural heat-haze distortion accumulation pass.
/// Uploaded via SetRootConstants(0, ...) before the fullscreen haze draw.
struct DistortionHazeConstants {
    float emitterScreenUV[2];  ///< Emitter position in screen UV [0,1].
    float strength;            ///< Global distortion strength (0=off, 1=full).
    float time;                ///< Total elapsed time for animation.
    float radius;              ///< Falloff radius in screen UV units.
    float frequency;           ///< Wave frequency for animated haze.
    float _pad0 = 0.0f;
    float _pad1 = 0.0f;
};
static_assert(sizeof(DistortionHazeConstants) == 32, "DistortionHazeConstants must be 32 bytes (8 x uint32)");

// ─────────────────────────────────────────────────────────────────────────────
// Distortion Resolve Root Constants (matches DistortionResolveIndices in distortion_resolve_ps.hlsl, b0)
// ─────────────────────────────────────────────────────────────────────────────

/// Root constants for the distortion resolve fullscreen pass.
/// Points the shader at the pre-distortion HDR color and the distortion accumulation textures.
struct DistortionResolveConstants {
    uint32_t colorTextureSrvIndex;       ///< SRV index: pre-distortion HDR color.
    uint32_t distortionTextureSrvIndex;  ///< SRV index: RGBA16F distortion buffer.
    uint32_t _pad0 = 0;
    uint32_t _pad1 = 0;
};
static_assert(sizeof(DistortionResolveConstants) == 16, "DistortionResolveConstants must be 16 bytes (4 x uint32)");

} // namespace uz
