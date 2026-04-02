/// VFX Viewport Demo — GPU Particle System Implementation
///
/// Creates SoA particle buffers, compiles compute/graphics pipelines via
/// ShaderManager, and dispatches spawn/update/sort/billboard passes.

#include "vfx_viewport_vfx.h"
#include "rhi_device.h"
#include "rhi_command_list.h"
#include "shader_manager.h"
#include "cb_ring_allocator.h"
#include "engine_shaders.h"
#include "engine/vfx/curves/vfx_color_ramp.h"
#include "engine/renderer/upload/upload_context.h"
#include "log.h"

#include <algorithm>
#include <cmath>
#include <cstring>

UZ_DECLARE_LOG_CATEGORY(App);

namespace uz {

// ─────────────────────────────────────────────────────────────────────────────
// VFX Presets (matches the original uz_viewport.cpp presets)
// ─────────────────────────────────────────────────────────────────────────────

static const VFXPreset s_presets[] = {
    { "Simple Burst", {
        /*position*/ {0.0f, 0.0f, 0.0f}, /*spawnRadius*/ 0.2f,
        /*velocity*/ {0.0f, 0.0f, 6.0f}, /*velVariation*/ 3.0f,
        /*spread*/ 1.0f, /*minLife*/ 1.0f, /*maxLife*/ 2.5f, /*size*/ 1.0f,
        /*color*/ {1.0f, 0.8f, 0.4f, 1.0f},
        /*gravity*/ {0.0f, 0.0f, -3.0f}, /*damping*/ 0.15f,
        /*softFade*/ 0.5f, /*emissive*/ 1.0f,
        /*sizeOverLife*/ 0.5f, 1.2f, 0.0f, 0.3f,
        /*colorLifeStart*/ 1.0f, 0.9f, 0.5f, 1.0f,
        /*colorLifeEnd*/ 1.0f, 0.3f, 0.05f, 0.0f,
        /*facingMode*/ 0, /*customFacingX*/ 0.0f, /*customFacingY*/ 0.0f, /*customFacingZ*/ 1.0f, /*stretchFactor*/ 0.0f,
        /*spawnShape*/ 0, /*spawnOnSurface*/ 0,
        /*shapeParam1*/ 0.0f, /*shapeParam2*/ 0.0f, /*shapeParam3*/ 0.0f,
        /*blendMode*/ 1 // Additive — fire glow on dark backgrounds
    }, 300.0f },
    { "Smoke Plume", {
        /*position*/ {0.0f, 0.0f, 0.0f}, /*spawnRadius*/ 0.2f,
        /*velocity*/ {0.0f, 0.0f, 3.0f}, /*velVariation*/ 0.5f,
        /*spread*/ 0.6f, /*minLife*/ 3.0f, /*maxLife*/ 6.0f, /*size*/ 1.2f,
        /*color*/ {0.5f, 0.5f, 0.5f, 0.7f},
        /*gravity*/ {0.0f, 0.0f, -1.0f}, /*damping*/ 0.2f,
        /*softFade*/ 0.5f, /*emissive*/ 0.0f,
        /*sizeOverLife*/ 0.3f, 1.5f, 2.5f, 0.3f,
        /*colorLifeStart*/ 0.7f, 0.6f, 0.5f, 0.8f,
        /*colorLifeEnd*/ 0.2f, 0.2f, 0.2f, 0.0f,
        /*facingMode*/ 0, /*customFacingX*/ 0.0f, /*customFacingY*/ 0.0f, /*customFacingZ*/ 1.0f, /*stretchFactor*/ 0.0f,
        /*spawnShape*/ 0, /*spawnOnSurface*/ 0,
        /*shapeParam1*/ 0.0f, /*shapeParam2*/ 0.0f, /*shapeParam3*/ 0.0f,
        /*blendMode*/ 0 // AlphaBlend — smoke is translucent, not additive
    }, 200.0f },
    // ── Ground Collision Preset ────────────────────────────────────────────
    /// @ref Müller 2006 "Position Based Dynamics" — restitution + friction response
    { "Sparks", {
        /*position*/ {0.0f, 0.0f, 0.0f}, /*spawnRadius*/ 0.15f,
        /*velocity*/ {0.0f, 0.0f, 10.0f}, /*velVariation*/ 4.0f,
        /*spread*/ 1.0f, /*minLife*/ 0.5f, /*maxLife*/ 1.5f, /*size*/ 0.15f,
        /*color*/ {1.0f, 0.9f, 0.5f, 1.0f},
        /*gravity*/ {0.0f, 0.0f, -12.0f}, /*damping*/ 0.03f,
        /*softFade*/ 0.2f, /*emissive*/ 2.0f,
        /*sizeOverLife*/ 1.0f, 1.0f, 0.3f, 0.0f,
        /*colorLifeStart*/ 1.0f, 0.95f, 0.7f, 1.0f,
        /*colorLifeEnd*/ 1.0f, 0.3f, 0.0f, 0.2f,
        /*facingMode*/ 1, /*customFacingX*/ 0.0f, /*customFacingY*/ 0.0f, /*customFacingZ*/ 1.0f, /*stretchFactor*/ 2.0f,
        /*spawnShape*/ 0, /*spawnOnSurface*/ 0,
        /*shapeParam1*/ 0.0f, /*shapeParam2*/ 0.0f, /*shapeParam3*/ 0.0f,
        /*blendMode*/ 1, // Additive — bright spark streaks
        /*curlStrength*/ 0.0f, /*curlFrequency*/ 0.5f, /*curlSpeed*/ 0.2f,
        /*vortexAxisOrigin*/ {0,0,0}, /*vortexAxisDir*/ {0,0,1},
        /*vortexTangential*/ 0.0f, /*vortexRadial*/ 0.0f, /*vortexMaxRadius*/ 5.0f,
        /*windDir*/ {1,0,0}, /*windStrength*/ 0.0f,
        /*litMode*/ 0,
        /*groundCollision*/ 1,   // Sparks bounce off Z=0 ground plane
        /*restitution*/ 0.5f,    // Fairly elastic bounce (hot metal spark energy)
        /*collisionResponse*/ 0, // Bounce — sparks ricochet off the surface
        /*collisionFriction*/ 0.7f // Moderate friction: sparks slide briefly then stop
    }, 800.0f },
    { "Magic Orb", VFXEmitterConfig{
        .position        = {0.0f, 0.0f, 0.0f},
        .spawnRadius     = 0.8f,
        .spawnVelocity   = {0.0f, 0.0f, 1.0f},
        .velocityVariation = 0.6f,
        .spreadAngle     = 3.14159f,
        .minLifetime     = 1.0f,
        .maxLifetime     = 2.0f,
        .initialSize     = 0.5f,
        .initialColor    = {0.3f, 0.6f, 1.0f, 0.9f},
        .gravity         = {0.0f, 0.0f, 0.0f},
        .damping         = 0.3f,
        .softFadeDistance = 0.3f,
        .emissiveIntensity = 0.5f,
        .sizeOverLifeStart    = 0.5f,
        .sizeOverLifePeak     = 1.0f,
        .sizeOverLifeEnd      = 0.2f,
        .sizeOverLifePeakTime = 0.4f,
        .colorLifeStartR = 0.4f, .colorLifeStartG = 0.8f, .colorLifeStartB = 1.0f, .colorLifeStartA = 1.0f,
        .colorLifeEndR   = 0.1f, .colorLifeEndG   = 0.2f, .colorLifeEndB   = 0.8f, .colorLifeEndA   = 0.0f,
        .blendMode       = 1, // Additive — glowing magical particles
        .rotationRate    = 1.5f, // slow spin — magical particles gently rotate
    }, 300.0f },
    { "Explosion Debris", {
        /*position*/ {0.0f, 0.0f, 0.0f}, /*spawnRadius*/ 0.3f,
        /*velocity*/ {0.0f, 0.0f, 8.0f}, /*velVariation*/ 4.0f,
        /*spread*/ 1.5f, /*minLife*/ 0.5f, /*maxLife*/ 2.0f, /*size*/ 0.5f,
        /*color*/ {1.0f, 0.5f, 0.15f, 1.0f},
        /*gravity*/ {0.0f, 0.0f, -20.0f}, /*damping*/ 0.08f,
        /*softFade*/ 0.3f, /*emissive*/ 1.0f,
        /*sizeOverLife*/ 0.8f, 1.5f, 0.1f, 0.1f,
        /*colorLifeStart*/ 1.0f, 0.8f, 0.4f, 1.0f,
        /*colorLifeEnd*/ 0.4f, 0.08f, 0.0f, 0.0f,
        /*facingMode*/ 1, /*customFacingX*/ 0.0f, /*customFacingY*/ 0.0f, /*customFacingZ*/ 1.0f, /*stretchFactor*/ 2.0f,
        /*spawnShape*/ 0, /*spawnOnSurface*/ 0,
        /*shapeParam1*/ 0.0f, /*shapeParam2*/ 0.0f, /*shapeParam3*/ 0.0f,
        /*blendMode*/ 1, // Additive — hot glowing debris
        /*curlStrength*/ 0.0f, /*curlFrequency*/ 0.5f, /*curlSpeed*/ 0.2f,
        /*vortexAxisOrigin*/ {0,0,0}, /*vortexAxisDir*/ {0,0,1},
        /*vortexTangential*/ 0.0f, /*vortexRadial*/ 0.0f, /*vortexMaxRadius*/ 5.0f,
        /*windDir*/ {1,0,0}, /*windStrength*/ 0.0f,
        /*litMode*/ 0,
        /*groundCollision*/ 1,   // Debris bounces off Z=0 ground plane
        /*restitution*/ 0.3f,    // Low restitution — heavy debris loses most energy on impact
        /*collisionResponse*/ 0, // Bounce — debris ricochets, energy is mostly absorbed
        /*collisionFriction*/ 0.8f // High friction: debris tumbles to a slow stop
    }, 2000.0f },
    { "Fireball", {
        /*position*/ {0.0f, 0.0f, 0.0f}, /*spawnRadius*/ 0.4f,
        /*velocity*/ {0.0f, 0.0f, 0.5f}, /*velVariation*/ 1.5f,
        /*spread*/ 2.5f, /*minLife*/ 0.3f, /*maxLife*/ 0.8f, /*size*/ 0.6f,
        /*color*/ {1.0f, 0.5f, 0.1f, 0.9f},
        /*gravity*/ {0.0f, 0.0f, 1.0f}, /*damping*/ 0.5f,
        /*softFade*/ 0.4f, /*emissive*/ 2.5f,
        /*sizeOverLife*/ 0.5f, 1.3f, 0.4f, 0.2f,
        /*colorLifeStart*/ 1.0f, 0.7f, 0.2f, 1.0f,
        /*colorLifeEnd*/ 0.7f, 0.15f, 0.02f, 0.3f,
        /*facingMode*/ 0, /*customFacingX*/ 0.0f, /*customFacingY*/ 0.0f, /*customFacingZ*/ 1.0f, /*stretchFactor*/ 0.5f,
        /*spawnShape*/ 0, /*spawnOnSurface*/ 0,
        /*shapeParam1*/ 0.0f, /*shapeParam2*/ 0.0f, /*shapeParam3*/ 0.0f,
        /*blendMode*/ 1 // Additive — intense fireball glow
    }, 800.0f },
    // ── Spawn Shape Presets ──────────────────────────────────────────────
    { "Ring of Fire", {
        /*position*/ {0.0f, 0.0f, 0.0f}, /*spawnRadius*/ 3.0f,
        /*velocity*/ {0.0f, 0.0f, 4.0f}, /*velVariation*/ 1.0f,
        /*spread*/ 0.5f, /*minLife*/ 0.5f, /*maxLife*/ 1.5f, /*size*/ 0.4f,
        /*color*/ {1.0f, 0.5f, 0.1f, 0.9f},
        /*gravity*/ {0.0f, 0.0f, -2.0f}, /*damping*/ 0.2f,
        /*softFade*/ 0.3f, /*emissive*/ 1.5f,
        /*sizeOverLife*/ 0.5f, 1.0f, 0.0f, 0.3f,
        /*colorLifeStart*/ 1.0f, 0.8f, 0.3f, 1.0f,
        /*colorLifeEnd*/ 1.0f, 0.2f, 0.0f, 0.0f,
        /*facingMode*/ 0, /*customFacingX*/ 0.0f, /*customFacingY*/ 0.0f, /*customFacingZ*/ 1.0f, /*stretchFactor*/ 0.0f,
        /*spawnShape*/ 3, /*spawnOnSurface*/ 0,
        /*shapeParam1*/ 0.3f, /*shapeParam2*/ 0.0f, /*shapeParam3*/ 0.0f,
        /*blendMode*/ 1 // Additive — ring of fire glow
    }, 400.0f },
    { "Cone Jet", {
        /*position*/ {0.0f, 0.0f, 0.0f}, /*spawnRadius*/ 0.1f,
        /*velocity*/ {0.0f, 0.0f, 10.0f}, /*velVariation*/ 2.0f,
        /*spread*/ 0.3f, /*minLife*/ 0.5f, /*maxLife*/ 1.5f, /*size*/ 0.2f,
        /*color*/ {0.3f, 0.6f, 1.0f, 0.9f},
        /*gravity*/ {0.0f, 0.0f, -5.0f}, /*damping*/ 0.1f,
        /*softFade*/ 0.3f, /*emissive*/ 2.0f,
        /*sizeOverLife*/ 0.3f, 1.0f, 0.0f, 0.3f,
        /*colorLifeStart*/ 0.5f, 0.8f, 1.0f, 1.0f,
        /*colorLifeEnd*/ 0.1f, 0.3f, 0.8f, 0.0f,
        /*facingMode*/ 1, /*customFacingX*/ 0.0f, /*customFacingY*/ 0.0f, /*customFacingZ*/ 1.0f, /*stretchFactor*/ 1.5f,
        /*spawnShape*/ 4, /*spawnOnSurface*/ 1,
        /*shapeParam1*/ 0.5f, /*shapeParam2*/ 2.0f, /*shapeParam3*/ 0.0f,
        /*blendMode*/ 1 // Additive — bright energy jet
    }, 600.0f },
    { "Cylinder Volume", {
        /*position*/ {0.0f, 0.0f, 0.0f}, /*spawnRadius*/ 2.0f,
        /*velocity*/ {0.0f, 0.0f, 2.0f}, /*velVariation*/ 0.5f,
        /*spread*/ 0.8f, /*minLife*/ 1.5f, /*maxLife*/ 3.0f, /*size*/ 0.3f,
        /*color*/ {0.2f, 0.9f, 0.3f, 0.8f},
        /*gravity*/ {0.0f, 0.0f, -1.0f}, /*damping*/ 0.15f,
        /*softFade*/ 0.4f, /*emissive*/ 0.5f,
        /*sizeOverLife*/ 0.5f, 1.2f, 0.3f, 0.4f,
        /*colorLifeStart*/ 0.3f, 1.0f, 0.4f, 1.0f,
        /*colorLifeEnd*/ 0.1f, 0.5f, 0.1f, 0.0f,
        /*facingMode*/ 0, /*customFacingX*/ 0.0f, /*customFacingY*/ 0.0f, /*customFacingZ*/ 1.0f, /*stretchFactor*/ 0.0f,
        /*spawnShape*/ 5, /*spawnOnSurface*/ 0,
        /*shapeParam1*/ 4.0f, /*shapeParam2*/ 0.0f, /*shapeParam3*/ 0.0f,
        /*blendMode*/ 0 // AlphaBlend — translucent volume particles
    }, 300.0f },
    // ── Curl Noise Preset ───────────────────────────────────────────────
    { "Curl Smoke", {
        /*position*/ {0.0f, 0.0f, 0.0f}, /*spawnRadius*/ 0.5f,
        /*velocity*/ {0.0f, 0.0f, 1.5f}, /*velVariation*/ 0.5f,
        /*spread*/ 0.6f, /*minLife*/ 2.0f, /*maxLife*/ 5.0f, /*size*/ 0.6f,
        /*color*/ {0.6f, 0.6f, 0.6f, 0.8f},
        /*gravity*/ {0.0f, 0.0f, 0.0f}, /*damping*/ 0.1f,
        /*softFade*/ 0.5f, /*emissive*/ 0.0f,
        /*sizeOverLife*/ 0.3f, 1.5f, 2.0f, 0.3f,
        /*colorLifeStart*/ 0.7f, 0.7f, 0.7f, 0.8f,
        /*colorLifeEnd*/ 0.3f, 0.3f, 0.3f, 0.0f,
        /*facingMode*/ 0, /*customFacingX*/ 0.0f, /*customFacingY*/ 0.0f, /*customFacingZ*/ 1.0f, /*stretchFactor*/ 0.0f,
        /*spawnShape*/ 0, /*spawnOnSurface*/ 0,
        /*shapeParam1*/ 0.0f, /*shapeParam2*/ 0.0f, /*shapeParam3*/ 0.0f,
        /*blendMode*/ 0, // AlphaBlend — smoke is translucent
        /*curlStrength*/ 2.0f, /*curlFrequency*/ 0.4f, /*curlSpeed*/ 0.15f,
        /*vortexAxisOrigin*/ {0,0,0}, /*vortexAxisDir*/ {0,0,1},
        /*vortexTangential*/ 0.0f, /*vortexRadial*/ 0.0f, /*vortexMaxRadius*/ 5.0f,
        /*windDir*/ {1,0,0}, /*windStrength*/ 0.0f
    }, 200.0f },
    // ── Color Ramp LUT Preset ──────────────────────────────────────────
    // Designed to showcase the color ramp LUT feature (N key to activate).
    // White initial color lets the LUT gradient define the full color curve.
    { "Fire Ramp", {
        /*position*/ {0.0f, 0.0f, 0.0f}, /*spawnRadius*/ 0.3f,
        /*velocity*/ {0.0f, 0.0f, 5.0f}, /*velVariation*/ 2.0f,
        /*spread*/ 0.8f, /*minLife*/ 0.5f, /*maxLife*/ 2.0f, /*size*/ 0.8f,
        /*color*/ {1.0f, 1.0f, 1.0f, 1.0f},
        /*gravity*/ {0.0f, 0.0f, -2.0f}, /*damping*/ 0.15f,
        /*softFade*/ 0.5f, /*emissive*/ 1.5f,
        /*sizeOverLife*/ 0.5f, 1.3f, 0.0f, 0.3f,
        /*colorLifeStart*/ 1.0f, 1.0f, 1.0f, 1.0f,
        /*colorLifeEnd*/ 1.0f, 1.0f, 1.0f, 0.0f,
        /*facingMode*/ 0, /*customFacingX*/ 0.0f, /*customFacingY*/ 0.0f, /*customFacingZ*/ 1.0f, /*stretchFactor*/ 0.0f,
        /*spawnShape*/ 0, /*spawnOnSurface*/ 0,
        /*shapeParam1*/ 0.0f, /*shapeParam2*/ 0.0f, /*shapeParam3*/ 0.0f,
        /*blendMode*/ 1 // Additive — fire glow
    }, 500.0f },
    // ── Vortex Force Presets ──────────────────────────────────────────
    /// @ref Stomakhin 2013 "A Material Point Method for Snow Simulation" App.A
    { "Tornado", {
        /*position*/ {0.0f, 0.0f, 0.0f}, /*spawnRadius*/ 2.0f,
        /*velocity*/ {1.0f, 0.0f, 4.0f}, /*velVariation*/ 1.5f,
        /*spread*/ 1.2f, /*minLife*/ 2.0f, /*maxLife*/ 4.0f, /*size*/ 0.25f,
        /*color*/ {0.7f, 0.6f, 0.5f, 0.7f},
        /*gravity*/ {0.0f, 0.0f, -0.5f}, /*damping*/ 0.05f,
        /*softFade*/ 0.4f, /*emissive*/ 0.0f,
        /*sizeOverLife*/ 0.4f, 1.2f, 2.0f, 0.3f,
        /*colorLifeStart*/ 0.8f, 0.7f, 0.5f, 0.9f,
        /*colorLifeEnd*/ 0.4f, 0.4f, 0.35f, 0.0f,
        /*facingMode*/ 0, /*customFacingX*/ 0.0f, /*customFacingY*/ 0.0f, /*customFacingZ*/ 1.0f, /*stretchFactor*/ 0.0f,
        /*spawnShape*/ 1, /*spawnOnSurface*/ 0,
        /*shapeParam1*/ 0.0f, /*shapeParam2*/ 0.0f, /*shapeParam3*/ 0.0f,
        /*blendMode*/ 0, // AlphaBlend — dust/debris is translucent
        /*curlStrength*/ 0.0f, /*curlFrequency*/ 0.5f, /*curlSpeed*/ 0.2f,
        /*vortexAxisOrigin*/ {0,0,0}, /*vortexAxisDir*/ {0,0,1},
        /*vortexTangential*/ 8.0f, /*vortexRadial*/ 2.0f, /*vortexMaxRadius*/ 3.0f,
        /*windDir*/ {1,0,0}, /*windStrength*/ 0.0f
    }, 300.0f },
    { "Whirlpool", {
        /*position*/ {0.0f, 0.0f, 0.0f}, /*spawnRadius*/ 1.0f,
        /*velocity*/ {0.0f, 0.0f, 0.5f}, /*velVariation*/ 0.5f,
        /*spread*/ 0.8f, /*minLife*/ 2.0f, /*maxLife*/ 4.0f, /*size*/ 0.3f,
        /*color*/ {0.2f, 0.5f, 0.9f, 0.7f},
        /*gravity*/ {0.0f, 0.0f, -9.81f}, /*damping*/ 0.15f,
        /*softFade*/ 0.4f, /*emissive*/ 0.0f,
        /*sizeOverLife*/ 0.5f, 1.0f, 0.5f, 0.4f,
        /*colorLifeStart*/ 0.3f, 0.6f, 1.0f, 0.8f,
        /*colorLifeEnd*/ 0.1f, 0.3f, 0.7f, 0.0f,
        /*facingMode*/ 0, /*customFacingX*/ 0.0f, /*customFacingY*/ 0.0f, /*customFacingZ*/ 1.0f, /*stretchFactor*/ 0.0f,
        /*spawnShape*/ 0, /*spawnOnSurface*/ 0,
        /*shapeParam1*/ 0.0f, /*shapeParam2*/ 0.0f, /*shapeParam3*/ 0.0f,
        /*blendMode*/ 0, // AlphaBlend — water-like particles
        /*curlStrength*/ 0.0f, /*curlFrequency*/ 0.5f, /*curlSpeed*/ 0.2f,
        /*vortexAxisOrigin*/ {0,0,0}, /*vortexAxisDir*/ {0,0,1},
        /*vortexTangential*/ 5.0f, /*vortexRadial*/ -1.0f, /*vortexMaxRadius*/ 4.0f,
        /*windDir*/ {1,0,0}, /*windStrength*/ 0.0f
    }, 250.0f },
    // ── Wind Zone Preset ───────────────────────────────────────────────
    /// @ref Stam 1999 "Stable Fluids" -- wind field accumulation
    { "Windy Smoke", {
        /*position*/ {0.0f, 0.0f, 0.0f}, /*spawnRadius*/ 0.3f,
        /*velocity*/ {0.0f, 0.0f, 2.0f}, /*velVariation*/ 0.5f,
        /*spread*/ 0.5f, /*minLife*/ 3.0f, /*maxLife*/ 6.0f, /*size*/ 0.8f,
        /*color*/ {0.5f, 0.5f, 0.5f, 0.6f},
        /*gravity*/ {0.0f, 0.0f, -0.3f}, /*damping*/ 0.12f,
        /*softFade*/ 0.5f, /*emissive*/ 0.0f,
        /*sizeOverLife*/ 0.4f, 1.5f, 2.8f, 0.3f,
        /*colorLifeStart*/ 0.6f, 0.6f, 0.6f, 0.7f,
        /*colorLifeEnd*/ 0.2f, 0.2f, 0.2f, 0.0f,
        /*facingMode*/ 0, /*customFacingX*/ 0.0f, /*customFacingY*/ 0.0f, /*customFacingZ*/ 1.0f, /*stretchFactor*/ 0.0f,
        /*spawnShape*/ 0, /*spawnOnSurface*/ 0,
        /*shapeParam1*/ 0.0f, /*shapeParam2*/ 0.0f, /*shapeParam3*/ 0.0f,
        /*blendMode*/ 0, // AlphaBlend — smoke
        /*curlStrength*/ 0.5f, /*curlFrequency*/ 0.3f, /*curlSpeed*/ 0.1f,
        /*vortexAxisOrigin*/ {0,0,0}, /*vortexAxisDir*/ {0,0,1},
        /*vortexTangential*/ 0.0f, /*vortexRadial*/ 0.0f, /*vortexMaxRadius*/ 5.0f,
        /*windDir*/ {1,0,0}, /*windStrength*/ 5.0f // 5 m/s ambient wind (+X)
    }, 200.0f },
    // ── Lit Particle Preset ────────────────────────────────────────────
    /// @ref Pharr 2023 "Physically Based Rendering" Ch.12 — Lambertian diffuse NdotL
    { "Lit Smoke", {
        /*position*/ {0.0f, 0.0f, 0.0f}, /*spawnRadius*/ 0.3f,
        /*velocity*/ {0.0f, 0.0f, 2.0f}, /*velVariation*/ 0.5f,
        /*spread*/ 0.6f, /*minLife*/ 3.0f, /*maxLife*/ 6.0f, /*size*/ 1.0f,
        /*color*/ {0.8f, 0.8f, 0.8f, 1.0f},
        /*gravity*/ {0.0f, 0.0f, 0.0f}, /*damping*/ 0.1f,
        /*softFade*/ 0.5f, /*emissive*/ 0.0f,
        /*sizeOverLife*/ 0.3f, 1.5f, 2.0f, 0.3f,
        /*colorLifeStart*/ 0.8f, 0.8f, 0.8f, 1.0f,
        /*colorLifeEnd*/ 0.3f, 0.3f, 0.3f, 0.0f,
        /*facingMode*/ 0, /*customFacingX*/ 0.0f, /*customFacingY*/ 0.0f, /*customFacingZ*/ 1.0f, /*stretchFactor*/ 0.0f,
        /*spawnShape*/ 0, /*spawnOnSurface*/ 0,
        /*shapeParam1*/ 0.0f, /*shapeParam2*/ 0.0f, /*shapeParam3*/ 0.0f,
        /*blendMode*/ 0, // AlphaBlend — lit smoke is translucent, not additive
        /*curlStrength*/ 0.5f, /*curlFrequency*/ 0.3f, /*curlSpeed*/ 0.1f,
        /*vortexAxisOrigin*/ {0,0,0}, /*vortexAxisDir*/ {0,0,1},
        /*vortexTangential*/ 0.0f, /*vortexRadial*/ 0.0f, /*vortexMaxRadius*/ 5.0f,
        /*windDir*/ {1,0,0}, /*windStrength*/ 0.0f,
        /*litMode*/ 1 // Lit particles — NdotL directional + ambient
    }, 200.0f },
    /// @ref Pharr 2023 "Physically Based Rendering" Ch.12 — Lambertian diffuse NdotL
    { "Lit Debris", {
        /*position*/ {0.0f, 0.0f, 0.0f}, /*spawnRadius*/ 0.3f,
        /*velocity*/ {0.0f, 0.0f, 8.0f}, /*velVariation*/ 4.0f,
        /*spread*/ 1.5f, /*minLife*/ 0.5f, /*maxLife*/ 2.0f, /*size*/ 0.4f,
        /*color*/ {0.7f, 0.6f, 0.5f, 1.0f},
        /*gravity*/ {0.0f, 0.0f, -20.0f}, /*damping*/ 0.08f,
        /*softFade*/ 0.3f, /*emissive*/ 0.2f,
        /*sizeOverLife*/ 0.8f, 1.2f, 0.1f, 0.1f,
        /*colorLifeStart*/ 0.8f, 0.7f, 0.5f, 1.0f,
        /*colorLifeEnd*/ 0.4f, 0.35f, 0.25f, 0.0f,
        /*facingMode*/ 1, /*customFacingX*/ 0.0f, /*customFacingY*/ 0.0f, /*customFacingZ*/ 1.0f, /*stretchFactor*/ 1.5f,
        /*spawnShape*/ 0, /*spawnOnSurface*/ 0,
        /*shapeParam1*/ 0.0f, /*shapeParam2*/ 0.0f, /*shapeParam3*/ 0.0f,
        /*blendMode*/ 0, // AlphaBlend — lit debris is translucent, shows directional shading
        /*curlStrength*/ 0.0f, /*curlFrequency*/ 0.5f, /*curlSpeed*/ 0.2f,
        /*vortexAxisOrigin*/ {0,0,0}, /*vortexAxisDir*/ {0,0,1},
        /*vortexTangential*/ 0.0f, /*vortexRadial*/ 0.0f, /*vortexMaxRadius*/ 5.0f,
        /*windDir*/ {1,0,0}, /*windStrength*/ 0.0f,
        /*litMode*/ 1,           // Lit particles — NdotL directional + ambient
        /*groundCollision*/ 1,   // Lit debris collides with Z=0 ground plane
        /*restitution*/ 0.0f,    // No bounce energy — stick mode ignores restitution
        /*collisionResponse*/ 1, // Stick — lit debris embeds into surface on impact
        /*collisionFriction*/ 0.8f // Friction irrelevant in stick mode (vel=0 on contact)
    }, 1500.0f },

    // ── Orbital preset (Task 4.9.0) ────────────────────────────────────────────
    // Particles orbit the emitter origin on a horizontal circle. No radial drift —
    // initial velocity is zero so particles stay on the ring. The golden angle spread
    // (baked into the shader) guarantees uniform distribution at any particle count.
    // @ref Christensen 2018 "Fibonacci Sphere Sampling" — golden angle for uniform spread
    { "Orbital", VFXEmitterConfig{
        .position        = {0.0f, 0.0f, 0.0f},
        .spawnRadius     = 0.01f,          // spawn near center — orbit overrides position
        .spawnVelocity   = {0.0f, 0.0f, 0.0f}, // no initial velocity; orbit sets position
        .velocityVariation = 0.0f,
        .spreadAngle     = 0.0f,
        .minLifetime     = 7.0f,           // ~1.8 full revolutions at 1.6 rad/s
        .maxLifetime     = 9.0f,
        .initialSize     = 0.5f,
        .initialColor    = {0.4f, 0.8f, 1.0f, 1.0f}, // cyan-blue
        .gravity         = {0.0f, 0.0f, 0.0f},        // no gravity — clean orbit
        .damping         = 0.0f,
        .softFadeDistance = 0.3f,
        .emissiveIntensity = 2.0f,         // glow effect for orbiting energy particles
        .sizeOverLifeStart    = 0.3f,
        .sizeOverLifePeak     = 1.0f,
        .sizeOverLifeEnd      = 0.0f,
        .sizeOverLifePeakTime = 0.2f,
        .colorLifeStartR = 0.4f, .colorLifeStartG = 0.8f, .colorLifeStartB = 1.0f, .colorLifeStartA = 1.0f,
        .colorLifeEndR   = 0.1f, .colorLifeEndG   = 0.3f, .colorLifeEndB   = 0.8f, .colorLifeEndA   = 0.0f,
        .facingMode      = 0,              // FaceCamera — spherical facing for glow orbs
        .customFacingX   = 0.0f,
        .customFacingY   = 0.0f,
        .customFacingZ   = 1.0f,
        .stretchFactor   = 0.0f,
        .spawnShape      = 0,              // Point spawn — orbit radius controls ring size
        .spawnOnSurface  = 0,
        .shapeParam1     = 0.0f,
        .shapeParam2     = 0.0f,
        .shapeParam3     = 0.0f,
        .blendMode       = 1,              // Additive — glowing energy orbs composite nicely
        .curlStrength    = 0.0f,
        .curlFrequency   = 0.5f,
        .curlSpeed       = 0.2f,
        .vortexAxisOrigin = {0,0,0},
        .vortexAxisDir    = {0,0,1},
        .vortexTangentialStrength = 0.0f,
        .vortexRadialStrength     = 0.0f,
        .vortexMaxRadius          = 5.0f,
        .windDir         = {1,0,0},
        .windStrength    = 0.0f,
        .litMode         = 0,              // Unlit — emissive glow doesn't need directional
        .groundCollision = 0,
        .restitution     = 0.0f,
        .orbitEnabled    = 1,              // activate orbit
        .orbitRadius     = 150.0f,         // 150 world units radius
        .orbitSpeed      = 1.2f,           // ~1 revolution / 5.2 seconds
        .orbitAxisX      = 0.0f,
        .orbitAxisY      = 0.0f,
        .orbitAxisZ      = 1.0f,           // Z-up axis → horizontal orbit in XY plane
    }, 12.0f },  // 12 particles/sec → 84-108 alive at steady state, fills the ring

    // ── Air Resist preset (Task 4.5.0/4.8.0) ──────────────────────────────────
    // High drag coefficient, zero damping, high initial velocity. Demonstrates
    // quadratic drag decelerating fast-moving particles to a near stop.
    // Color-by-speed ON: fast=bright yellow, slow=dark red.
    // @ref Witkin & Baraff 2001 "Physically Based Modeling" — quadratic drag
    { "Air Resist", VFXEmitterConfig{
        .position        = {0.0f, 0.0f, 0.0f},
        .spawnRadius     = 0.1f,
        .spawnVelocity   = {0.0f, 0.0f, 15.0f},  // high initial upward velocity
        .velocityVariation = 3.0f,
        .spreadAngle     = 1.2f,
        .minLifetime     = 2.0f,
        .maxLifetime     = 4.0f,
        .initialSize     = 0.3f,
        .initialColor    = {1.0f, 0.9f, 0.3f, 1.0f},  // bright yellow
        .gravity         = {0.0f, 0.0f, -5.0f},         // mild gravity
        .damping         = 0.0f,                          // NO linear damping — drag only
        .softFadeDistance = 0.3f,
        .emissiveIntensity = 1.0f,
        .sizeOverLifeStart    = 0.8f,
        .sizeOverLifePeak     = 1.2f,
        .sizeOverLifeEnd      = 0.3f,
        .sizeOverLifePeakTime = 0.15f,
        .colorLifeStartR = 1.0f, .colorLifeStartG = 0.9f, .colorLifeStartB = 0.3f, .colorLifeStartA = 1.0f,  // fast = bright yellow
        .colorLifeEndR   = 0.6f, .colorLifeEndG   = 0.1f, .colorLifeEndB   = 0.0f, .colorLifeEndA   = 0.3f,  // slow = dark red
        .facingMode      = 0,
        .stretchFactor   = 0.0f,
        .blendMode       = 1,   // Additive
        .dragCoefficient = 3.0f, // high quadratic drag — particles decelerate rapidly
        .colorBySpeedEnabled = 1, // color-by-speed ON — fast=start color, slow=end color
    }, 500.0f },

    // ── Vector Flow preset (Task 4.15.0) ──────────────────────────────────
    // Slow-moving particles influenced by a procedural 3D noise vector field.
    // Low initial velocity + long lifetime lets the field drive organic flow.
    // Low tightness (0.3) means the field steers without fully replacing momentum.
    // @ref Stam 1999 "Stable Fluids" — vector field advection fundamentals
    // @ref Bridson 2007 "Fluid Simulation for Computer Graphics" — velocity field
    { "Vector Flow", VFXEmitterConfig{
        .position        = {0.0f, 0.0f, 0.0f},
        .spawnRadius     = 1.0f,           // wide spawn area to showcase field variation
        .spawnVelocity   = {0.0f, 0.0f, 1.0f},  // minimal initial upward velocity
        .velocityVariation = 0.5f,
        .spreadAngle     = 3.14159f,       // full hemisphere spawn
        .minLifetime     = 3.0f,
        .maxLifetime     = 6.0f,
        .initialSize     = 0.15f,
        .initialColor    = {0.3f, 0.7f, 1.0f, 0.8f},  // cool blue
        .gravity         = {0.0f, 0.0f, 0.0f},          // no gravity — pure field motion
        .damping         = 0.05f,                         // very light damping
        .softFadeDistance = 0.3f,
        .emissiveIntensity = 0.5f,
        .sizeOverLifeStart    = 0.5f,
        .sizeOverLifePeak     = 1.0f,
        .sizeOverLifeEnd      = 0.0f,
        .sizeOverLifePeakTime = 0.3f,
        .colorLifeStartR = 0.3f, .colorLifeStartG = 0.7f, .colorLifeStartB = 1.0f, .colorLifeStartA = 0.9f,
        .colorLifeEndR   = 0.1f, .colorLifeEndG   = 0.3f, .colorLifeEndB   = 0.8f, .colorLifeEndA   = 0.0f,
        .facingMode      = 0,
        .stretchFactor   = 0.0f,
        .blendMode       = 0,   // AlphaBlend — soft translucent flow
        .vectorFieldEnabled   = 1,
        .vectorFieldStrength  = 5.0f,   // moderate field influence
        .vectorFieldTightness = 0.3f,   // mostly additive, slight direction lock
        .vectorFieldScale     = 0.5f,   // large-scale flow structures
    }, 200.0f },
};

const VFXPreset* GetVFXPresets(int& outCount) {
    outCount = static_cast<int>(sizeof(s_presets) / sizeof(s_presets[0]));
    return s_presets;
}

// ─────────────────────────────────────────────────────────────────────────────
// Channel Stride
// ─────────────────────────────────────────────────────────────────────────────

uint32_t VFXParticleSystem::ChannelStride(uint32_t channel) {
    // Channels: 0=positions(float4=16), 1=velocities(float4=16), 2=ages(float=4),
    //           3=lifespans(float=4), 4=sizes(float=4), 5=colors(float4=16), 6=rotations(float=4),
    //           7=dynamicParams(float4=16)
    switch (channel) {
        case 0: return 16; // float4 positions (xyz + padding)
        case 1: return 16; // float4 velocities (xyz + padding)
        case 2: return 4;  // float ages
        case 3: return 4;  // float lifespans
        case 4: return 4;  // float sizes
        case 5: return 16; // float4 colors (rgba)
        case 6: return 4;  // float rotations
        case 7: return 16; // float4 dynamicParams (Task 4.14.0)
        default: return 4;
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Buffer Creation Helper
// ─────────────────────────────────────────────────────────────────────────────

RHIBufferHandle VFXParticleSystem::CreateParticleBuffer(IRHIDevice* device,
                                                          uint32_t stride,
                                                          const char* name) {
    RHIBufferDesc desc{};
    desc.sizeBytes = static_cast<uint64_t>(m_maxParticles) * stride;
    desc.stride = stride;
    desc.usage = RHIBufferUsage::StructuredBuffer | RHIBufferUsage::UnorderedAccess;
    desc.memory = RHIMemoryType::Default;
    desc.debugName = name;
    return device->CreateBuffer(desc);
}

// ─────────────────────────────────────────────────────────────────────────────
// Init
// ─────────────────────────────────────────────────────────────────────────────

bool VFXParticleSystem::Init(IRHIDevice* device, ShaderManager* shaderMgr,
                              uint32_t maxParticles) {
    m_device = device;
    m_maxParticles = maxParticles;
    m_pingPong = 0;

    // Channel names for debug labels
    static const char* kChannelNames[] = {
        "VFX_Positions", "VFX_Velocities", "VFX_Ages",
        "VFX_Lifespans", "VFX_Sizes", "VFX_Colors", "VFX_Rotations",
        "VFX_DynamicParams"
    };

    // Create SoA ping-pong buffers
    for (uint32_t ch = 0; ch < kChannelCount; ++ch) {
        const uint32_t stride = ChannelStride(ch);
        for (int pp = 0; pp < 2; ++pp) {
            m_particleBuffers[ch][pp] = CreateParticleBuffer(device, stride, kChannelNames[ch]);
            if (!m_particleBuffers[ch][pp].IsValid()) {
                UZ_LOG(App, Error, "VFX: Failed to create particle buffer ch={} pp={}", ch, pp);
                return false;
            }
        }
    }

    // Alive count buffer (single uint32)
    {
        RHIBufferDesc desc{};
        desc.sizeBytes = sizeof(uint32_t);
        desc.stride = sizeof(uint32_t);
        desc.usage = RHIBufferUsage::StructuredBuffer | RHIBufferUsage::UnorderedAccess;
        desc.memory = RHIMemoryType::Default;
        desc.debugName = "VFX_AliveCount";
        m_aliveCount = device->CreateBuffer(desc);
    }

    // Free list buffer (one uint32 per particle slot)
    {
        RHIBufferDesc desc{};
        desc.sizeBytes = static_cast<uint64_t>(maxParticles) * sizeof(uint32_t);
        desc.stride = sizeof(uint32_t);
        desc.usage = RHIBufferUsage::StructuredBuffer | RHIBufferUsage::UnorderedAccess;
        desc.memory = RHIMemoryType::Default;
        desc.debugName = "VFX_FreeList";
        m_freeList = device->CreateBuffer(desc);
    }

    // Free list head (single uint32)
    {
        RHIBufferDesc desc{};
        desc.sizeBytes = sizeof(uint32_t);
        desc.stride = sizeof(uint32_t);
        desc.usage = RHIBufferUsage::StructuredBuffer | RHIBufferUsage::UnorderedAccess;
        desc.memory = RHIMemoryType::Default;
        desc.debugName = "VFX_FreeListHead";
        m_freeListHead = device->CreateBuffer(desc);
    }

    // Indirect args buffer: DrawIndexedIndirect args (6 x uint32 = 24 bytes)
    // Format: [vertexCountPerInstance, instanceCount, startIndex, baseVertex, startInstance, padding]
    {
        RHIBufferDesc desc{};
        desc.sizeBytes = 6 * sizeof(uint32_t);
        desc.stride = sizeof(uint32_t);
        desc.usage = RHIBufferUsage::IndirectArgs | RHIBufferUsage::UnorderedAccess
                   | RHIBufferUsage::StructuredBuffer;
        desc.memory = RHIMemoryType::Default;
        desc.debugName = "VFX_IndirectArgs";
        m_indirectArgs = device->CreateBuffer(desc);
    }

    // Sort buffers
    {
        RHIBufferDesc desc{};
        desc.sizeBytes = static_cast<uint64_t>(maxParticles) * sizeof(float);
        desc.stride = sizeof(float);
        desc.usage = RHIBufferUsage::StructuredBuffer | RHIBufferUsage::UnorderedAccess;
        desc.memory = RHIMemoryType::Default;
        desc.debugName = "VFX_SortKeys";
        m_sortKeys = device->CreateBuffer(desc);
    }
    {
        RHIBufferDesc desc{};
        desc.sizeBytes = static_cast<uint64_t>(maxParticles) * sizeof(uint32_t);
        desc.stride = sizeof(uint32_t);
        desc.usage = RHIBufferUsage::StructuredBuffer | RHIBufferUsage::UnorderedAccess;
        desc.memory = RHIMemoryType::Default;
        desc.debugName = "VFX_SortIndices";
        m_sortIndices = device->CreateBuffer(desc);
    }

    // Wind zone GPU buffer (StructuredBuffer<WindZone>[kMaxWindZones])
    {
        RHIBufferDesc desc{};
        desc.sizeBytes = static_cast<uint64_t>(kMaxWindZones) * sizeof(WindZone);
        desc.stride    = sizeof(WindZone);
        desc.usage     = RHIBufferUsage::StructuredBuffer | RHIBufferUsage::CopyDst;
        desc.memory    = RHIMemoryType::Default;
        desc.debugName = "VFX_WindZones";
        m_windZoneBuffer = device->CreateBuffer(desc);
        if (!m_windZoneBuffer.IsValid()) {
            UZ_LOG(App, Error, "VFX: Failed to create wind zone buffer");
            return false;
        }
        m_windZoneSrvIndex = device->GetBufferSRVIndex(m_windZoneBuffer);
    }

    // Kill volume GPU buffer (StructuredBuffer<KillVolume>[kMaxKillVolumes])
    {
        RHIBufferDesc desc{};
        desc.sizeBytes = static_cast<uint64_t>(kMaxKillVolumes) * sizeof(KillVolume);
        desc.stride    = sizeof(KillVolume);
        desc.usage     = RHIBufferUsage::StructuredBuffer | RHIBufferUsage::CopyDst;
        desc.memory    = RHIMemoryType::Default;
        desc.debugName = "VFX_KillVolumes";
        m_killVolumeBuffer = device->CreateBuffer(desc);
        if (!m_killVolumeBuffer.IsValid()) {
            UZ_LOG(App, Error, "VFX: Failed to create kill volume buffer");
            return false;
        }
        m_killVolumeSrvIndex = device->GetBufferSRVIndex(m_killVolumeBuffer);
    }

    // Death event buffer (RWStructuredBuffer<DeathEvent>[VFX_MAX_DEATH_EVENTS])
    // Written by the update CS on particle death; read by the death spawn CS.
    // @ref Shopf 2008 "GPU Particles" — death event buffer for sub-emitter spawning
    {
        RHIBufferDesc desc{};
        desc.sizeBytes = static_cast<uint64_t>(VFX_MAX_DEATH_EVENTS) * sizeof(DeathEvent);
        desc.stride    = sizeof(DeathEvent);
        desc.usage     = RHIBufferUsage::StructuredBuffer | RHIBufferUsage::UnorderedAccess;
        desc.memory    = RHIMemoryType::Default;
        desc.debugName = "VFX_DeathEventBuffer";
        m_deathEventBuffer = device->CreateBuffer(desc);
        if (!m_deathEventBuffer.IsValid()) {
            UZ_LOG(App, Error, "VFX: Failed to create death event buffer");
            return false;
        }
        m_deathEventBufferUavIndex = device->GetBufferUAVIndex(m_deathEventBuffer);
        m_deathEventBufferSrvIndex = device->GetBufferSRVIndex(m_deathEventBuffer);
    }

    // Death event count buffer (RWBuffer<uint>, 1 element, atomic counter).
    // Reset to 0 each frame via CopyBuffer from m_deathEventCountZeroStaging.
    {
        RHIBufferDesc desc{};
        desc.sizeBytes = sizeof(uint32_t);
        desc.stride    = sizeof(uint32_t);
        desc.usage     = RHIBufferUsage::StructuredBuffer | RHIBufferUsage::UnorderedAccess;
        desc.memory    = RHIMemoryType::Default;
        desc.debugName = "VFX_DeathEventCount";
        m_deathEventCountBuffer = device->CreateBuffer(desc);
        if (!m_deathEventCountBuffer.IsValid()) {
            UZ_LOG(App, Error, "VFX: Failed to create death event count buffer");
            return false;
        }
        m_deathEventCountUavIndex = device->GetBufferUAVIndex(m_deathEventCountBuffer);
        m_deathEventCountSrvIndex = device->GetBufferSRVIndex(m_deathEventCountBuffer);

        // Persistent zero staging buffer for per-frame atomic counter reset.
        // Created once; CopyBuffer(zero_staging -> death_event_count) each frame.
        RHIBufferDesc zeroDesc{};
        zeroDesc.sizeBytes = sizeof(uint32_t);
        zeroDesc.stride    = sizeof(uint32_t);
        zeroDesc.usage     = RHIBufferUsage::StructuredBuffer;
        zeroDesc.memory    = RHIMemoryType::Upload;
        zeroDesc.debugName = "VFX_DeathEventCount_ZeroStaging";
        m_deathEventCountZeroStaging = device->CreateBuffer(zeroDesc);
        if (!m_deathEventCountZeroStaging.IsValid()) {
            UZ_LOG(App, Error, "VFX: Failed to create death event count zero staging buffer");
            return false;
        }
        // Write zero into staging — stays mapped permanently (upload heap).
        void* mapped = device->MapBuffer(m_deathEventCountZeroStaging);
        if (mapped) {
            *static_cast<uint32_t*>(mapped) = 0u;
            // Do NOT unmap upload-heap buffers used as persistent staging — RHI keeps
            // them CPU-visible for the duration of the buffer's lifetime.
            device->UnmapBuffer(m_deathEventCountZeroStaging);
        }
    }

    // Compile compute pipelines via RHIComputePipelineDesc + CreateComputePipeline
    {
        RHIComputePipelineDesc desc{};
        desc.computeShader = shaderMgr->CreateShader(EngineShader::ParticleSpawnCS, RHIShaderStage::Compute);
        desc.debugName = "VFX_SpawnCS";
        m_spawnPSO = device->CreateComputePipeline(desc);
        if (!m_spawnPSO.IsValid()) {
            UZ_LOG(App, Error, "VFX: Failed to create spawn compute PSO");
            return false;
        }
    }
    {
        RHIComputePipelineDesc desc{};
        desc.computeShader = shaderMgr->CreateShader(EngineShader::ParticleUpdateCS, RHIShaderStage::Compute);
        desc.debugName = "VFX_UpdateCS";
        m_updatePSO = device->CreateComputePipeline(desc);
        if (!m_updatePSO.IsValid()) {
            UZ_LOG(App, Error, "VFX: Failed to create update compute PSO");
            return false;
        }
    }
    {
        RHIComputePipelineDesc desc{};
        desc.computeShader = shaderMgr->CreateShader(EngineShader::ParticleIndirectArgsCS, RHIShaderStage::Compute);
        desc.debugName = "VFX_IndirectArgsCS";
        m_indirectArgsPSO = device->CreateComputePipeline(desc);
        if (!m_indirectArgsPSO.IsValid()) {
            UZ_LOG(App, Error, "VFX: Failed to create indirect args compute PSO");
            return false;
        }
    }
    {
        RHIComputePipelineDesc desc{};
        desc.computeShader = shaderMgr->CreateShader(EngineShader::ParticleSortCS, RHIShaderStage::Compute);
        desc.debugName = "VFX_SortCS";
        m_sortPSO = device->CreateComputePipeline(desc);
        if (!m_sortPSO.IsValid()) {
            UZ_LOG(App, Error, "VFX: Failed to create sort compute PSO");
            return false;
        }
    }
    {
        // Death spawn CS: secondary spawn pass for sub-emitter children on particle death.
        // @ref Shopf 2008 "GPU Particles" — death event driven sub-emitter spawn
        RHIComputePipelineDesc desc{};
        desc.computeShader = shaderMgr->CreateShader(EngineShader::ParticleDeathSpawnCS, RHIShaderStage::Compute);
        desc.debugName = "VFX_DeathSpawnCS";
        m_deathSpawnPSO = device->CreateComputePipeline(desc);
        if (!m_deathSpawnPSO.IsValid()) {
            UZ_LOG(App, Error, "VFX: Failed to create death spawn compute PSO");
            return false;
        }
    }

    // Billboard graphics pipeline: alpha blend, no depth write, back-face cull off
    {
        RHIGraphicsPipelineDesc desc{};
        desc.vertexShader = shaderMgr->CreateShader(EngineShader::ParticleBillboardVS, RHIShaderStage::Vertex);
        desc.pixelShader = shaderMgr->CreateShader(EngineShader::ParticleBillboardPS, RHIShaderStage::Pixel);
        desc.renderTargetCount = 1;
        desc.renderTargetFormats[0] = RHIFormat::RGBA16Float; // HDR target
        desc.depthStencilFormat = RHIFormat::D32Float;
        desc.depthStencil.depthEnable = true;
        desc.depthStencil.depthWrite = false; // Particles don't write depth
        // depthFunc defaults to Greater (reversed-Z)
        desc.rasterizer.cullMode = RHICullMode::None;
        // AlphaComposite / PremulAlpha (blend mode 2): One / InvSrcAlpha
        // The pixel shader outputs premultiplied RGB (rgb *= opacity before return).
        // SrcAlpha=Zero, DstAlpha=InvSrcAlpha preserves correct destination alpha
        // compositing (Niagara exact match for alpha channel handling).
        desc.renderTargetBlend[0].blendEnable = true;
        desc.renderTargetBlend[0].srcColor = RHIBlendFactor::One;
        desc.renderTargetBlend[0].dstColor = RHIBlendFactor::InvSrcAlpha;
        desc.renderTargetBlend[0].colorOp = RHIBlendOp::Add;
        desc.renderTargetBlend[0].srcAlpha = RHIBlendFactor::Zero;
        desc.renderTargetBlend[0].dstAlpha = RHIBlendFactor::InvSrcAlpha;
        desc.renderTargetBlend[0].alphaOp = RHIBlendOp::Add;
        // No input layout — billboard VS reads from structured buffers via SV_VertexID
        desc.inputElementCount = 0;
        desc.debugName = "VFX_BillboardPSO";
        m_billboardPSO = device->CreateGraphicsPipeline(desc);
        if (!m_billboardPSO.IsValid()) {
            UZ_LOG(App, Error, "VFX: Failed to create billboard graphics PSO (PremulAlpha)");
            return false;
        }

        // Additive PSO (blend mode 1): One/One — pure additive for fire, sparks, glow.
        // RGB adds directly to the framebuffer. Alpha: Zero/InvSrcAlpha preserves
        // destination alpha correctly (Niagara exact match).
        desc.renderTargetBlend[0].srcColor = RHIBlendFactor::One;
        desc.renderTargetBlend[0].dstColor = RHIBlendFactor::One;
        desc.renderTargetBlend[0].srcAlpha = RHIBlendFactor::Zero;
        desc.renderTargetBlend[0].dstAlpha = RHIBlendFactor::InvSrcAlpha;
        desc.debugName = "VFX_BillboardPSO_Additive";
        m_billboardPSO_Additive = device->CreateGraphicsPipeline(desc);
        if (!m_billboardPSO_Additive.IsValid()) {
            UZ_LOG(App, Error, "VFX: Failed to create billboard graphics PSO (Additive)");
            return false;
        }

        // Translucent / AlphaBlend PSO (blend mode 0): SrcAlpha/InvSrcAlpha — standard
        // transparency for smoke, dust, soft translucent particles. The blend state
        // multiplies RGB by alpha automatically. Alpha: Zero/InvSrcAlpha preserves
        // destination alpha correctly (Niagara exact match).
        desc.renderTargetBlend[0].srcColor = RHIBlendFactor::SrcAlpha;
        desc.renderTargetBlend[0].dstColor = RHIBlendFactor::InvSrcAlpha;
        desc.renderTargetBlend[0].srcAlpha = RHIBlendFactor::Zero;
        desc.renderTargetBlend[0].dstAlpha = RHIBlendFactor::InvSrcAlpha;
        desc.debugName = "VFX_BillboardPSO_AlphaBlend";
        m_billboardPSO_AlphaBlend = device->CreateGraphicsPipeline(desc);
        if (!m_billboardPSO_AlphaBlend.IsValid()) {
            UZ_LOG(App, Error, "VFX: Failed to create billboard graphics PSO (AlphaBlend)");
            return false;
        }

        // Lit PSO removed — lit mode uses the standard PS with litMode CB flag.
    }

    UZ_LOG(App, Info, "VFX: Initialized particle system — maxParticles={}", maxParticles);
    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// Upload Initial Free List
// ─────────────────────────────────────────────────────────────────────────────

void VFXParticleSystem::UploadInitialFreeList(IRHICommandList* cmd) {
    // Free list: array of [0, 1, 2, ..., maxParticles-1] — every slot starts free
    {
        const uint64_t sizeBytes = static_cast<uint64_t>(m_maxParticles) * sizeof(uint32_t);
        RHIBufferDesc desc{};
        desc.sizeBytes = sizeBytes;
        desc.stride = sizeof(uint32_t);
        desc.usage = RHIBufferUsage::StructuredBuffer;
        desc.memory = RHIMemoryType::Upload;
        desc.debugName = "VFX_FreeList_Staging";
        m_freeListStaging = m_device->CreateBuffer(desc);

        void* mapped = m_device->MapBuffer(m_freeListStaging);
        if (mapped) {
            auto* indices = static_cast<uint32_t*>(mapped);
            for (uint32_t i = 0; i < m_maxParticles; ++i) {
                indices[i] = i;
            }
            m_device->UnmapBuffer(m_freeListStaging);
        }
        cmd->CopyBuffer(m_freeListStaging, 0, m_freeList, 0, sizeBytes);
    }

    // Free list head: initial value = maxParticles (all slots are free)
    {
        RHIBufferDesc desc{};
        desc.sizeBytes = sizeof(uint32_t);
        desc.stride = sizeof(uint32_t);
        desc.usage = RHIBufferUsage::StructuredBuffer;
        desc.memory = RHIMemoryType::Upload;
        desc.debugName = "VFX_FreeListHead_Staging";
        m_freeListHeadStaging = m_device->CreateBuffer(desc);

        void* mapped = m_device->MapBuffer(m_freeListHeadStaging);
        if (mapped) {
            auto* head = static_cast<uint32_t*>(mapped);
            *head = m_maxParticles;
            m_device->UnmapBuffer(m_freeListHeadStaging);
        }
        cmd->CopyBuffer(m_freeListHeadStaging, 0, m_freeListHead, 0, sizeof(uint32_t));
    }

    // Alive count: initial value = 0 (no particles alive yet)
    {
        RHIBufferDesc desc{};
        desc.sizeBytes = sizeof(uint32_t);
        desc.stride = sizeof(uint32_t);
        desc.usage = RHIBufferUsage::StructuredBuffer;
        desc.memory = RHIMemoryType::Upload;
        desc.debugName = "VFX_AliveCount_Staging";
        m_aliveCountStaging = m_device->CreateBuffer(desc);

        void* mapped = m_device->MapBuffer(m_aliveCountStaging);
        if (mapped) {
            auto* count = static_cast<uint32_t*>(mapped);
            *count = 0;
            m_device->UnmapBuffer(m_aliveCountStaging);
        }
        cmd->CopyBuffer(m_aliveCountStaging, 0, m_aliveCount, 0, sizeof(uint32_t));
    }

    // Zero-initialize ALL particle attribute buffers (both ping-pong sets).
    // GPU Default memory has undefined contents — if the update shader reads
    // uninitialized data on frame 0, garbage lifespans could make particles
    // appear "alive" and corrupt the alive count. With all-zero data:
    // age=0, lifespan=0 → age >= lifespan → all particles are dead (correct).
    {
        // Find largest channel size for a reusable staging buffer
        uint64_t maxChannelSize = 0;
        for (uint32_t ch = 0; ch < kChannelCount; ++ch) {
            const uint64_t chSize = static_cast<uint64_t>(m_maxParticles) * ChannelStride(ch);
            if (chSize > maxChannelSize) maxChannelSize = chSize;
        }

        RHIBufferDesc desc{};
        desc.sizeBytes = maxChannelSize;
        desc.stride = sizeof(uint32_t);
        desc.usage = RHIBufferUsage::StructuredBuffer;
        desc.memory = RHIMemoryType::Upload;
        desc.debugName = "VFX_ZeroInit_Staging";
        m_zeroInitStaging = m_device->CreateBuffer(desc);

        void* mapped = m_device->MapBuffer(m_zeroInitStaging);
        if (mapped) {
            std::memset(mapped, 0, maxChannelSize);
            m_device->UnmapBuffer(m_zeroInitStaging);
        }

        for (uint32_t ch = 0; ch < kChannelCount; ++ch) {
            const uint64_t chSize = static_cast<uint64_t>(m_maxParticles) * ChannelStride(ch);
            for (int pp = 0; pp < 2; ++pp) {
                cmd->CopyBuffer(m_zeroInitStaging, 0,
                                m_particleBuffers[ch][pp], 0, chSize);
            }
        }
    }

    UZ_LOG(App, Info, "VFX: Uploaded initial free list — {} slots available", m_maxParticles);
}

// ─────────────────────────────────────────────────────────────────────────────
// Shutdown
// ─────────────────────────────────────────────────────────────────────────────

void VFXParticleSystem::Shutdown(IRHIDevice* device) {
    if (!device) return;

    for (uint32_t ch = 0; ch < kChannelCount; ++ch) {
        for (int pp = 0; pp < 2; ++pp) {
            if (m_particleBuffers[ch][pp].IsValid()) {
                device->DestroyBuffer(m_particleBuffers[ch][pp]);
                m_particleBuffers[ch][pp] = {};
            }
        }
    }

    auto destroyBuf = [device](RHIBufferHandle& h) {
        if (h.IsValid()) { device->DestroyBuffer(h); h = {}; }
    };
    destroyBuf(m_aliveCount);
    destroyBuf(m_freeList);
    destroyBuf(m_freeListHead);
    destroyBuf(m_indirectArgs);
    destroyBuf(m_sortKeys);
    destroyBuf(m_sortIndices);
    destroyBuf(m_freeListStaging);
    destroyBuf(m_freeListHeadStaging);
    destroyBuf(m_aliveCountStaging);
    destroyBuf(m_zeroInitStaging);
    destroyBuf(m_windZoneBuffer);
    destroyBuf(m_killVolumeBuffer);
    destroyBuf(m_deathEventBuffer);
    destroyBuf(m_deathEventCountBuffer);
    destroyBuf(m_deathEventCountZeroStaging);

    auto destroyPso = [device](RHIPipelineHandle& h) {
        if (h.IsValid()) { device->DestroyPipeline(h); h = {}; }
    };
    destroyPso(m_spawnPSO);
    destroyPso(m_updatePSO);
    destroyPso(m_indirectArgsPSO);
    destroyPso(m_sortPSO);
    destroyPso(m_deathSpawnPSO);
    destroyPso(m_billboardPSO);
    destroyPso(m_billboardPSO_Additive);
    destroyPso(m_billboardPSO_AlphaBlend);
    // m_billboardPSO_Lit removed — lit mode uses standard PSO with CB flag.

    // Destroy color ramp LUT texture
    if (m_colorRampLUT.IsValid()) {
        device->DestroyTexture(m_colorRampLUT);
        m_colorRampLUT = {};
        m_colorRampSrvIndex = UINT32_MAX;
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Upload Wind Zones
// ─────────────────────────────────────────────────────────────────────────────

/// @ref Stam 1999 "Stable Fluids" -- wind field accumulation
void VFXParticleSystem::UploadWindZones(IRHICommandList* cmd, CBRingAllocator* cbRing,
                                         const WindZone* zones, uint32_t count) {
    // Clamp to the GPU buffer capacity
    count = std::min(count, kMaxWindZones);
    m_activeWindZones = count;

    if (count == 0u || !m_windZoneBuffer.IsValid()) {
        return;
    }

    // Allocate a per-frame staging region from the CB ring allocator.
    // The ring allocator is backed by upload-heap memory (CPU-visible + GPU-readable),
    // so we write zone data directly and issue a CopyBuffer to the Default-heap buffer.
    const uint32_t uploadBytes = count * sizeof(WindZone);
    auto alloc = cbRing->Allocate(uploadBytes);
    std::memcpy(alloc.cpuPtr, zones, uploadBytes);

    // Copy from staging (upload heap, cb ring) to GPU wind zone buffer (Default heap).
    // The command list must be in a state that allows copy commands (pre-render-pass).
    cmd->CopyBuffer(alloc.bufferHandle, alloc.offsetBytes, m_windZoneBuffer, 0, uploadBytes);
}

// ─────────────────────────────────────────────────────────────────────────────
// Upload Kill Volumes
// ─────────────────────────────────────────────────────────────────────────────

/// @ref Ericson 2005 "Real-Time Collision Detection" Ch.4
void VFXParticleSystem::UploadKillVolumes(IRHICommandList* cmd, CBRingAllocator* cbRing,
                                           const KillVolume* volumes, uint32_t count) {
    // Clamp to the GPU buffer capacity
    count = std::min(count, kMaxKillVolumes);
    m_activeKillVolumes = count;

    if (count == 0u || !m_killVolumeBuffer.IsValid()) {
        return;
    }

    const uint32_t uploadBytes = count * sizeof(KillVolume);
    auto alloc = cbRing->Allocate(uploadBytes);
    std::memcpy(alloc.cpuPtr, volumes, uploadBytes);

    cmd->CopyBuffer(alloc.bufferHandle, alloc.offsetBytes, m_killVolumeBuffer, 0, uploadBytes);
}

// ─────────────────────────────────────────────────────────────────────────────
// Dispatch Compute
// ─────────────────────────────────────────────────────────────────────────────

void VFXParticleSystem::DispatchCompute(IRHICommandList* cmdList, CBRingAllocator* cbRing,
                                         const VFXEmitterConfig& config, uint32_t spawnCount,
                                         float dt, uint32_t frameNumber, float totalTime) {
    const int read = m_pingPong;
    const int write = 1 - m_pingPong;

    // ── Reset alive count to 0 before this frame's simulation ─────────
    // Both spawn and update atomically increment aliveCount. Without a
    // per-frame reset, the counter grows without bound and the free list
    // eventually exhausts (particles stop spawning).
    if (m_aliveCountStaging.IsValid()) {
        cmdList->CopyBuffer(m_aliveCountStaging, 0, m_aliveCount, 0, sizeof(uint32_t));
    }

    // ── Reset death event counter to 0 (per-frame) ─────────────────────
    // The update CS atomically increments this counter when writing death events.
    // Must be reset BEFORE the update dispatch so the counter starts at 0 each frame.
    // Uses the persistent zero staging buffer (upload heap, CPU-mapped once in Init).
    // @ref Shopf 2008 "GPU Particles" — per-frame event buffer reset via copy
    if (m_deathEventCountBuffer.IsValid() && m_deathEventCountZeroStaging.IsValid()) {
        cmdList->CopyBuffer(m_deathEventCountZeroStaging, 0,
                            m_deathEventCountBuffer, 0, sizeof(uint32_t));
    }

    // ── UAV barriers before compute ──────────────────────────────────────
    // Transition all write-set buffers to UAV state. Read-set already in SRV
    // from the previous frame's billboard draw.
    for (uint32_t ch = 0; ch < kChannelCount; ++ch) {
        RHIBarrierDesc barrier{};
        barrier.buffer = m_particleBuffers[ch][write];
        barrier.stateBefore = RHIResourceState::Common;
        barrier.stateAfter = RHIResourceState::UnorderedAccess;
        cmdList->ResourceBarrier(&barrier, 1);
    }

    // Transition aliveCount to UAV after the copy
    {
        RHIBarrierDesc barrier{};
        barrier.buffer = m_aliveCount;
        barrier.stateBefore = RHIResourceState::CopyDst;
        barrier.stateAfter = RHIResourceState::UnorderedAccess;
        cmdList->ResourceBarrier(&barrier, 1);
    }

    // Transition deathEventCountBuffer to UAV after the zero-reset copy.
    // The update CS writes to it via InterlockedAdd; must be in UAV state.
    if (m_deathEventCountBuffer.IsValid()) {
        RHIBarrierDesc barrier{};
        barrier.buffer = m_deathEventCountBuffer;
        barrier.stateBefore = RHIResourceState::CopyDst;
        barrier.stateAfter = RHIResourceState::UnorderedAccess;
        cmdList->ResourceBarrier(&barrier, 1);
    }

    // ── Spawn Pass ───────────────────────────────────────────────────────
    if (spawnCount > 0) {
        VFXSpawnCB cb{};
        std::memcpy(cb.emitterPos, config.position, sizeof(float) * 3);
        cb.spawnRadius = config.spawnRadius;
        std::memcpy(cb.spawnVelocity, config.spawnVelocity, sizeof(float) * 3);
        cb.velocityVariation = config.velocityVariation;
        cb.spreadAngle = config.spreadAngle;
        cb.minLifetime = config.minLifetime;
        cb.maxLifetime = config.maxLifetime;
        cb.initialSize = config.initialSize;
        std::memcpy(cb.initialColor, config.initialColor, sizeof(float) * 4);
        cb.dt = dt;
        cb.frameNumber = frameNumber;
        cb.spawnCount = spawnCount;
        cb.maxParticles = m_maxParticles;

        // Bindless indices for write-set buffers
        cb.freeListSrvIndex = m_device->GetBufferSRVIndex(m_freeList);
        cb.positionsUavIndex = m_device->GetBufferUAVIndex(m_particleBuffers[0][write]);
        cb.velocitiesUavIndex = m_device->GetBufferUAVIndex(m_particleBuffers[1][write]);
        cb.agesUavIndex = m_device->GetBufferUAVIndex(m_particleBuffers[2][write]);
        cb.lifespansUavIndex = m_device->GetBufferUAVIndex(m_particleBuffers[3][write]);
        cb.sizesUavIndex = m_device->GetBufferUAVIndex(m_particleBuffers[4][write]);
        cb.colorsUavIndex = m_device->GetBufferUAVIndex(m_particleBuffers[5][write]);
        cb.rotationsUavIndex = m_device->GetBufferUAVIndex(m_particleBuffers[6][write]);
        cb.freeListHeadUavIndex = m_device->GetBufferUAVIndex(m_freeListHead);
        cb.aliveCountUavIndex = m_device->GetBufferUAVIndex(m_aliveCount);

        // Spawn shape parameters
        cb.spawnShape = config.spawnShape;
        cb.spawnOnSurface = config.spawnOnSurface;
        cb.shapeParam1 = config.shapeParam1;
        cb.shapeParam2 = config.shapeParam2;
        cb.shapeParam3 = config.shapeParam3;

        // Initial rotation range
        cb.initialRotationMin = config.initialRotationMin;
        cb.initialRotationMax = config.initialRotationMax;

        // Velocity inheritance — emitter velocity is pre-computed by the caller
        // (OnFrame) and stored in config.emitterVelocity before DispatchCompute.
        // The spawn shader adds emitterVelocity * velocityInheritance to spawn vel.
        cb.emitterVelocityX    = config.emitterVelocity[0];
        cb.emitterVelocityY    = config.emitterVelocity[1];
        cb.emitterVelocityZ    = config.emitterVelocity[2];
        cb.velocityInheritance = config.velocityInheritance;

        // Dynamic material parameters (Task 4.14.0)
        cb.dynamicParamsUavIndex = m_device->GetBufferUAVIndex(m_particleBuffers[7][write]);
        cb.defaultDynParam0      = config.dynamicParam0;
        cb.defaultDynParam1      = config.dynamicParam1;
        cb.defaultDynParam2      = config.dynamicParam2;
        cb.defaultDynParam3      = config.dynamicParam3;

        auto alloc = cbRing->Allocate(sizeof(VFXSpawnCB));
        std::memcpy(alloc.cpuPtr, &cb, sizeof(VFXSpawnCB));

        cmdList->SetPipeline(m_spawnPSO);
        cmdList->SetRootCBV(1, alloc.gpuAddr); // b1 = root param 1 (CBV)
        cmdList->Dispatch((spawnCount + kSpawnGroupSize - 1) / kSpawnGroupSize, 1, 1);
    }

    // ── UAV barrier between spawn and update ────────────────────────────
    {
        RHIBarrierDesc barrier{};
        barrier.buffer = m_aliveCount;
        barrier.stateBefore = RHIResourceState::UnorderedAccess;
        barrier.stateAfter = RHIResourceState::UnorderedAccess;
        cmdList->ResourceBarrier(&barrier, 1);
    }

    // ── Update Pass ──────────────────────────────────────────────────────
    {
        VFXUpdateCB cb{};
        std::memcpy(cb.gravity, config.gravity, sizeof(float) * 3);
        cb.dt = dt;
        cb.damping = config.damping;
        cb.aliveCount = m_maxParticles; // Use maxParticles as upper bound
        cb.frameNumber = frameNumber;
        cb.maxParticles = m_maxParticles;

        // Read-set SRV indices
        cb.readPositionsSrv = m_device->GetBufferSRVIndex(m_particleBuffers[0][read]);
        cb.readVelocitiesSrv = m_device->GetBufferSRVIndex(m_particleBuffers[1][read]);
        cb.readAgesSrv = m_device->GetBufferSRVIndex(m_particleBuffers[2][read]);
        cb.readLifespansSrv = m_device->GetBufferSRVIndex(m_particleBuffers[3][read]);
        cb.readSizesSrv = m_device->GetBufferSRVIndex(m_particleBuffers[4][read]);
        cb.readColorsSrv = m_device->GetBufferSRVIndex(m_particleBuffers[5][read]);
        cb.readRotationsSrv = m_device->GetBufferSRVIndex(m_particleBuffers[6][read]);

        // Write-set UAV indices
        cb.writePositionsUav = m_device->GetBufferUAVIndex(m_particleBuffers[0][write]);
        cb.writeVelocitiesUav = m_device->GetBufferUAVIndex(m_particleBuffers[1][write]);
        cb.writeAgesUav = m_device->GetBufferUAVIndex(m_particleBuffers[2][write]);
        cb.writeLifespansUav = m_device->GetBufferUAVIndex(m_particleBuffers[3][write]);
        cb.writeSizesUav = m_device->GetBufferUAVIndex(m_particleBuffers[4][write]);
        cb.writeColorsUav = m_device->GetBufferUAVIndex(m_particleBuffers[5][write]);
        cb.writeRotationsUav = m_device->GetBufferUAVIndex(m_particleBuffers[6][write]);

        // Atomic buffer UAV indices
        cb.aliveCountUavIndex = m_device->GetBufferUAVIndex(m_aliveCount);
        cb.freeListUavIndex = m_device->GetBufferUAVIndex(m_freeList);
        cb.freeListHeadUavIndex = m_device->GetBufferUAVIndex(m_freeListHead);

        // Curl noise force parameters
        cb.curlStrength = config.curlStrength;
        cb.curlFrequency = config.curlFrequency;
        cb.curlSpeed = config.curlSpeed;
        cb.totalTime = totalTime;

        // Vortex force parameters
        // @ref Stomakhin 2013 "A Material Point Method for Snow Simulation" App.A
        cb.vortexAxisOriginX = config.vortexAxisOrigin[0];
        cb.vortexAxisOriginY = config.vortexAxisOrigin[1];
        cb.vortexAxisOriginZ = config.vortexAxisOrigin[2];
        cb.vortexTangentialStrength = config.vortexTangentialStrength;
        // Normalize vortex axis direction on CPU (guard against zero-length input)
        {
            const float ax = config.vortexAxisDir[0];
            const float ay = config.vortexAxisDir[1];
            const float az = config.vortexAxisDir[2];
            const float axisLen = std::sqrt(ax*ax + ay*ay + az*az);
            if (axisLen > 1e-4f) {
                cb.vortexAxisDirX = ax / axisLen;
                cb.vortexAxisDirY = ay / axisLen;
                cb.vortexAxisDirZ = az / axisLen;
            } else {
                cb.vortexAxisDirX = 0.0f;
                cb.vortexAxisDirY = 1.0f; // fallback: +Y up
                cb.vortexAxisDirZ = 0.0f;
            }
        }
        cb.vortexRadialStrength = config.vortexRadialStrength;
        cb.vortexMaxRadius = config.vortexMaxRadius;

        // Wind zone GPU buffer SRV index and active zone count
        // @ref Stam 1999 "Stable Fluids" -- wind zone accumulation
        cb.windZoneSrvIndex      = m_windZoneSrvIndex;
        cb.activeWindZoneCount   = m_activeWindZones;

        // Kill volume GPU buffer SRV index and active volume count
        // @ref Ericson 2005 "Real-Time Collision Detection" Ch.4
        cb.killVolumeSrvIndex    = m_killVolumeSrvIndex;
        cb.activeKillVolumeCount = m_activeKillVolumes;

        // Ground plane collision parameters
        // @ref Müller 2006 "Position Based Dynamics" — restitution + friction response
        // @ref Bender 2014 "Position-Based Simulation Methods in Computer Graphics" — friction
        cb.groundCollision   = config.groundCollision;
        cb.restitution       = config.restitution;
        cb.collisionResponse = config.collisionResponse;
        cb.collisionFriction = config.collisionFriction;

        // Quadratic drag (air resistance)
        // @ref Witkin & Baraff 2001 "Physically Based Modeling" — quadratic drag
        cb.dragCoefficient = config.dragCoefficient;

        // Orbit around emitter origin
        // @ref Christensen 2018 "Fibonacci Sphere Sampling" — golden angle for uniform spread
        cb.orbitEnabled = config.orbitEnabled;
        cb.orbitRadius  = config.orbitRadius;
        cb.orbitSpeed   = config.orbitSpeed;
        cb.orbitAxisX   = config.orbitAxisX;
        cb.orbitAxisY   = config.orbitAxisY;
        cb.orbitAxisZ   = config.orbitAxisZ;

        // Death spawn parameters (sub-emitter on particle death, Task 4.11.0)
        // @ref Shopf 2008 "GPU Particles" — death event driven sub-emitter spawn
        cb.deathSpawnEnabled          = config.deathSpawnEnabled;
        cb.maxDeathEvents             = VFX_MAX_DEATH_EVENTS;
        cb.deathSpawnSpread           = config.deathSpawnSpread;
        cb.deathSpawnSpeedScale       = config.deathSpawnSpeedScale;
        cb.deathSpawnCount            = config.deathSpawnCount;
        cb.deathEventBufferUavIndex   = m_deathEventBufferUavIndex;
        cb.deathEventCountUavIndex    = m_deathEventCountUavIndex;

        // Rotation rate (continuous spin)
        cb.rotationRate = config.rotationRate;

        // Rotation over lifetime (lerped rotation rate)
        cb.rotationOverLifeStart = config.rotationOverLifeStart;
        cb.rotationOverLifeEnd   = config.rotationOverLifeEnd;

        // Acceleration force (independent per-axis, additive with gravity)
        cb.accelX = config.accelX;
        cb.accelY = config.accelY;
        cb.accelZ = config.accelZ;

        // Dynamic material parameters (Task 4.14.0)
        cb.readDynParamsSrv  = m_device->GetBufferSRVIndex(m_particleBuffers[7][read]);
        cb.writeDynParamsUav = m_device->GetBufferUAVIndex(m_particleBuffers[7][write]);

        // Vector field parameters (Task 4.15.0)
        // @ref Stam 1999 "Stable Fluids" — vector field advection
        // @ref Bridson 2007 "Fluid Simulation for Computer Graphics" — velocity field
        cb.vectorFieldEnabled   = config.vectorFieldEnabled;
        cb.vectorFieldStrength  = config.vectorFieldStrength;
        cb.vectorFieldTightness = config.vectorFieldTightness;
        cb.vectorFieldScale     = config.vectorFieldScale;

        auto alloc = cbRing->Allocate(sizeof(VFXUpdateCB));
        std::memcpy(alloc.cpuPtr, &cb, sizeof(VFXUpdateCB));

        cmdList->SetPipeline(m_updatePSO);
        cmdList->SetRootCBV(1, alloc.gpuAddr); // b1 = root param 1 (CBV)
        cmdList->Dispatch((m_maxParticles + kUpdateGroupSize - 1) / kUpdateGroupSize, 1, 1);
    }

    // ── UAV barrier between update and death spawn ─────────────────────────
    // The update CS writes to deathEventBuffer and deathEventCountBuffer.
    // The death spawn CS reads these. A UAV barrier ensures the writes are
    // visible before the death spawn CS reads them.
    // @ref Shopf 2008 "GPU Particles" — UAV barrier between producer and consumer passes
    {
        RHIBarrierDesc barrier{};
        barrier.buffer = m_aliveCount;
        barrier.stateBefore = RHIResourceState::UnorderedAccess;
        barrier.stateAfter = RHIResourceState::UnorderedAccess;
        cmdList->ResourceBarrier(&barrier, 1);
    }
    if (m_deathEventBuffer.IsValid()) {
        RHIBarrierDesc barrier{};
        barrier.buffer = m_deathEventBuffer;
        barrier.stateBefore = RHIResourceState::UnorderedAccess;
        barrier.stateAfter = RHIResourceState::UnorderedAccess;
        cmdList->ResourceBarrier(&barrier, 1);
    }
    if (m_deathEventCountBuffer.IsValid()) {
        RHIBarrierDesc barrier{};
        barrier.buffer = m_deathEventCountBuffer;
        barrier.stateBefore = RHIResourceState::UnorderedAccess;
        barrier.stateAfter = RHIResourceState::UnorderedAccess;
        cmdList->ResourceBarrier(&barrier, 1);
    }

    // ── Death Spawn Pass (optional, only when deathSpawnEnabled) ────────────
    // Reads the death event buffer written by the update CS and spawns child
    // particles at each death position. Dispatched over death events (not particles),
    // so the group size is 64 (smaller than spawn/update 256 — events are sparser).
    //
    // We conservatively dispatch VFX_MAX_DEATH_EVENTS / 64 groups when enabled.
    // Threads beyond the actual event count exit via early return in the shader.
    // This avoids a CPU readback of the atomic event counter (which would require
    // 1-frame latency) while keeping the dispatch cost bounded at ~64 groups.
    //
    // @ref Shopf 2008 "GPU Particles" — death event driven sub-emitter spawn
    if (config.deathSpawnEnabled != 0u && m_deathSpawnPSO.IsValid()) {
        static constexpr uint32_t kDeathSpawnGroupSize = 64u;

        VFXDeathSpawnCB dsCb{};
        dsCb.deathEventBufferSrvIndex = m_deathEventBufferSrvIndex;
        // The actual death event count is written by the update CS via InterlockedAdd.
        // The death spawn CS reads it directly from the GPU counter buffer (SRV).
        // We conservatively dispatch VFX_MAX_DEATH_EVENTS / 64 groups and let the
        // shader's early-exit guard (evtIdx >= actualEventCount) discard excess threads.
        // This avoids CPU readback latency entirely.
        dsCb.deathEventCountSrvIndex  = m_deathEventCountSrvIndex;
        dsCb.deathSpawnCount          = config.deathSpawnCount;
        dsCb.deathSpawnSpread         = config.deathSpawnSpread;
        dsCb.deathSpawnSpeedScale     = config.deathSpawnSpeedScale;
        dsCb.maxParticles             = m_maxParticles;
        dsCb.maxDeathEvents           = VFX_MAX_DEATH_EVENTS;
        dsCb.positionsUavIndex        = m_device->GetBufferUAVIndex(m_particleBuffers[0][write]);
        dsCb.velocitiesUavIndex       = m_device->GetBufferUAVIndex(m_particleBuffers[1][write]);
        dsCb.agesUavIndex             = m_device->GetBufferUAVIndex(m_particleBuffers[2][write]);
        dsCb.lifespansUavIndex        = m_device->GetBufferUAVIndex(m_particleBuffers[3][write]);
        dsCb.sizesUavIndex            = m_device->GetBufferUAVIndex(m_particleBuffers[4][write]);
        dsCb.colorsUavIndex           = m_device->GetBufferUAVIndex(m_particleBuffers[5][write]);
        dsCb.rotationsUavIndex        = m_device->GetBufferUAVIndex(m_particleBuffers[6][write]);
        dsCb.freeListUavIndex         = m_device->GetBufferUAVIndex(m_freeList);
        dsCb.freeListHeadUavIndex     = m_device->GetBufferUAVIndex(m_freeListHead);
        dsCb.aliveCountUavIndex       = m_device->GetBufferUAVIndex(m_aliveCount);
        dsCb.frameNumber              = frameNumber;
        dsCb.dynamicParamsUavIndex    = m_device->GetBufferUAVIndex(m_particleBuffers[7][write]);
        // Child lifetime and size: use parent emitter's lifetime/size as defaults.
        // For sparks/debris effects, shorter lifetime than parent is typical.
        dsCb.childLifetime            = (config.minLifetime + config.maxLifetime) * 0.5f * 0.5f;
        dsCb.childSize                = config.initialSize * 0.5f;

        auto alloc = cbRing->Allocate(sizeof(VFXDeathSpawnCB));
        std::memcpy(alloc.cpuPtr, &dsCb, sizeof(VFXDeathSpawnCB));

        cmdList->SetPipeline(m_deathSpawnPSO);
        cmdList->SetRootCBV(1, alloc.gpuAddr); // b1 = root param 1 (CBV)
        const uint32_t deathGroups = (VFX_MAX_DEATH_EVENTS + kDeathSpawnGroupSize - 1) / kDeathSpawnGroupSize;
        cmdList->Dispatch(deathGroups, 1, 1);
    }

    // ── UAV barrier before indirect args ─────────────────────────────────
    {
        RHIBarrierDesc barrier{};
        barrier.buffer = m_aliveCount;
        barrier.stateBefore = RHIResourceState::UnorderedAccess;
        barrier.stateAfter = RHIResourceState::UnorderedAccess;
        cmdList->ResourceBarrier(&barrier, 1);
    }

    // ── Indirect Args Pass ──────────────────────────────────────────────
    {
        // The indirect args CS reads alive count and writes draw args
        cmdList->SetPipeline(m_indirectArgsPSO);
        // Root constant: alive count UAV index + indirect args UAV index
        uint32_t rootData[2] = {
            m_device->GetBufferUAVIndex(m_aliveCount),
            m_device->GetBufferUAVIndex(m_indirectArgs)
        };
        cmdList->SetRootConstants(0, rootData, 2);
        cmdList->Dispatch(1, 1, 1);
    }

    // ── Transition indirect args to IndirectArgument state ──────────────
    {
        RHIBarrierDesc barrier{};
        barrier.buffer = m_indirectArgs;
        barrier.stateBefore = RHIResourceState::UnorderedAccess;
        barrier.stateAfter = RHIResourceState::Common; // IndirectArgument
        cmdList->ResourceBarrier(&barrier, 1);
    }

    // ── Transition write-set to SRV for billboard rendering ─────────────
    for (uint32_t ch = 0; ch < kChannelCount; ++ch) {
        RHIBarrierDesc barrier{};
        barrier.buffer = m_particleBuffers[ch][write];
        barrier.stateBefore = RHIResourceState::UnorderedAccess;
        barrier.stateAfter = RHIResourceState::Common;
        cmdList->ResourceBarrier(&barrier, 1);
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Blend Mode PSO Selection
// ─────────────────────────────────────────────────────────────────────────────

RHIPipelineHandle VFXParticleSystem::GetBillboardPSOForBlendMode(uint32_t mode) const {
    switch (mode) {
        case 0:  return m_billboardPSO_AlphaBlend;   // SrcAlpha / InvSrcAlpha
        case 1:  return m_billboardPSO_Additive;     // One / One
        case 2:  // PremulAlpha (default) — fall through
        default: return m_billboardPSO;              // One / InvSrcAlpha
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// ─────────────────────────────────────────────────────────────────────────────
// Color Ramp LUT Upload
// ─────────────────────────────────────────────────────────────────────────────

bool VFXParticleSystem::UploadColorRampLUT(UploadContext& upload, IRHICommandList* cmd,
                                            const VFXColorRamp& ramp) {
    // Destroy previous LUT if one exists
    if (m_colorRampLUT.IsValid() && m_device) {
        m_device->DestroyTexture(m_colorRampLUT);
        m_colorRampLUT = {};
        m_colorRampSrvIndex = UINT32_MAX;
    }

    // Bake ramp to RGBA16F pixel data
    const auto pixels = ramp.BakeToRGBA16F();

    // Create 256x1 RGBA16F texture
    RHITextureDesc desc{};
    desc.width = VFXColorRamp::kLutWidth;
    desc.height = 1;
    desc.depthOrArraySize = 1;
    desc.mipLevels = 1;
    desc.format = RHIFormat::RGBA16Float;
    desc.usage = RHITextureUsage::ShaderResource | RHITextureUsage::CopyDst;
    desc.debugName = "VFXColorRampLUT";
    m_colorRampLUT = m_device->CreateTexture(desc);
    if (!m_colorRampLUT.IsValid()) {
        UZ_LOG(App, Error, "VFX: Failed to create color ramp LUT texture");
        return false;
    }

    // Upload pixel data via staging buffer
    const uint64_t dataSize = VFXColorRamp::kLutWidth * 4 * sizeof(uint16_t);
    if (!upload.UploadTexture(cmd, m_colorRampLUT, pixels.data(), dataSize)) {
        UZ_LOG(App, Error, "VFX: Failed to upload color ramp LUT data");
        m_device->DestroyTexture(m_colorRampLUT);
        m_colorRampLUT = {};
        return false;
    }

    m_colorRampSrvIndex = m_device->GetTextureSRVIndex(m_colorRampLUT);
    UZ_LOG(App, Info, "VFX: Uploaded color ramp LUT — SRV index={}", m_colorRampSrvIndex);
    return true;
}

void VFXParticleSystem::ClearColorRampLUT() {
    if (m_colorRampLUT.IsValid() && m_device) {
        m_device->DestroyTexture(m_colorRampLUT);
        m_colorRampLUT = {};
    }
    m_colorRampSrvIndex = UINT32_MAX;
}

// ─────────────────────────────────────────────────────────────────────────────
// Predefined Color Ramp Factory Functions
// ─────────────────────────────────────────────────────────────────────────────

/// Builds a fire gradient: black -> orange -> yellow -> white -> transparent.
///
/// @ref GDC 2019 "Niagara: Building the Future of FX in Unreal Engine"
///     — Color over Life with multi-stop fire gradient
VFXColorRamp BuildFireRamp() {
    VFXColorRamp ramp;
    ramp.AddStop(0.0f,  0.0f,  0.0f,  0.0f,  1.0f); // black, opaque
    ramp.AddStop(0.3f,  1.0f,  0.4f,  0.0f,  1.0f); // orange
    ramp.AddStop(0.6f,  1.0f,  0.9f,  0.2f,  0.9f); // yellow
    ramp.AddStop(0.85f, 1.0f,  1.0f,  1.0f,  0.6f); // white
    ramp.AddStop(1.0f,  1.0f,  1.0f,  1.0f,  0.0f); // transparent
    return ramp;
}

/// Builds an ice gradient: dark blue -> cyan -> white -> transparent.
VFXColorRamp BuildIceRamp() {
    VFXColorRamp ramp;
    ramp.AddStop(0.0f,  0.0f,  0.05f, 0.3f,  1.0f); // dark blue
    ramp.AddStop(0.4f,  0.1f,  0.7f,  0.9f,  1.0f); // cyan
    ramp.AddStop(0.8f,  0.9f,  0.95f, 1.0f,  0.7f); // white-ish
    ramp.AddStop(1.0f,  1.0f,  1.0f,  1.0f,  0.0f); // transparent
    return ramp;
}

/// Builds a rainbow gradient: red -> yellow -> green -> cyan -> blue.
/// All stops at full alpha, fading to zero at t=1 for a clean fadeout.
VFXColorRamp BuildRainbowRamp() {
    VFXColorRamp ramp;
    ramp.AddStop(0.0f,  1.0f,  0.0f,  0.0f,  1.0f); // red
    ramp.AddStop(0.25f, 1.0f,  1.0f,  0.0f,  1.0f); // yellow
    ramp.AddStop(0.5f,  0.0f,  1.0f,  0.0f,  1.0f); // green
    ramp.AddStop(0.75f, 0.0f,  1.0f,  1.0f,  0.8f); // cyan
    ramp.AddStop(1.0f,  0.0f,  0.0f,  1.0f,  0.0f); // blue, transparent
    return ramp;
}

// Draw Billboards
// ─────────────────────────────────────────────────────────────────────────────

void VFXParticleSystem::DrawBillboards(IRHICommandList* cmdList, CBRingAllocator* cbRing,
                                        uint64_t perFrameCBGpuAddr,
                                        const Mat4& viewMat, const Vec3& emitterPos,
                                        const Mat4& prevViewProj,
                                        const VFXEmitterConfig& config,
                                        uint32_t blendMode,
                                        uint32_t depthBufferSrvIdx) {
    const int write = 1 - m_pingPong; // Freshly written data is in the write set

    // b0: Unified billboard root constants (VS buffer indices + PS texture indices)
    VFXBillboardIndicesCB indices{};
    // VS indices
    indices.positionBufferIdx = m_device->GetBufferSRVIndex(m_particleBuffers[0][write]);
    indices.sizeBufferIdx = m_device->GetBufferSRVIndex(m_particleBuffers[4][write]);
    indices.colorBufferIdx = m_device->GetBufferSRVIndex(m_particleBuffers[5][write]);
    indices.rotationBufferIdx = m_device->GetBufferSRVIndex(m_particleBuffers[6][write]);
    indices.sortedIndicesSrvIndex = m_device->GetBufferSRVIndex(m_sortIndices);
    indices.velocityBufferIdx = m_device->GetBufferSRVIndex(m_particleBuffers[1][write]);
    indices.agesBufferIdx = m_device->GetBufferSRVIndex(m_particleBuffers[2][write]);
    indices.lifespansBufferIdx = m_device->GetBufferSRVIndex(m_particleBuffers[3][write]);
    // PS indices — no sprite texture, always procedural circles
    indices.spriteTextureIdx = UINT32_MAX;
    indices.depthBufferIdx = depthBufferSrvIdx;
    // Dynamic material parameters SRV (Task 4.14.0)
    indices.dynamicParamsBufferIdx = m_device->GetBufferSRVIndex(m_particleBuffers[7][write]);

    // Select PSO based on blend mode (lit mode uses standard PSO with CB flag).
    cmdList->SetPipeline(GetBillboardPSOForBlendMode(blendMode));
    cmdList->SetRootConstants(0, reinterpret_cast<const uint32_t*>(&indices),
                              sizeof(indices) / sizeof(uint32_t));

    // b1: PerFrameCB
    cmdList->SetRootCBV(1, perFrameCBGpuAddr);

    // b2: Unified VFXBillboardCB (VS emitter params + PS rendering params)
    VFXBillboardCB billboardCB{};
    // VS data
    billboardCB.invView = viewMat.Inverse();
    billboardCB.emitterPosition[0] = emitterPos.x;
    billboardCB.emitterPosition[1] = emitterPos.y;
    billboardCB.emitterPosition[2] = emitterPos.z;
    billboardCB.globalSizeMultiplier = 1.0f;
    billboardCB.globalColorTint[0] = 1.0f;
    billboardCB.globalColorTint[1] = 1.0f;
    billboardCB.globalColorTint[2] = 1.0f;
    billboardCB.globalColorTint[3] = 1.0f;
    billboardCB.prevViewProj = prevViewProj;
    // PS data — rendering parameters from emitter config
    billboardCB.softFadeDistance   = config.softFadeDistance;
    billboardCB.alphaErosion      = 0.0f;
    billboardCB.emissiveIntensity = config.emissiveIntensity;
    billboardCB.litMode           = config.litMode;
    // Size over lifetime curve parameters
    billboardCB.sizeOverLifeStart    = config.sizeOverLifeStart;
    billboardCB.sizeOverLifePeak     = config.sizeOverLifePeak;
    billboardCB.sizeOverLifeEnd      = config.sizeOverLifeEnd;
    billboardCB.sizeOverLifePeakTime = config.sizeOverLifePeakTime;
    // Color over lifetime parameters
    billboardCB.colorLifeStart[0] = config.colorLifeStartR;
    billboardCB.colorLifeStart[1] = config.colorLifeStartG;
    billboardCB.colorLifeStart[2] = config.colorLifeStartB;
    billboardCB.colorLifeStart[3] = config.colorLifeStartA;
    billboardCB.colorLifeEnd[0]   = config.colorLifeEndR;
    billboardCB.colorLifeEnd[1]   = config.colorLifeEndG;
    billboardCB.colorLifeEnd[2]   = config.colorLifeEndB;
    billboardCB.colorLifeEnd[3]   = config.colorLifeEndA;
    // Billboard facing mode and custom direction
    billboardCB.facingMode      = config.facingMode;
    billboardCB.customFacingX   = config.customFacingX;
    billboardCB.customFacingY   = config.customFacingY;
    billboardCB.customFacingZ   = config.customFacingZ;
    // Billboard stretching + blend
    billboardCB.stretchFactor   = config.stretchFactor;
    // Blend mode hint tells the PS how to handle alpha output
    billboardCB.blendModeHint   = blendMode;
    // Color ramp LUT SRV index (UINT32_MAX = legacy two-point lerp)
    billboardCB.colorRampLutSrvIndex = m_colorRampSrvIndex;
    // Camera offset & pivot offset (Task 4.7.0)
    // @ref Latta 2004 "Inside Geometry Instancing" — camera offset for depth layering
    billboardCB.cameraOffset    = config.cameraOffset;
    billboardCB.pivotOffsetX    = config.pivotOffsetX;
    billboardCB.pivotOffsetY    = config.pivotOffsetY;
    // Size by speed (Task 4.8.0)
    billboardCB.sizeBySpeedEnabled = config.sizeBySpeedEnabled;
    billboardCB.sizeBySpeedMin    = config.sizeBySpeedMin;
    billboardCB.sizeBySpeedMax    = config.sizeBySpeedMax;
    billboardCB.speedRangeMax     = config.speedRangeMax;
    // Screen-space size: constant-pixel-coverage mode.
    // @ref Valve 2006 "Rendering in Left 4 Dead" — constant-screen-size sprites
    billboardCB.screenSpaceSizeEnabled = config.screenSpaceSizeEnabled;
    billboardCB.screenSpacePixelSize   = config.screenSpacePixelSize;
    // Color by speed: VS passes speed factor to PS for color lerp.
    billboardCB.colorBySpeedEnabled    = config.colorBySpeedEnabled;

    auto cbAlloc = cbRing->Allocate(sizeof(VFXBillboardCB));
    std::memcpy(cbAlloc.cpuPtr, &billboardCB, sizeof(VFXBillboardCB));
    cmdList->SetRootCBV(2, cbAlloc.gpuAddr);

    // Draw all particle slots — the billboard VS reads particle attributes
    // and outputs degenerate triangles for dead particles (age >= lifespan).
    // 6 vertices per particle (2 triangles per billboard quad).
    cmdList->Draw(m_maxParticles * 6, 1, 0, 0);
}

// ─────────────────────────────────────────────────────────────────────────────
// Frustum Culling — Task 3.89.0
// ─────────────────────────────────────────────────────────────────────────────

void VFXParticleSystem::UpdateCullState(const Mat4& viewProj, const VFXEmitterConfig& config) {
    // ── Extract 6 frustum planes from VP matrix (Gribb & Hartmann 2001) ────────
    // Each plane is stored as (normal.xyz, d) where the frustum inside half-space is:
    //   dot(normal, point) + d >= 0.
    // Direct extraction from row arithmetic on the clip-space VP matrix.
    //
    /// @ref Gribb 2001 "Fast Extraction of Viewing Frustum Planes"
    float planes[6][4]; // [plane][x, y, z, d]

    // Access VP matrix as row-major float array.
    const float* m = reinterpret_cast<const float*>(&viewProj);
    auto row = [&](int r, int c) -> float { return m[r * 4 + c]; };

    // Left:   row3 + row0
    planes[0][0] = row(3,0) + row(0,0);
    planes[0][1] = row(3,1) + row(0,1);
    planes[0][2] = row(3,2) + row(0,2);
    planes[0][3] = row(3,3) + row(0,3);
    // Right:  row3 - row0
    planes[1][0] = row(3,0) - row(0,0);
    planes[1][1] = row(3,1) - row(0,1);
    planes[1][2] = row(3,2) - row(0,2);
    planes[1][3] = row(3,3) - row(0,3);
    // Bottom: row3 + row1
    planes[2][0] = row(3,0) + row(1,0);
    planes[2][1] = row(3,1) + row(1,1);
    planes[2][2] = row(3,2) + row(1,2);
    planes[2][3] = row(3,3) + row(1,3);
    // Top:    row3 - row1
    planes[3][0] = row(3,0) - row(1,0);
    planes[3][1] = row(3,1) - row(1,1);
    planes[3][2] = row(3,2) - row(1,2);
    planes[3][3] = row(3,3) - row(1,3);
    // Near:   row3 + row2
    planes[4][0] = row(3,0) + row(2,0);
    planes[4][1] = row(3,1) + row(2,1);
    planes[4][2] = row(3,2) + row(2,2);
    planes[4][3] = row(3,3) + row(2,3);
    // Far:    row3 - row2
    planes[5][0] = row(3,0) - row(2,0);
    planes[5][1] = row(3,1) - row(2,1);
    planes[5][2] = row(3,2) - row(2,2);
    planes[5][3] = row(3,3) - row(2,3);

    // Normalize plane normals so signed distance is in world-space units.
    for (int i = 0; i < 6; ++i) {
        const float len = std::sqrt(planes[i][0] * planes[i][0]
                                  + planes[i][1] * planes[i][1]
                                  + planes[i][2] * planes[i][2]);
        if (len > 1e-6f) {
            planes[i][0] /= len;
            planes[i][1] /= len;
            planes[i][2] /= len;
            planes[i][3] /= len;
        }
    }

    // ── Conservative bounding sphere radius ───────────────────────────────────
    // Worst-case sphere: start from spawnRadius, expand by max reachable distance.
    // maxSpeed = |spawnVelocity| + velocityVariation + |gravity| * maxLifetime
    // radius   = spawnRadius + maxSpeed * maxLifetime
    //
    /// @ref Assarsson 2000 "Optimized View Frustum Culling Algorithms" — conservative sphere
    const float vx = config.spawnVelocity[0];
    const float vy = config.spawnVelocity[1];
    const float vz = config.spawnVelocity[2];
    const float velMag  = std::sqrt(vx*vx + vy*vy + vz*vz);
    const float gx = config.gravity[0];
    const float gy = config.gravity[1];
    const float gz = config.gravity[2];
    const float gravMag = std::sqrt(gx*gx + gy*gy + gz*gz);
    const float maxSpeed = velMag + config.velocityVariation + gravMag * config.maxLifetime;
    const float radius   = config.spawnRadius + maxSpeed * config.maxLifetime;

    // ── Sphere-plane frustum test ─────────────────────────────────────────────
    // If signed distance of center < -radius for ANY plane, sphere is outside.
    //
    /// @ref Assarsson 2000 "Optimized View Frustum Culling Algorithms" — sphere-plane test
    const float cx = config.position[0];
    const float cy = config.position[1];
    const float cz = config.position[2];

    bool outside = false;
    for (int i = 0; i < 6; ++i) {
        const float dist = planes[i][0] * cx
                         + planes[i][1] * cy
                         + planes[i][2] * cz
                         + planes[i][3];
        if (dist < -radius) {
            outside = true;
            break;
        }
    }

    // ── Two-frame delay to prevent single-frame edge pop-in ───────────────────
    // Only cull when two consecutive frames detect the emitter outside the frustum.
    m_culledPrevFrame = m_culledThisFrame;
    m_culledThisFrame = outside;

    if (IsCulled()) {
        ++m_framesSinceCulled;
    } else {
        m_framesSinceCulled = 0;
    }
}

} // namespace uz
