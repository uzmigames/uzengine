/// VFX Viewport Demo — Preset Data Tables
///
/// Extracted from vfx_viewport_vfx.cpp to keep files under 1500 lines.
/// Contains the static VFXPreset array and preset count accessor.

#include "vfx_viewport_vfx.h"

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

} // namespace uz
