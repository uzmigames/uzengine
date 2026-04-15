#pragma once

/// Closed-form analytical motion for stateless particle emitters
/// (Niagara-parity UE5.3+ `UNiagaraStatelessEmitter`).
///
/// Stateful emitters integrate position/velocity every frame in the update
/// compute shader. Stateless emitters skip the update pass entirely — the
/// billboard VS evaluates `position(t)` directly from spawn-time
/// parameters using a closed-form formula. That saves the entire update CS
/// dispatch cost for effects with simple ballistic motion (muzzle flashes,
/// spark showers, pickup bursts).
///
/// This header ships the CPU reference implementation used by unit tests
/// and future CPU-preview tooling. The HLSL port lives in
/// `shaders/runtime/vfx/particle_stateless_vs.hlsl` (follow-up 7.5.1).
///
/// # Supported physics
///
/// The model covers:
///   - Initial position + velocity.
///   - Constant gravity.
///   - Exponential linear damping.
///
/// ```
/// v(t) = v0 * exp(-damping * t)
/// p(t) = p0 + (v0 / damping) * (1 - exp(-damping * t)) + 0.5 * g * t^2
/// ```
///
/// When `damping == 0` the velocity term collapses to the standard
/// ballistic formula (`p = p0 + v0 * t + 0.5 * g * t^2`).
///
/// # Example
///
/// ```cpp
/// VfxStatelessParticle p;
/// p.spawnPosition = {0, 0, 0};
/// p.spawnVelocity = {500, 0, 0};      // 5 m/s along +X at spawn
/// p.gravity       = {0, 0, -980};     // standard gravity
/// p.damping       = 0.0f;
/// const Vec3 pos = VfxStatelessEvaluatePosition(p, /*t=*/ 0.5f);
/// ```
///
/// @ref UE5 UNiagaraStatelessEmitter.h

#include "vector3.h"

namespace uz {

/// Spawn-time parameters describing a stateless particle's closed-form motion.
///
/// Must be trivially copyable so the same struct can be uploaded to a GPU
/// structured buffer once per spawn and read by the VS every frame.
struct VfxStatelessParticle {
    Vec3  spawnPosition {};                 ///< World or local position at t=0.
    Vec3  spawnVelocity {};                 ///< Velocity at t=0 (cm/s).
    Vec3  gravity       {0.0f, 0.0f, 0.0f}; ///< Constant accel (cm/s^2). -980Z for realistic gravity.
    float spawnTime     {0.0f};             ///< Wall-clock `totalTime` at spawn.
    float lifetime      {1.0f};             ///< Seconds the particle lives (for fade + discard).
    float damping       {0.0f};             ///< Linear damping coefficient. 0 = no drag.
    float _pad          {0.0f};             ///< 16-byte aligned; mirrors HLSL struct layout.
};

/// Emitter-level simulation mode shared with the HLSL cbuffer. Kept as a
/// typed enum so render graph branches stay legible.
enum class VfxEmitterMode : uint8_t {
    /// Default. Runs the standard spawn + update CS pipeline.
    Stateful  = 0,

    /// Skips the update CS entirely. The billboard VS evaluates the
    /// analytical `position(t)` from per-particle spawn data.
    Stateless = 1,
};

/// Human-readable string form for logging and JSON round-trip.
[[nodiscard]] constexpr const char* VfxEmitterModeToString(VfxEmitterMode m) noexcept {
    switch (m) {
        case VfxEmitterMode::Stateful:  return "Stateful";
        case VfxEmitterMode::Stateless: return "Stateless";
    }
    return "Unknown";
}

/// CPU reference implementation of the stateless position formula.
///
/// `t` is seconds since spawn (i.e. `currentTime - spawnTime`). Negative
/// inputs are clamped to 0 (particle not yet born). Values beyond
/// `lifetime` are NOT clamped — the caller is responsible for discarding
/// dead particles; this function still returns a sensible trajectory for
/// debugging / editor scrubbing.
///
/// Mirrors the HLSL VS implementation byte-for-byte so unit tests can
/// lock the expected behaviour.
[[nodiscard]] Vec3 VfxStatelessEvaluatePosition(const VfxStatelessParticle& p,
                                                float t) noexcept;

/// CPU reference implementation of the stateless velocity formula.
[[nodiscard]] Vec3 VfxStatelessEvaluateVelocity(const VfxStatelessParticle& p,
                                                float t) noexcept;

/// Normalised age in `[0, 1]`. Returns values > 1 when `t` exceeds
/// `lifetime` so fade-out shaders can see the overflow.
[[nodiscard]] float VfxStatelessNormalizedAge(const VfxStatelessParticle& p,
                                              float t) noexcept;

} // namespace uz
