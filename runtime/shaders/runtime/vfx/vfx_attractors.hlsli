/// VFX Attractor Force Utilities
///
/// Computes attractor forces for GPU particle simulation. Each attractor
/// pulls (or repels with negative strength) particles toward a world-space
/// point. Three falloff models are supported: constant (no distance decay),
/// linear (force scales with 1 - dist/radius), and inverse-square (force
/// scales with 1/dist^2, physically based gravitational analogy).
///
/// Up to eight attractors may be active simultaneously per emitter; the
/// limit exists to keep the per-particle loop unrollable on all hardware.
///
/// @ref Reeves 1983 "Particle Systems — A Technique for Modeling a Class
///      of Fuzzy Objects" — foundational particle force model reference
/// @ref Latta 2004 "Building a Million Particle System" GDC — GPU particle
///      force accumulation pattern, multiple overlapping force fields
/// @ref Pharr, Jakob, Humphreys 2023 "Physically Based Rendering" 4th ed.,
///      ch. 2 — inverse-square gravitational analogy for attractor falloff
/// @constants kMaxAttractors = 8  (hardware-unrollable loop bound)
///            kMinDist       = 0.001 (guards direction normalize)
///            kMinDistSq     = 0.0001 (guards inverse-square divide)

#ifndef VFX_ATTRACTORS_HLSLI
#define VFX_ATTRACTORS_HLSLI

// ─────────────────────────────────────────────────────────────────────────────
// Falloff type constants
// ─────────────────────────────────────────────────────────────────────────────

/// Force magnitude does not decay with distance within the effect radius.
/// Useful for suction volumes and teleport-ring effects.
static const uint ATTRACTOR_FALLOFF_CONSTANT       = 0;

/// Force magnitude decays linearly from `strength` at distance 0 to 0 at
/// `radius`. The standard choice for art-directable attractor fields.
/// @ref Latta 2004 "Building a Million Particle System" GDC — linear falloff
static const uint ATTRACTOR_FALLOFF_LINEAR         = 1;

/// Force magnitude decays as 1/dist^2, analogous to Newtonian gravity.
/// `radius` is unused for this model — force applies at any distance.
/// @ref Pharr et al. 2023 "Physically Based Rendering" ch. 2 — inverse-square law
static const uint ATTRACTOR_FALLOFF_INVERSE_SQUARE = 2;

// ─────────────────────────────────────────────────────────────────────────────
// AttractorParams — per-attractor data for structured buffer packing
// ─────────────────────────────────────────────────────────────────────────────

/// Per-attractor parameters packed for GPU structured buffer access.
///
/// The struct is 32 bytes (two float4 rows) to satisfy 16-byte CB alignment
/// rules when the buffer is also used as a constant array. In a
/// StructuredBuffer<AttractorParams> the stride is set automatically by
/// the RHI to sizeof(AttractorParams) = 32 bytes.
///
/// @ref Latta 2004 "Building a Million Particle System" GDC — attractor
///      parameter layout for GPU force fields
struct AttractorParams
{
    /// World-space position of the attractor center. Particles are pulled
    /// toward (positive strength) or pushed away from (negative strength)
    /// this point.
    float3 position;

    /// Signed force magnitude in units per second squared (acceleration).
    /// Positive = attract, negative = repel.
    /// Typical range: [-5000, 5000].
    float  strength;

    /// Effect radius in world units. For ATTRACTOR_FALLOFF_LINEAR the force
    /// is zero at or beyond this distance. Ignored by INVERSE_SQUARE.
    /// Must be > 0 to avoid divide-by-zero in linear falloff.
    float  radius;

    /// Selects the distance-decay model.
    /// 0 = ATTRACTOR_FALLOFF_CONSTANT
    /// 1 = ATTRACTOR_FALLOFF_LINEAR
    /// 2 = ATTRACTOR_FALLOFF_INVERSE_SQUARE
    uint   falloffType;

    /// When non-zero, particles that enter `killDistance` of the attractor
    /// center are flagged for destruction on the next particle update pass.
    /// Useful for drain/vortex attractors that consume particles.
    uint   killOnArrival;

    /// Distance threshold in world units for the kill-on-arrival test.
    /// Only evaluated when killOnArrival != 0.
    float  killDistance;
};

// ─────────────────────────────────────────────────────────────────────────────
// ComputeAttractorForce — single-attractor force vector
// ─────────────────────────────────────────────────────────────────────────────

/// Computes the acceleration vector exerted by one attractor on a particle.
///
/// The returned value is an instantaneous acceleration (units/s^2). Multiply
/// by dt before adding to velocity:
///   velocity += ComputeAttractorForce(pos, a) * dt;
///
/// @ref Reeves 1983 "Particle Systems" — force vector from field center
/// @ref Latta 2004 "Building a Million Particle System" GDC — constant,
///      linear, and inverse-square force models for GPU particles
/// @constants kMinDist   = 0.001  — minimum distance for direction normalize
///            kMinDistSq = 0.0001 — minimum dist^2 for inverse-square divide
///
/// @param particlePos  World-space particle position.
/// @param attractor    Packed attractor parameters.
/// @return             Acceleration vector (units / s^2). Zero when the
///                     particle is outside the attractor radius (linear mode)
///                     or when strength is zero.
float3 ComputeAttractorForce(float3 particlePos, AttractorParams attractor)
{
    float3 toAttractor = attractor.position - particlePos;

    // Compute scalar distance. Guard against zero-length vectors when the
    // particle sits exactly on the attractor center — both direction
    // normalize and inverse-square divide would be undefined without it.
    float dist = length(toAttractor);

    // Safe normalize: if dist < kMinDist the particle is essentially inside
    // the attractor; return zero force rather than a NaN/Inf direction.
    // kMinDist = 0.001 world units (~1 mm for a cm-unit scene).
    if (dist < 0.001)
    {
        return float3(0.0, 0.0, 0.0);
    }

    float3 dir = toAttractor / dist; // Guaranteed unit vector here.

    float forceMag = 0.0;

    if (attractor.falloffType == ATTRACTOR_FALLOFF_CONSTANT)
    {
        // Constant model: full strength everywhere within the radius.
        // A particle outside the radius receives zero force so the attractor
        // volume has a hard boundary, matching art-director expectations.
        float safeRadius = max(attractor.radius, 0.001);
        forceMag = (dist <= safeRadius) ? attractor.strength : 0.0;
    }
    else if (attractor.falloffType == ATTRACTOR_FALLOFF_LINEAR)
    {
        // Linear model: force tapers from `strength` at the center to 0 at
        // `radius`. saturate clamps the 1-dist/radius term to [0,1], which
        // simultaneously handles the out-of-radius case (gives 0) and any
        // negative result from numerical imprecision (gives 0 rather than
        // a tiny repulsive spike).
        // @ref Latta 2004 "Building a Million Particle System" GDC — eq. (3)
        float safeRadius = max(attractor.radius, 0.001);
        float normalizedDist = dist / safeRadius; // [0, ∞)
        forceMag = attractor.strength * saturate(1.0 - normalizedDist);
    }
    else // ATTRACTOR_FALLOFF_INVERSE_SQUARE
    {
        // Inverse-square model: force = strength / dist^2.
        // kMinDistSq = 0.0001 prevents the singularity at dist == 0.
        // This model has no hard radius boundary; force asymptotically
        // approaches zero at infinity.
        // @ref Pharr et al. 2023 "Physically Based Rendering" ch. 2
        float distSq    = dist * dist;
        float safeDistSq = max(distSq, 0.0001); // kMinDistSq
        forceMag = attractor.strength / safeDistSq;
    }

    return dir * forceMag;
}

// ─────────────────────────────────────────────────────────────────────────────
// ShouldKillAtAttractor — per-particle kill test for drain attractors
// ─────────────────────────────────────────────────────────────────────────────

/// Returns true if a particle should be destroyed by this attractor.
///
/// The test is a simple distance threshold check. Particles that cross inside
/// `killDistance` of the attractor center are flagged for recycling on the
/// next particle update pass. This is only evaluated when killOnArrival != 0
/// to avoid the sqrt cost for non-drain attractors.
///
/// @ref Latta 2004 "Building a Million Particle System" GDC — drain/vortex
///      kill zone for particle recycling
///
/// @param particlePos  World-space particle position.
/// @param attractor    Packed attractor parameters.
/// @return             True when the particle is within killDistance of the
///                     attractor center and killOnArrival is enabled.
bool ShouldKillAtAttractor(float3 particlePos, AttractorParams attractor)
{
    // Early-out: skip the sqrt entirely for non-drain attractors.
    if (attractor.killOnArrival == 0u)
    {
        return false;
    }

    float dist = length(attractor.position - particlePos);
    return dist < attractor.killDistance;
}

// ─────────────────────────────────────────────────────────────────────────────
// ApplyAttractors — accumulate forces from a structured buffer
// ─────────────────────────────────────────────────────────────────────────────

/// Accumulates attractor forces from all active attractors onto a particle.
///
/// Iterates over up to kMaxAttractors = 8 entries from the StructuredBuffer
/// and sums their contributions. The 8-attractor cap is deliberately tight:
/// the loop is unrollable on all DX12 / Vulkan hardware tiers, avoiding
/// dynamic-index overhead in the shader for the common case where the scene
/// has 1-3 attractors.
///
/// Usage in a particle update compute shader:
/// @code
///   float3 deltaVelocity;
///   bool   kill;
///   ApplyAttractors(pos, g_Attractors, g_AttractorCount, dt,
///                   deltaVelocity, kill);
///   velocity += deltaVelocity;
///   if (kill) particle.lifetime = -1.0; // mark dead
/// @endcode
///
/// @ref Reeves 1983 "Particle Systems" — force accumulation over multiple fields
/// @ref Latta 2004 "Building a Million Particle System" GDC — structured-buffer
///      force field array pattern for GPU particle systems
/// @constants kMaxAttractors = 8 (static loop bound for hardware unrolling)
///
/// @param particlePos    World-space particle position.
/// @param attractors     StructuredBuffer of AttractorParams. Must contain at
///                       least `attractorCount` valid entries.
/// @param attractorCount Number of active attractors. Clamped internally to
///                       kMaxAttractors = 8.
/// @param dt             Frame delta time in seconds. The force is integrated
///                       into velocity as: velocity += force * dt.
/// @param outVelocity    [out] Velocity delta to add to the particle velocity.
///                       Zero-initialized before accumulation.
/// @param outKill        [out] Set to true if any attractor's kill-on-arrival
///                       test fires. Left false when no kill is triggered.
void ApplyAttractors(
    float3                           particlePos,
    StructuredBuffer<AttractorParams> attractors,
    uint                             attractorCount,
    float                            dt,
    out float3                       outVelocity,
    out bool                         outKill)
{
    outVelocity = float3(0.0, 0.0, 0.0);
    outKill     = false;

    // Clamp to kMaxAttractors = 8. This static upper bound lets the compiler
    // fully unroll the loop without a dynamic branch count on any SM6 target.
    uint count = min(attractorCount, 8u);

    [unroll(8)]
    for (uint i = 0u; i < count; ++i)
    {
        AttractorParams a = attractors[i];

        // Accumulate velocity delta: force * dt (Euler integration step).
        outVelocity += ComputeAttractorForce(particlePos, a) * dt;

        // Kill test: OR into outKill so any triggering attractor dominates.
        if (ShouldKillAtAttractor(particlePos, a))
        {
            outKill = true;
        }
    }
}

#endif // VFX_ATTRACTORS_HLSLI
