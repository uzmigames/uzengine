/// VFXEmitterConfig ↔ ParticleEmitter bridge implementation.
///
/// This translation unit is intentionally compiled as part of the
/// vfx_viewport demo (not the engine runtime) because VFXEmitterConfig is
/// a demo-side struct whose header is not on the engine's include path.
/// The engine provides only the bridge interface header; this .cpp provides
/// the concrete mapping once both sides are visible.
///
/// See engine/vfx/components/emitter_config_bridge.h for the full gap
/// registry documenting which VFXEmitterConfig fields have no current
/// ParticleEmitter equivalent.

#include "engine/vfx/components/emitter_config_bridge.h"
#include "vfx_viewport_vfx.h"

#include <algorithm> // std::clamp

namespace uz {

// ─────────────────────────────────────────────────────────────────────────────
// ApplyConfigToEmitter
// ─────────────────────────────────────────────────────────────────────────────

void ApplyConfigToEmitter(ParticleEmitter& emitter,
                          const VFXEmitterConfig& config,
                          float spawnRate) noexcept
{
    // ── Emission rate ────────────────────────────────────────────────────────
    // VFXPreset stores the spawn rate separately from the config struct.
    // The emitter uses it directly as particles-per-second.
    emitter.emissionRate = spawnRate;

    // ── Lifetime ─────────────────────────────────────────────────────────────
    // VFXEmitterConfig stores min/max lifetime; ParticleEmitter stores a base
    // lifetime and an additive random variation.
    //   base    = minLifetime
    //   variation = maxLifetime - minLifetime  (clamped to >= 0)
    // The compute shader subtracts random(0, variation) from the base so actual
    // lifetimes are uniformly distributed in [minLifetime, maxLifetime].
    emitter.lifetime = config.minLifetime;
    emitter.lifetimeVariation = std::max(0.0f, config.maxLifetime - config.minLifetime);

    // ── Spawn velocity ───────────────────────────────────────────────────────
    emitter.spawnVelocity = Vec3{
        config.spawnVelocity[0],
        config.spawnVelocity[1],
        config.spawnVelocity[2],
    };

    // ── Velocity variation & spread ──────────────────────────────────────────
    // VFXEmitterConfig.velocityVariation is a scalar spread applied per-axis.
    // ParticleEmitter.velocityVariation is also a scalar spread per-axis.
    emitter.velocityVariation = config.velocityVariation;

    // VFXEmitterConfig.spreadAngle is stored in radians (cone half-angle).
    // ParticleEmitter.spreadAngle is in degrees.
    // Convert: degrees = radians * (180 / pi).
    // Use a constexpr to avoid pulling in <cmath> or <numbers> just for this.
    constexpr float kRadToDeg = 57.29577951308232f; // 180 / pi
    emitter.spreadAngle = config.spreadAngle * kRadToDeg;

    // ── Gravity ───────────────────────────────────────────────────────────────
    emitter.gravity = Vec3{
        config.gravity[0],
        config.gravity[1],
        config.gravity[2],
    };

    // ── Damping ───────────────────────────────────────────────────────────────
    // VFXEmitterConfig.damping is a drag coefficient (0 = heavy damping,
    // 1 = none) applied as: velocity -= velocity * damping * dt  (i.e. linear
    // drag).  ParticleEmitter.damping is a per-frame multiplier applied as:
    // velocity *= damping  (multiplicative exponential decay).
    //
    // The two conventions are incompatible in general, but at typical frame
    // rates (dt ≈ 0.016 s) and the demo's small drag values (≈ 0.1), the
    // mapping  multiplier ≈ 1 - drag  is a reasonable first-order
    // approximation that preserves author intent when porting presets.
    //
    // An exact per-frame conversion would require the actual dt at apply-time
    // (not available here), so we use the linear approximation and clamp the
    // result to [0, 1].
    emitter.damping = std::clamp(1.0f - config.damping, 0.0f, 1.0f);

    // ── Soft particle fade range ─────────────────────────────────────────────
    // VFXEmitterConfig.softFadeDistance and ParticleEmitter.softParticleFadeRange
    // both represent the world-space depth fade distance (same semantics).
    emitter.softParticleFadeRange = config.softFadeDistance;

    // ── Emission state ───────────────────────────────────────────────────────
    // isEmitting is kept at whatever the caller set (defaults to true) unless
    // the config has an emitterDuration that would cause it to start disabled —
    // that lifecycle state lives in VFXEmitterInstance, not in the config, so
    // we leave isEmitting unchanged.
    // (No config field maps directly to isEmitting.)

    // ── Start delay ───────────────────────────────────────────────────────────
    // The config has no startDelay field; emitter.startDelay stays at 0.
    // (The demo controls stagger through preset ordering / spawn timing.)

    // ── LOD ──────────────────────────────────────────────────────────────────
    // VFXEmitterConfig carries no LOD fields; leave lodEnabled / lodSettings
    // at their defaults (Full quality).

    // ── Priority ─────────────────────────────────────────────────────────────
    // VFXEmitterConfig has no explicit priority; leave emitter.priority = 50
    // (middle of range, already the default).

    // ── maxParticles ─────────────────────────────────────────────────────────
    // Not in VFXEmitterConfig (the demo sets buffer sizes separately via
    // VFXEmitterInstance::Create). Leave emitter.maxParticles at its default
    // (10 000) so the caller can override it before calling Initialize().
}

// ─────────────────────────────────────────────────────────────────────────────
// ExtractConfigFromEmitter
// ─────────────────────────────────────────────────────────────────────────────

void ExtractConfigFromEmitter(VFXEmitterConfig& config,
                              float& spawnRate,
                              const ParticleEmitter& emitter) noexcept
{
    // ── Emission rate ────────────────────────────────────────────────────────
    spawnRate = emitter.emissionRate;

    // ── Lifetime ─────────────────────────────────────────────────────────────
    // Reconstruct [min, max] from base + variation:
    //   minLifetime = base
    //   maxLifetime = base + variation
    config.minLifetime = emitter.lifetime;
    config.maxLifetime = emitter.lifetime + emitter.lifetimeVariation;

    // ── Spawn velocity ───────────────────────────────────────────────────────
    config.spawnVelocity[0] = emitter.spawnVelocity.x;
    config.spawnVelocity[1] = emitter.spawnVelocity.y;
    config.spawnVelocity[2] = emitter.spawnVelocity.z;

    // ── Velocity variation & spread ──────────────────────────────────────────
    config.velocityVariation = emitter.velocityVariation;

    // Reverse the deg→rad conversion applied in ApplyConfigToEmitter.
    constexpr float kDegToRad = 0.017453292519943295f; // pi / 180
    config.spreadAngle = emitter.spreadAngle * kDegToRad;

    // ── Gravity ───────────────────────────────────────────────────────────────
    config.gravity[0] = emitter.gravity.x;
    config.gravity[1] = emitter.gravity.y;
    config.gravity[2] = emitter.gravity.z;

    // ── Damping ───────────────────────────────────────────────────────────────
    // Reverse the linear approximation: drag ≈ 1 - multiplier.
    config.damping = std::clamp(1.0f - emitter.damping, 0.0f, 1.0f);

    // ── Soft particle fade range ─────────────────────────────────────────────
    config.softFadeDistance = emitter.softParticleFadeRange;

    // ── Fields with no ParticleEmitter equivalent ────────────────────────────
    // All other VFXEmitterConfig fields (spawn shape, color/size over lifetime,
    // curl noise, vortex, collision, orbit, etc.) retain the values they had in
    // `config` before this call.  The caller must supply those separately if
    // needed (e.g., by reading from the original preset file).
}

} // namespace uz
