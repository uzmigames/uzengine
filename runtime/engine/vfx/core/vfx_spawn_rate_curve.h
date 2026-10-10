#pragma once

/// Spawn-rate ramp table — a tiny 4-point piecewise-linear curve for
/// modulating emission rate over the emitter's life.
///
/// Interim deliverable of phase 7.20.0. Full Hermite `VfxCurve` arrives
/// with the curve editor (7.16.0); until then, a fixed-size 4-point table
/// covers the common authoring cases (ramp-up → plateau → ramp-down,
/// pulses, cooldown) and keeps the CB layout tiny and deterministic.
///
/// # Time axis
///
/// Normalised to the emitter's lifetime: `t = 0` at spawn, `t = 1` at
/// `emitterDuration`. Values outside `[0, 1]` clamp to the first/last
/// entry. Looping emitters re-enter `t = 0` on each loop; the curve does
/// not need to wrap itself.
///
/// # Example
///
/// ```cpp
/// VfxSpawnRateCurve ramp;
/// ramp.keys[0] = { 0.0f, 0.0f };     // fade in
/// ramp.keys[1] = { 0.2f, 1.0f };     // plateau
/// ramp.keys[2] = { 0.8f, 1.0f };
/// ramp.keys[3] = { 1.0f, 0.0f };     // fade out
/// const float mul = ramp.Evaluate(0.5f); // -> 1.0
/// ```
///
/// @ref phase7_7.20.0-vfx-velocity-inheritance-spawn-curves

#include <cstdint>

namespace uz {

/// Single ramp key — normalised time in [0, 1] and a scalar multiplier
/// applied on top of `ParticleEmitter::emissionRate`.
struct VfxSpawnRateKey {
    float time       = 0.0f;   ///< In [0, 1].
    float multiplier = 1.0f;   ///< Rate scalar; 1.0 = unchanged.
};

/// Fixed-size 4-point spawn-rate ramp. Values between keys use linear
/// interpolation. Disabled by default (all keys evaluate to 1.0) so
/// emitters ignore the curve unless authored.
struct VfxSpawnRateCurve {
    VfxSpawnRateKey keys[4] {
        {0.0f, 1.0f}, {0.0f, 1.0f}, {0.0f, 1.0f}, {0.0f, 1.0f}
    };
    bool enabled = false;

    /// Evaluates the curve at normalised time `t`. Clamps out-of-range
    /// inputs to the first/last key. When `enabled == false` returns
    /// 1.0 unconditionally.
    [[nodiscard]] float Evaluate(float t) const noexcept;

    /// Convenience: populate a standard fade-in / plateau / fade-out
    /// shape with user-tunable plateau boundaries.
    ///
    /// - `rampInEnd`:  time at which the rate reaches 1.0 (e.g. 0.1).
    /// - `rampOutStart`: time at which fade-out begins (e.g. 0.9).
    /// - Both clamped to [0, 1]; caller responsibility to keep
    ///   `rampInEnd <= rampOutStart`.
    void SetFadeInOut(float rampInEnd, float rampOutStart) noexcept;
};

} // namespace uz
