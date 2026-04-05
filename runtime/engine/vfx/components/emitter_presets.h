/// VFX Emitter Presets — factory functions for common particle effects.
///
/// Each preset configures a ParticleEmitter with parameters matching the
/// standalone VFX demo's VFXEmitterConfig presets. Use these to quickly
/// set up fire, smoke, sparks, explosions, etc. in the engine Application.
///
/// Usage:
/// ```cpp
/// auto* emitter = ...; // from SyncVfxEmitters
/// ApplyPreset_Fire(*emitter);
/// ```

#pragma once

#include "particle_emitter.h"

namespace uz {

/// Fire fountain — warm upward particles with size fade and glow.
inline void ApplyPreset_Fire(ParticleEmitter& e) {
    e.maxParticles = 5000;
    e.emissionRate = 200.0f;
    e.lifetime = 2.0f;
    e.lifetimeVariation = 0.5f;
    e.spawnVelocity = {0.0f, 0.0f, 300.0f};
    e.velocityVariation = 50.0f;
    e.spreadAngle = 15.0f;
    e.gravity = {0.0f, 0.0f, -200.0f};
    e.damping = 0.95f;
    e.initialSize = 15.0f;
    e.initialColor = {1.0f, 0.6f, 0.2f, 0.8f};
    e.spawnShape = 0; // Point
    e.spawnRadius = 5.0f;
    e.emissiveIntensity = 2.0f;
    e.sizeOverLifeStart = 1.0f;
    e.sizeOverLifePeak = 1.2f;
    e.sizeOverLifeEnd = 0.0f;
    e.sizeOverLifePeakTime = 0.2f;
    e.colorLifeStartR = 1.0f; e.colorLifeStartG = 0.8f;
    e.colorLifeStartB = 0.3f; e.colorLifeStartA = 1.0f;
    e.colorLifeEndR = 1.0f; e.colorLifeEndG = 0.2f;
    e.colorLifeEndB = 0.0f; e.colorLifeEndA = 0.0f;
    e.blendMode = 1; // Additive
    e.softParticleFadeRange = 50.0f;
}

/// Smoke plume — slow rising dark particles with curl noise.
inline void ApplyPreset_Smoke(ParticleEmitter& e) {
    e.maxParticles = 3000;
    e.emissionRate = 100.0f;
    e.lifetime = 4.0f;
    e.lifetimeVariation = 1.0f;
    e.spawnVelocity = {0.0f, 0.0f, 150.0f};
    e.velocityVariation = 30.0f;
    e.spreadAngle = 20.0f;
    e.gravity = {0.0f, 0.0f, 50.0f}; // slight updraft
    e.damping = 0.92f;
    e.initialSize = 20.0f;
    e.initialColor = {0.3f, 0.3f, 0.3f, 0.5f};
    e.spawnShape = 1; // Sphere
    e.spawnRadius = 10.0f;
    e.emissiveIntensity = 0.0f;
    e.sizeOverLifeStart = 0.5f;
    e.sizeOverLifePeak = 1.5f;
    e.sizeOverLifeEnd = 2.0f;
    e.sizeOverLifePeakTime = 0.5f;
    e.colorLifeStartR = 0.4f; e.colorLifeStartG = 0.4f;
    e.colorLifeStartB = 0.4f; e.colorLifeStartA = 0.6f;
    e.colorLifeEndR = 0.2f; e.colorLifeEndG = 0.2f;
    e.colorLifeEndB = 0.2f; e.colorLifeEndA = 0.0f;
    e.blendMode = 0; // AlphaBlend
    e.curlStrength = 2.0f;
    e.curlFrequency = 0.5f;
    e.curlSpeed = 0.3f;
}

/// Sparks — fast small bright particles with ground collision.
inline void ApplyPreset_Sparks(ParticleEmitter& e) {
    e.maxParticles = 2000;
    e.emissionRate = 150.0f;
    e.lifetime = 1.5f;
    e.lifetimeVariation = 0.5f;
    e.spawnVelocity = {0.0f, 0.0f, 800.0f};
    e.velocityVariation = 200.0f;
    e.spreadAngle = 45.0f;
    e.gravity = {0.0f, 0.0f, -981.0f};
    e.damping = 0.99f;
    e.initialSize = 3.0f;
    e.initialColor = {1.0f, 0.8f, 0.3f, 1.0f};
    e.spawnShape = 0;
    e.emissiveIntensity = 3.0f;
    e.sizeOverLifeStart = 1.0f;
    e.sizeOverLifePeak = 0.8f;
    e.sizeOverLifeEnd = 0.0f;
    e.sizeOverLifePeakTime = 0.1f;
    e.colorLifeStartR = 1.0f; e.colorLifeStartG = 0.9f;
    e.colorLifeStartB = 0.5f; e.colorLifeStartA = 1.0f;
    e.colorLifeEndR = 1.0f; e.colorLifeEndG = 0.3f;
    e.colorLifeEndB = 0.0f; e.colorLifeEndA = 0.0f;
    e.blendMode = 1; // Additive
    e.groundCollision = 1;
    e.restitution = 0.4f;
    e.collisionFriction = 0.6f;
    e.dragCoefficient = 0.5f;
}

/// Simple fountain — upward stream with gravity arc.
inline void ApplyPreset_Fountain(ParticleEmitter& e) {
    e.maxParticles = 10000;
    e.emissionRate = 300.0f;
    e.lifetime = 3.0f;
    e.spawnVelocity = {0.0f, 0.0f, 500.0f};
    e.velocityVariation = 50.0f;
    e.spreadAngle = 10.0f;
    e.gravity = {0.0f, 0.0f, -981.0f};
    e.damping = 0.99f;
    e.initialSize = 8.0f;
    e.initialColor = {0.5f, 0.7f, 1.0f, 0.8f};
    e.spawnShape = 0;
    e.sizeOverLifeStart = 1.0f;
    e.sizeOverLifePeak = 1.0f;
    e.sizeOverLifeEnd = 0.3f;
    e.sizeOverLifePeakTime = 0.5f;
    e.colorLifeStartR = 0.6f; e.colorLifeStartG = 0.8f;
    e.colorLifeStartB = 1.0f; e.colorLifeStartA = 1.0f;
    e.colorLifeEndR = 0.3f; e.colorLifeEndG = 0.5f;
    e.colorLifeEndB = 1.0f; e.colorLifeEndA = 0.0f;
    e.softParticleFadeRange = 30.0f;
}

/// Magic orb — orbiting particles with vortex.
inline void ApplyPreset_MagicOrb(ParticleEmitter& e) {
    e.maxParticles = 3000;
    e.emissionRate = 200.0f;
    e.lifetime = 2.0f;
    e.spawnVelocity = {0.0f, 0.0f, 50.0f};
    e.velocityVariation = 20.0f;
    e.spreadAngle = 90.0f;
    e.gravity = {0.0f, 0.0f, 0.0f};
    e.damping = 0.95f;
    e.initialSize = 5.0f;
    e.initialColor = {0.5f, 0.2f, 1.0f, 0.9f};
    e.spawnShape = 1; // Sphere
    e.spawnRadius = 30.0f;
    e.emissiveIntensity = 2.5f;
    e.orbitEnabled = 1;
    e.orbitRadius = 50.0f;
    e.orbitSpeed = 3.0f;
    e.sizeOverLifeEnd = 0.0f;
    e.colorLifeStartR = 0.6f; e.colorLifeStartG = 0.3f;
    e.colorLifeStartB = 1.0f; e.colorLifeStartA = 1.0f;
    e.colorLifeEndR = 0.2f; e.colorLifeEndG = 0.0f;
    e.colorLifeEndB = 0.8f; e.colorLifeEndA = 0.0f;
    e.blendMode = 1; // Additive
}

/// Instant burst — all particles spawn at once.
inline void ApplyPreset_Burst(ParticleEmitter& e) {
    e.maxParticles = 500;
    e.emissionRate = 50000.0f; // burst: all in one frame
    e.lifetime = 2.0f;
    e.lifetimeVariation = 0.5f;
    e.spawnVelocity = {0.0f, 0.0f, 400.0f};
    e.velocityVariation = 150.0f;
    e.spreadAngle = 90.0f; // hemisphere
    e.gravity = {0.0f, 0.0f, -981.0f};
    e.damping = 0.97f;
    e.initialSize = 10.0f;
    e.initialColor = {1.0f, 1.0f, 0.8f, 1.0f};
    e.emissiveIntensity = 1.5f;
    e.sizeOverLifeEnd = 0.0f;
    e.colorLifeEndA = 0.0f;
    e.blendMode = 1;
}

} // namespace uz
