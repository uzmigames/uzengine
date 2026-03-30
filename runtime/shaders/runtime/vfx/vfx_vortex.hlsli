/// VFX Vortex Force — Tangential + Radial Force Around an Axis
///
/// Computes the instantaneous vortex force on a particle for one simulation step.
/// The force has two orthogonal components:
///   - Tangential: spins the particle around the axis (tornado/whirlpool swirl).
///   - Radial: pulls toward or pushes away from the axis (spiral tightening/loosening).
///
/// The force magnitude falls off linearly from the axis center to `maxRadius`.
/// Particles outside `maxRadius` are unaffected.
///
/// @ref Stomakhin 2013 "A Material Point Method for Snow Simulation" Appendix A —
///      vortex field formulation: F = tangent * omega + radial * pull
/// @constants vortex: tangential strength (m/s^2), radial strength (m/s^2), maxRadius (m)
/// @deviations Linear radial falloff (1 - dist/maxRadius) instead of quadratic for
///             performance. Consistent with Niagara's Vortex Force module behavior.

#ifndef VFX_VORTEX_HLSLI
#define VFX_VORTEX_HLSLI

/// Computes the vortex force vector applied to a particle in world space.
///
/// # Parameters
/// - `pos`                  : Current particle world-space position.
/// - `axisOrigin`           : World-space origin of the vortex axis.
/// - `axisDir`              : Normalized world-space direction of the vortex axis.
/// - `tangentialStrength`   : Magnitude of the tangential (swirl) force (m/s^2).
///                            Sign controls CCW (+) or CW (-) rotation viewed from axisDir tip.
/// - `radialStrength`       : Magnitude of the radial force (m/s^2).
///                            Positive = pull toward axis (inward spiral).
///                            Negative = push away from axis (outward spiral).
/// - `maxRadius`            : Cutoff radius in world units. Particles farther than this
///                            receive zero vortex force.
///
/// # Returns
/// Force vector (m/s^2) to be multiplied by `dt` and added to particle velocity.
///
/// @ref Stomakhin 2013 Appendix A — vortex tangential + radial decomposition
float3 ComputeVortexForce(
    float3 pos,
    float3 axisOrigin,
    float3 axisDir,           // Must be normalized by caller
    float  tangentialStrength,
    float  radialStrength,
    float  maxRadius)
{
    // Vector from axis origin to particle in world space
    float3 toParticle = pos - axisOrigin;

    // Project toParticle onto the axis to get the axial component.
    // axialComponent is the portion of toParticle parallel to the axis.
    float3 axialComponent = dot(toParticle, axisDir) * axisDir;

    // Radial vector: the portion of toParticle perpendicular to the axis.
    // This points from the axis line to the particle.
    // @ref Stomakhin 2013 — radialVec = toParticle - (toParticle . axis) * axis
    float3 radialVec = toParticle - axialComponent;

    // Radial distance from the axis
    float dist = length(radialVec);

    // Guard: particles on or beyond the axis/radius receive no vortex force.
    // 0.0001 avoids division by zero for particles exactly on the axis.
    if (dist < 0.0001f || dist > maxRadius) {
        return float3(0.0f, 0.0f, 0.0f);
    }

    // Normalized direction from axis to particle (radial direction)
    float3 radialDir = radialVec / dist;

    // Tangential direction: perpendicular to both the axis and the radial direction.
    // cross(axis, radial) gives CCW rotation when viewed from the axisDir tip.
    // @ref Stomakhin 2013 — tangent = normalize(cross(axisDir, radialDir))
    float3 tangentDir = normalize(cross(axisDir, radialDir));

    // Linear falloff: force decreases linearly from full strength at dist=0
    // to zero at dist=maxRadius.
    float falloff = 1.0f - saturate(dist / maxRadius);

    // Combine tangential and radial components.
    // Positive radialStrength pulls inward (-radialDir) for spiral tightening.
    // Negative radialStrength pushes outward (+radialDir) for spiral loosening.
    return (tangentDir * tangentialStrength - radialDir * radialStrength) * falloff;
}

#endif // VFX_VORTEX_HLSLI
