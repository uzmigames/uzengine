/// VFX Emitter Presets — factory functions matching standalone demo exactly.
///
/// Conversion from standalone (meters, radians) to engine (cm, degrees):
///   distances/velocities/sizes/radii: × 100
///   spread angle: rad × 57.2958 → degrees
///   damping: same convention (small = heavy damping)
///   gravity: × 100
///   colors/sizeOverLife/time: unchanged (unitless)

#pragma once

#include "particle_emitter.h"

namespace uz {

/// Simple Burst (standalone preset 0) — warm upward burst, additive glow.
inline void ApplyPreset_Fire(ParticleEmitter& e) {
    e.maxParticles = 5000;
    e.emissionRate = 300.0f;
    e.lifetime = 1.75f;
    e.lifetimeVariation = 0.75f;
    e.spawnVelocity = {0.0f, 0.0f, 600.0f};
    e.velocityVariation = 300.0f;
    e.spreadAngle = 57.3f;   // 1.0 rad
    e.gravity = {0.0f, 0.0f, -300.0f};
    e.damping = 0.15f;
    e.initialSize = 100.0f;
    e.initialColor = {1.0f, 0.8f, 0.4f, 1.0f};
    e.spawnRadius = 20.0f;
    e.emissiveIntensity = 1.0f;
    e.softParticleFadeRange = 50.0f;
    e.sizeOverLifeStart = 0.5f; e.sizeOverLifePeak = 1.2f;
    e.sizeOverLifeEnd = 0.0f; e.sizeOverLifePeakTime = 0.3f;
    e.colorLifeStartR = 1.0f; e.colorLifeStartG = 0.9f;
    e.colorLifeStartB = 0.5f; e.colorLifeStartA = 1.0f;
    e.colorLifeEndR = 1.0f; e.colorLifeEndG = 0.3f;
    e.colorLifeEndB = 0.05f; e.colorLifeEndA = 0.0f;
    e.blendMode = 1;
    e.particleLightEnabled = 1;
    e.particleLightRadius = 800.0f;
    e.particleLightIntensity = 3.0f;
    e.particleLightFlicker = 0.3f;
    e.particleLightFlickerSpeed = 4.0f;
}

/// Smoke Plume (standalone preset 1) — slow rising dark particles.
inline void ApplyPreset_Smoke(ParticleEmitter& e) {
    e.maxParticles = 3000;
    e.emissionRate = 200.0f;
    e.lifetime = 4.5f;
    e.lifetimeVariation = 1.5f;
    e.spawnVelocity = {0.0f, 0.0f, 300.0f};
    e.velocityVariation = 50.0f;
    e.spreadAngle = 34.4f;   // 0.6 rad
    e.gravity = {0.0f, 0.0f, -100.0f};
    e.damping = 0.2f;
    e.initialSize = 120.0f;
    e.initialColor = {0.5f, 0.5f, 0.5f, 0.7f};
    e.spawnRadius = 20.0f;
    e.emissiveIntensity = 0.0f;
    e.softParticleFadeRange = 50.0f;
    e.sizeOverLifeStart = 0.3f; e.sizeOverLifePeak = 1.5f;
    e.sizeOverLifeEnd = 2.5f; e.sizeOverLifePeakTime = 0.3f;
    e.colorLifeStartR = 0.7f; e.colorLifeStartG = 0.6f;
    e.colorLifeStartB = 0.5f; e.colorLifeStartA = 0.8f;
    e.colorLifeEndR = 0.2f; e.colorLifeEndG = 0.2f;
    e.colorLifeEndB = 0.2f; e.colorLifeEndA = 0.0f;
    e.blendMode = 0;
}

/// Sparks (standalone preset 2) — fast bright with ground bounce.
inline void ApplyPreset_Sparks(ParticleEmitter& e) {
    e.maxParticles = 5000;
    e.emissionRate = 800.0f;
    e.lifetime = 1.0f;
    e.lifetimeVariation = 0.5f;
    e.spawnVelocity = {0.0f, 0.0f, 1000.0f};
    e.velocityVariation = 400.0f;
    e.spreadAngle = 57.3f;   // 1.0 rad
    e.gravity = {0.0f, 0.0f, -1200.0f};
    e.damping = 0.03f;
    e.initialSize = 5.0f;
    e.initialColor = {1.0f, 0.9f, 0.5f, 1.0f};
    e.spawnRadius = 15.0f;
    e.emissiveIntensity = 2.0f;
    e.softParticleFadeRange = 20.0f;
    e.facingMode = 1;  // Velocity aligned
    e.stretchFactor = 0.02f; // standalone=2.0, /100 for cm-scale speed
    e.sizeOverLifeStart = 1.0f; e.sizeOverLifePeak = 1.0f;
    e.sizeOverLifeEnd = 0.3f; e.sizeOverLifePeakTime = 0.0f;
    e.colorLifeStartR = 1.0f; e.colorLifeStartG = 0.95f;
    e.colorLifeStartB = 0.7f; e.colorLifeStartA = 1.0f;
    e.colorLifeEndR = 1.0f; e.colorLifeEndG = 0.3f;
    e.colorLifeEndB = 0.0f; e.colorLifeEndA = 0.2f;
    e.blendMode = 1;
    e.groundCollision = 1;
    e.restitution = 0.5f;
    e.collisionResponse = 0; // Bounce
    e.collisionFriction = 0.7f;
}

/// Fountain — upward stream with gravity arc (no standalone exact match, tuned).
inline void ApplyPreset_Fountain(ParticleEmitter& e) {
    e.maxParticles = 10000;
    e.emissionRate = 400.0f;
    e.lifetime = 3.0f;
    e.lifetimeVariation = 0.5f;
    e.spawnVelocity = {0.0f, 0.0f, 600.0f};
    e.velocityVariation = 50.0f;
    e.spreadAngle = 8.0f;
    e.gravity = {0.0f, 0.0f, -981.0f};
    e.damping = 0.02f;
    e.initialSize = 12.0f;
    e.initialColor = {0.5f, 0.7f, 1.0f, 0.8f};
    e.softParticleFadeRange = 30.0f;
    e.sizeOverLifeStart = 1.0f; e.sizeOverLifePeak = 1.0f;
    e.sizeOverLifeEnd = 0.3f; e.sizeOverLifePeakTime = 0.5f;
    e.colorLifeStartR = 0.6f; e.colorLifeStartG = 0.8f;
    e.colorLifeStartB = 1.0f; e.colorLifeStartA = 1.0f;
    e.colorLifeEndR = 0.3f; e.colorLifeEndG = 0.5f;
    e.colorLifeEndB = 1.0f; e.colorLifeEndA = 0.0f;
    e.blendMode = 0;
}

/// Magic Orb (standalone preset 3) — orbiting glowing particles.
inline void ApplyPreset_MagicOrb(ParticleEmitter& e) {
    e.maxParticles = 3000;
    e.emissionRate = 300.0f;
    e.lifetime = 1.5f;
    e.lifetimeVariation = 0.5f;
    e.spawnVelocity = {0.0f, 0.0f, 100.0f};
    e.velocityVariation = 60.0f;
    e.spreadAngle = 180.0f;   // 3.14 rad = full sphere
    e.gravity = {0.0f, 0.0f, 0.0f};
    e.damping = 0.3f;
    e.initialSize = 50.0f;
    e.initialColor = {0.3f, 0.6f, 1.0f, 0.9f};
    e.spawnShape = 1; // Sphere
    e.spawnRadius = 80.0f;
    e.emissiveIntensity = 0.5f;
    e.softParticleFadeRange = 30.0f;
    e.rotationRate = 1.5f;
    e.sizeOverLifeStart = 0.5f; e.sizeOverLifePeak = 1.0f;
    e.sizeOverLifeEnd = 0.2f; e.sizeOverLifePeakTime = 0.4f;
    e.colorLifeStartR = 0.4f; e.colorLifeStartG = 0.8f;
    e.colorLifeStartB = 1.0f; e.colorLifeStartA = 1.0f;
    e.colorLifeEndR = 0.1f; e.colorLifeEndG = 0.2f;
    e.colorLifeEndB = 0.8f; e.colorLifeEndA = 0.0f;
    e.blendMode = 1;
}

/// Instant burst — all particles spawn at once.
inline void ApplyPreset_Burst(ParticleEmitter& e) {
    e.maxParticles = 500;
    e.emissionRate = 50000.0f;
    e.lifetime = 2.0f;
    e.lifetimeVariation = 0.5f;
    e.spawnVelocity = {0.0f, 0.0f, 400.0f};
    e.velocityVariation = 150.0f;
    e.spreadAngle = 90.0f;
    e.gravity = {0.0f, 0.0f, -981.0f};
    e.damping = 0.05f;
    e.initialSize = 30.0f;
    e.initialColor = {1.0f, 1.0f, 0.8f, 1.0f};
    e.emissiveIntensity = 1.5f;
    e.sizeOverLifeEnd = 0.0f;
    e.colorLifeEndA = 0.0f;
    e.blendMode = 1;
}

/// Explosion flash.
inline void ApplyPreset_ExplosionBurst(ParticleEmitter& e) {
    e.maxParticles = 500;
    e.emissionRate = 50000.0f;
    e.lifetime = 0.5f;
    e.spawnVelocity = {0.0f, 0.0f, 600.0f};
    e.velocityVariation = 300.0f;
    e.spreadAngle = 90.0f;
    e.gravity = {0.0f, 0.0f, -200.0f};
    e.initialSize = 50.0f;
    e.initialColor = {1.0f, 0.9f, 0.5f, 1.0f};
    e.emissiveIntensity = 5.0f;
    e.particleLightEnabled = 1;
    e.particleLightRadius = 1500.0f;
    e.particleLightIntensity = 8.0f;
    e.particleLightFlicker = 0.5f;
    e.particleLightFlickerSpeed = 8.0f;
    e.sizeOverLifeEnd = 0.0f;
    e.colorLifeEndA = 0.0f;
    e.blendMode = 1;
}

/// Explosion smoke.
inline void ApplyPreset_ExplosionSmoke(ParticleEmitter& e) {
    e.maxParticles = 1000;
    e.emissionRate = 500.0f;
    e.lifetime = 3.0f;
    e.spawnVelocity = {0.0f, 0.0f, 100.0f};
    e.velocityVariation = 50.0f;
    e.spreadAngle = 60.0f;
    e.gravity = {0.0f, 0.0f, 30.0f};
    e.damping = 0.2f;
    e.initialSize = 80.0f;
    e.initialColor = {0.2f, 0.2f, 0.2f, 0.6f};
    e.sizeOverLifeStart = 0.5f; e.sizeOverLifePeak = 2.0f;
    e.sizeOverLifeEnd = 3.0f;
    e.curlStrength = 1.5f;
    e.colorLifeEndA = 0.0f;
    e.startDelay = 0.1f;
}

/// Explosion debris (standalone preset 4) — bouncing hot fragments.
inline void ApplyPreset_ExplosionDebris(ParticleEmitter& e) {
    e.maxParticles = 2000;
    e.emissionRate = 2000.0f;
    e.lifetime = 1.25f;
    e.lifetimeVariation = 0.75f;
    e.spawnVelocity = {0.0f, 0.0f, 800.0f};
    e.velocityVariation = 400.0f;
    e.spreadAngle = 85.9f;   // 1.5 rad
    e.gravity = {0.0f, 0.0f, -2000.0f};
    e.damping = 0.08f;
    e.initialSize = 50.0f;
    e.initialColor = {1.0f, 0.5f, 0.15f, 1.0f};
    e.spawnRadius = 30.0f;
    e.emissiveIntensity = 1.0f;
    e.softParticleFadeRange = 30.0f;
    e.facingMode = 1;  // Velocity aligned
    e.stretchFactor = 0.02f;
    e.sizeOverLifeStart = 0.8f; e.sizeOverLifePeak = 1.5f;
    e.sizeOverLifeEnd = 0.1f; e.sizeOverLifePeakTime = 0.1f;
    e.colorLifeStartR = 1.0f; e.colorLifeStartG = 0.8f;
    e.colorLifeStartB = 0.4f; e.colorLifeStartA = 1.0f;
    e.colorLifeEndR = 0.4f; e.colorLifeEndG = 0.08f;
    e.colorLifeEndB = 0.0f; e.colorLifeEndA = 0.0f;
    e.blendMode = 1;
    e.groundCollision = 1;
    e.restitution = 0.3f;
    e.collisionResponse = 0; // Bounce
    e.collisionFriction = 0.8f;
}

/// Fireball (standalone preset 5) — dense orange projectile cloud.
inline void ApplyPreset_Fireball(ParticleEmitter& e) {
    e.maxParticles = 5000;
    e.emissionRate = 800.0f;
    e.lifetime = 0.55f;
    e.lifetimeVariation = 0.25f;
    e.spawnVelocity = {0.0f, 0.0f, 50.0f};
    e.velocityVariation = 150.0f;
    e.spreadAngle = 143.2f;   // 2.5 rad
    e.gravity = {0.0f, 0.0f, 100.0f};
    e.damping = 0.5f;
    e.initialSize = 60.0f;
    e.initialColor = {1.0f, 0.5f, 0.1f, 0.9f};
    e.spawnRadius = 40.0f;
    e.emissiveIntensity = 2.5f;
    e.softParticleFadeRange = 40.0f;
    e.stretchFactor = 0.005f;
    e.sizeOverLifeStart = 0.5f; e.sizeOverLifePeak = 1.3f;
    e.sizeOverLifeEnd = 0.4f; e.sizeOverLifePeakTime = 0.2f;
    e.colorLifeStartR = 1.0f; e.colorLifeStartG = 0.7f;
    e.colorLifeStartB = 0.2f; e.colorLifeStartA = 1.0f;
    e.colorLifeEndR = 0.7f; e.colorLifeEndG = 0.15f;
    e.colorLifeEndB = 0.02f; e.colorLifeEndA = 0.3f;
    e.blendMode = 1;
    e.particleLightEnabled = 1;
    e.particleLightRadius = 600.0f;
    e.particleLightIntensity = 4.0f;
    e.particleLightFlicker = 0.4f;
    e.particleLightFlickerSpeed = 6.0f;
}

/// Ring of Fire (standalone preset 6) — circular spawn, upward arc.
inline void ApplyPreset_RingOfFire(ParticleEmitter& e) {
    e.maxParticles = 5000;
    e.emissionRate = 400.0f;
    e.lifetime = 1.0f;
    e.lifetimeVariation = 0.5f;
    e.spawnVelocity = {0.0f, 0.0f, 400.0f};
    e.velocityVariation = 100.0f;
    e.spreadAngle = 28.6f;   // 0.5 rad
    e.gravity = {0.0f, 0.0f, -200.0f};
    e.damping = 0.2f;
    e.initialSize = 40.0f;
    e.initialColor = {1.0f, 0.5f, 0.1f, 0.9f};
    e.spawnShape = 3; // Ring
    e.spawnRadius = 300.0f;
    e.emissiveIntensity = 1.5f;
    e.softParticleFadeRange = 30.0f;
    e.sizeOverLifeStart = 0.5f; e.sizeOverLifePeak = 1.0f;
    e.sizeOverLifeEnd = 0.0f; e.sizeOverLifePeakTime = 0.3f;
    e.colorLifeStartR = 1.0f; e.colorLifeStartG = 0.8f;
    e.colorLifeStartB = 0.3f; e.colorLifeStartA = 1.0f;
    e.colorLifeEndR = 1.0f; e.colorLifeEndG = 0.2f;
    e.colorLifeEndB = 0.0f; e.colorLifeEndA = 0.0f;
    e.blendMode = 1;
}

/// Cone Jet (standalone preset 7) — tight directional blue stream.
inline void ApplyPreset_ConeJet(ParticleEmitter& e) {
    e.maxParticles = 5000;
    e.emissionRate = 600.0f;
    e.lifetime = 1.0f;
    e.lifetimeVariation = 0.5f;
    e.spawnVelocity = {0.0f, 0.0f, 1000.0f};
    e.velocityVariation = 200.0f;
    e.spreadAngle = 17.2f;   // 0.3 rad
    e.gravity = {0.0f, 0.0f, -500.0f};
    e.damping = 0.1f;
    e.initialSize = 20.0f;
    e.initialColor = {0.3f, 0.6f, 1.0f, 0.9f};
    e.spawnShape = 4; // Cone
    e.spawnRadius = 10.0f;
    e.emissiveIntensity = 2.0f;
    e.softParticleFadeRange = 30.0f;
    e.facingMode = 1; // Velocity aligned
    e.stretchFactor = 0.015f;
    e.sizeOverLifeStart = 0.3f; e.sizeOverLifePeak = 1.0f;
    e.sizeOverLifeEnd = 0.0f; e.sizeOverLifePeakTime = 0.3f;
    e.colorLifeStartR = 0.5f; e.colorLifeStartG = 0.8f;
    e.colorLifeStartB = 1.0f; e.colorLifeStartA = 1.0f;
    e.colorLifeEndR = 0.1f; e.colorLifeEndG = 0.3f;
    e.colorLifeEndB = 0.8f; e.colorLifeEndA = 0.0f;
    e.blendMode = 1;
}

/// Cylinder Volume (standalone preset 8) — green translucent volume spawn.
inline void ApplyPreset_CylinderVolume(ParticleEmitter& e) {
    e.maxParticles = 3000;
    e.emissionRate = 300.0f;
    e.lifetime = 2.25f;
    e.lifetimeVariation = 0.75f;
    e.spawnVelocity = {0.0f, 0.0f, 200.0f};
    e.velocityVariation = 50.0f;
    e.spreadAngle = 45.8f;   // 0.8 rad
    e.gravity = {0.0f, 0.0f, -100.0f};
    e.damping = 0.15f;
    e.initialSize = 30.0f;
    e.initialColor = {0.2f, 0.9f, 0.3f, 0.8f};
    e.spawnShape = 5; // Cylinder
    e.spawnRadius = 200.0f;
    e.emissiveIntensity = 0.5f;
    e.softParticleFadeRange = 40.0f;
    e.sizeOverLifeStart = 0.5f; e.sizeOverLifePeak = 1.2f;
    e.sizeOverLifeEnd = 0.3f; e.sizeOverLifePeakTime = 0.4f;
    e.colorLifeStartR = 0.3f; e.colorLifeStartG = 1.0f;
    e.colorLifeStartB = 0.4f; e.colorLifeStartA = 1.0f;
    e.colorLifeEndR = 0.1f; e.colorLifeEndG = 0.5f;
    e.colorLifeEndB = 0.1f; e.colorLifeEndA = 0.0f;
    e.blendMode = 0;
}

/// Curl Smoke (standalone preset 9) — procedural curl noise turbulence.
inline void ApplyPreset_CurlSmoke(ParticleEmitter& e) {
    e.maxParticles = 3000;
    e.emissionRate = 200.0f;
    e.lifetime = 3.5f;
    e.lifetimeVariation = 1.5f;
    e.spawnVelocity = {0.0f, 0.0f, 150.0f};
    e.velocityVariation = 50.0f;
    e.spreadAngle = 34.4f;   // 0.6 rad
    e.gravity = {0.0f, 0.0f, 0.0f};
    e.damping = 0.1f;
    e.initialSize = 60.0f;
    e.initialColor = {0.6f, 0.6f, 0.6f, 0.8f};
    e.spawnRadius = 50.0f;
    e.softParticleFadeRange = 50.0f;
    e.curlStrength = 2.0f;
    e.curlFrequency = 0.4f;
    e.curlSpeed = 0.15f;
    e.sizeOverLifeStart = 0.3f; e.sizeOverLifePeak = 1.5f;
    e.sizeOverLifeEnd = 2.0f; e.sizeOverLifePeakTime = 0.3f;
    e.colorLifeStartR = 0.7f; e.colorLifeStartG = 0.7f;
    e.colorLifeStartB = 0.7f; e.colorLifeStartA = 0.8f;
    e.colorLifeEndR = 0.3f; e.colorLifeEndG = 0.3f;
    e.colorLifeEndB = 0.3f; e.colorLifeEndA = 0.0f;
    e.blendMode = 0;
}

/// Tornado (standalone preset 11) — vortex with tangential + radial pull.
inline void ApplyPreset_Tornado(ParticleEmitter& e) {
    e.maxParticles = 5000;
    e.emissionRate = 300.0f;
    e.lifetime = 3.0f;
    e.lifetimeVariation = 1.0f;
    e.spawnVelocity = {100.0f, 0.0f, 400.0f};
    e.velocityVariation = 150.0f;
    e.spreadAngle = 68.8f;   // 1.2 rad
    e.gravity = {0.0f, 0.0f, -50.0f};
    e.damping = 0.05f;
    e.initialSize = 25.0f;
    e.initialColor = {0.7f, 0.6f, 0.5f, 0.7f};
    e.spawnShape = 1; // Sphere
    e.spawnRadius = 200.0f;
    e.softParticleFadeRange = 40.0f;
    e.vortexTangentialStrength = 800.0f;
    e.vortexRadialStrength = 200.0f;
    e.vortexMaxRadius = 300.0f;
    e.sizeOverLifeStart = 0.4f; e.sizeOverLifePeak = 1.2f;
    e.sizeOverLifeEnd = 2.0f; e.sizeOverLifePeakTime = 0.3f;
    e.colorLifeStartR = 0.8f; e.colorLifeStartG = 0.7f;
    e.colorLifeStartB = 0.5f; e.colorLifeStartA = 0.9f;
    e.colorLifeEndR = 0.4f; e.colorLifeEndG = 0.4f;
    e.colorLifeEndB = 0.35f; e.colorLifeEndA = 0.0f;
    e.blendMode = 0;
}

/// Whirlpool (standalone preset 12) — downward spiral vortex, blue.
inline void ApplyPreset_Whirlpool(ParticleEmitter& e) {
    e.maxParticles = 3000;
    e.emissionRate = 250.0f;
    e.lifetime = 3.0f;
    e.lifetimeVariation = 1.0f;
    e.spawnVelocity = {0.0f, 0.0f, 50.0f};
    e.velocityVariation = 50.0f;
    e.spreadAngle = 45.8f;   // 0.8 rad
    e.gravity = {0.0f, 0.0f, -981.0f};
    e.damping = 0.15f;
    e.initialSize = 30.0f;
    e.initialColor = {0.2f, 0.5f, 0.9f, 0.7f};
    e.spawnRadius = 100.0f;
    e.softParticleFadeRange = 40.0f;
    e.vortexTangentialStrength = 500.0f;
    e.vortexRadialStrength = -100.0f;
    e.vortexMaxRadius = 400.0f;
    e.sizeOverLifeStart = 0.5f; e.sizeOverLifePeak = 1.0f;
    e.sizeOverLifeEnd = 0.5f; e.sizeOverLifePeakTime = 0.4f;
    e.colorLifeStartR = 0.3f; e.colorLifeStartG = 0.6f;
    e.colorLifeStartB = 1.0f; e.colorLifeStartA = 0.8f;
    e.colorLifeEndR = 0.1f; e.colorLifeEndG = 0.3f;
    e.colorLifeEndB = 0.7f; e.colorLifeEndA = 0.0f;
    e.blendMode = 0;
}

/// Windy Smoke (standalone preset 13) — wind pushes smoke sideways.
inline void ApplyPreset_WindySmoke(ParticleEmitter& e) {
    e.maxParticles = 3000;
    e.emissionRate = 200.0f;
    e.lifetime = 4.5f;
    e.lifetimeVariation = 1.5f;
    e.spawnVelocity = {0.0f, 0.0f, 200.0f};
    e.velocityVariation = 50.0f;
    e.spreadAngle = 28.6f;   // 0.5 rad
    e.gravity = {0.0f, 0.0f, -30.0f};
    e.damping = 0.12f;
    e.initialSize = 80.0f;
    e.initialColor = {0.5f, 0.5f, 0.5f, 0.6f};
    e.spawnRadius = 30.0f;
    e.softParticleFadeRange = 50.0f;
    e.curlStrength = 0.5f;
    e.curlFrequency = 0.3f;
    e.curlSpeed = 0.1f;
    e.windDirX = 1.0f; e.windDirY = 0.0f; e.windDirZ = 0.0f;
    e.windStrength = 500.0f;
    e.sizeOverLifeStart = 0.4f; e.sizeOverLifePeak = 1.5f;
    e.sizeOverLifeEnd = 2.8f; e.sizeOverLifePeakTime = 0.3f;
    e.colorLifeStartR = 0.6f; e.colorLifeStartG = 0.6f;
    e.colorLifeStartB = 0.6f; e.colorLifeStartA = 0.7f;
    e.colorLifeEndR = 0.2f; e.colorLifeEndG = 0.2f;
    e.colorLifeEndB = 0.2f; e.colorLifeEndA = 0.0f;
    e.blendMode = 0;
}

/// Lit Smoke (standalone preset 14) — directional NdotL lighting.
inline void ApplyPreset_LitSmoke(ParticleEmitter& e) {
    e.maxParticles = 3000;
    e.emissionRate = 200.0f;
    e.lifetime = 4.5f;
    e.lifetimeVariation = 1.5f;
    e.spawnVelocity = {0.0f, 0.0f, 200.0f};
    e.velocityVariation = 50.0f;
    e.spreadAngle = 34.4f;   // 0.6 rad
    e.gravity = {0.0f, 0.0f, 0.0f};
    e.damping = 0.1f;
    e.initialSize = 100.0f;
    e.initialColor = {0.8f, 0.8f, 0.8f, 1.0f};
    e.spawnRadius = 30.0f;
    e.softParticleFadeRange = 50.0f;
    e.litMode = 1;
    e.curlStrength = 0.5f;
    e.curlFrequency = 0.3f;
    e.curlSpeed = 0.1f;
    e.sizeOverLifeStart = 0.3f; e.sizeOverLifePeak = 1.5f;
    e.sizeOverLifeEnd = 2.0f; e.sizeOverLifePeakTime = 0.3f;
    e.colorLifeStartR = 0.8f; e.colorLifeStartG = 0.8f;
    e.colorLifeStartB = 0.8f; e.colorLifeStartA = 1.0f;
    e.colorLifeEndR = 0.3f; e.colorLifeEndG = 0.3f;
    e.colorLifeEndB = 0.3f; e.colorLifeEndA = 0.0f;
    e.blendMode = 0;
}

/// Lit Debris (standalone preset 15) — lit velocity-stretch with stick collision.
inline void ApplyPreset_LitDebris(ParticleEmitter& e) {
    e.maxParticles = 3000;
    e.emissionRate = 1500.0f;
    e.lifetime = 1.25f;
    e.lifetimeVariation = 0.75f;
    e.spawnVelocity = {0.0f, 0.0f, 800.0f};
    e.velocityVariation = 400.0f;
    e.spreadAngle = 85.9f;   // 1.5 rad
    e.gravity = {0.0f, 0.0f, -2000.0f};
    e.damping = 0.08f;
    e.initialSize = 40.0f;
    e.initialColor = {0.7f, 0.6f, 0.5f, 1.0f};
    e.spawnRadius = 30.0f;
    e.emissiveIntensity = 0.2f;
    e.softParticleFadeRange = 30.0f;
    e.litMode = 1;
    e.facingMode = 1; // Velocity aligned
    e.stretchFactor = 0.015f;
    e.groundCollision = 1;
    e.restitution = 0.0f;
    e.collisionResponse = 1; // Stick
    e.collisionFriction = 0.8f;
    e.sizeOverLifeStart = 0.8f; e.sizeOverLifePeak = 1.2f;
    e.sizeOverLifeEnd = 0.1f; e.sizeOverLifePeakTime = 0.1f;
    e.colorLifeStartR = 0.8f; e.colorLifeStartG = 0.7f;
    e.colorLifeStartB = 0.5f; e.colorLifeStartA = 1.0f;
    e.colorLifeEndR = 0.4f; e.colorLifeEndG = 0.35f;
    e.colorLifeEndB = 0.25f; e.colorLifeEndA = 0.0f;
    e.blendMode = 0;
}

/// Orbital (standalone preset 16) — slow cyan glow orbiting in a circle.
inline void ApplyPreset_Orbital(ParticleEmitter& e) {
    e.maxParticles = 500;
    e.emissionRate = 12.0f;
    e.lifetime = 8.0f;
    e.lifetimeVariation = 1.0f;
    e.spawnVelocity = {0.0f, 0.0f, 0.0f};
    e.velocityVariation = 0.0f;
    e.spreadAngle = 0.0f;
    e.gravity = {0.0f, 0.0f, 0.0f};
    e.damping = 0.0f;
    e.initialSize = 50.0f;
    e.initialColor = {0.4f, 0.8f, 1.0f, 1.0f};
    e.spawnRadius = 1.0f;
    e.emissiveIntensity = 2.0f;
    e.softParticleFadeRange = 30.0f;
    e.orbitEnabled = 1;
    e.orbitRadius = 300.0f;    // 3m radius — visible orbit at 15m camera distance
    e.orbitSpeed = 1.2f;
    e.sizeOverLifeStart = 0.3f; e.sizeOverLifePeak = 1.0f;
    e.sizeOverLifeEnd = 0.0f; e.sizeOverLifePeakTime = 0.2f;
    e.colorLifeStartR = 0.4f; e.colorLifeStartG = 0.8f;
    e.colorLifeStartB = 1.0f; e.colorLifeStartA = 1.0f;
    e.colorLifeEndR = 0.1f; e.colorLifeEndG = 0.3f;
    e.colorLifeEndB = 0.8f; e.colorLifeEndA = 0.0f;
    e.blendMode = 1;
}

/// Air Resist (standalone preset 17) — high drag, color-by-speed.
inline void ApplyPreset_AirResist(ParticleEmitter& e) {
    e.maxParticles = 5000;
    e.emissionRate = 500.0f;
    e.lifetime = 3.0f;
    e.lifetimeVariation = 1.0f;
    e.spawnVelocity = {0.0f, 0.0f, 1500.0f};
    e.velocityVariation = 300.0f;
    e.spreadAngle = 68.8f;   // 1.2 rad
    e.gravity = {0.0f, 0.0f, -500.0f};
    e.damping = 0.0f;  // No linear damping — drag only
    e.initialSize = 30.0f;
    e.initialColor = {1.0f, 0.9f, 0.3f, 1.0f};
    e.spawnRadius = 10.0f;
    e.emissiveIntensity = 1.0f;
    e.softParticleFadeRange = 30.0f;
    e.dragCoefficient = 3.0f;
    e.colorBySpeedEnabled = 1;
    e.sizeOverLifeStart = 0.8f; e.sizeOverLifePeak = 1.2f;
    e.sizeOverLifeEnd = 0.3f; e.sizeOverLifePeakTime = 0.15f;
    e.colorLifeStartR = 1.0f; e.colorLifeStartG = 0.9f;
    e.colorLifeStartB = 0.3f; e.colorLifeStartA = 1.0f;
    e.colorLifeEndR = 0.6f; e.colorLifeEndG = 0.1f;
    e.colorLifeEndB = 0.0f; e.colorLifeEndA = 0.3f;
    e.blendMode = 1;
}

} // namespace uz
