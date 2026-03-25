/// VFX Vector Field Sampling Utilities
///
/// Provides functions for sampling 3D vector field textures to apply
/// continuous velocity forces to GPU particles. A vector field stores a
/// directional velocity at every voxel of a 3D texture; particles inside
/// the field's axis-aligned bounding box sample this texture and receive
/// a velocity contribution each simulation tick.
///
/// Two sampling modes are supported:
///   - Static:   SampleVectorField     — samples UVW directly from world position.
///   - Animated: SampleAnimatedVectorField — scrolls the W (Z) coordinate by
///               a time offset so a looping 3D texture appears to advect over
///               time without requiring runtime re-baking.
///
/// The tightness parameter controls how the field velocity is applied:
///   tightness == 0  → additive:  velocity += fieldVel * dt
///   tightness  > 0  → blending:  velocity  = lerp(velocity, fieldVel,
///                                                  tightness * dt)
/// Blending mode can snap fast-moving particles to the field flow while
/// additive mode allows the field to steer without overriding momentum.
///
/// @ref Stam 1999 "Stable Fluids" — 3D velocity field representation and
///      advection by sampling a divergence-free vector field texture
/// @ref Bridson 2015 "Fluid Simulation for Computer Graphics" 2nd ed.,
///      ch. 3 — 3D texture-based velocity fields for particle animation
/// @ref Latta 2004 "Building a Million Particle System" GDC — structured
///      force field integration pattern for GPU particle systems
/// @constants kMinBoundsExtent = 0.001 (guards zero-size AABB divide)

#ifndef VFX_VECTOR_FIELD_HLSLI
#define VFX_VECTOR_FIELD_HLSLI

// ─────────────────────────────────────────────────────────────────────────────
// VectorFieldParams — per-field parameters for constant buffer upload
// ─────────────────────────────────────────────────────────────────────────────

/// Per-field parameters packed for GPU constant buffer or structured buffer
/// access. The struct is 48 bytes (three float4 rows) which satisfies 16-byte
/// CB alignment rules.
///
/// boundsMin and boundsMax define the world-space AABB in which the field is
/// active. A particle outside this box is left entirely unaffected.
///
/// @ref Stam 1999 "Stable Fluids" — vector field bounding volume representation
/// @ref Bridson 2015 "Fluid Simulation for Computer Graphics" ch. 3 — AABB
///      containment test for velocity field domain
struct VectorFieldParams
{
    /// World-space AABB minimum corner. Typically fieldOffset - bounds * 0.5.
    float3 boundsMin;

    /// Force magnitude multiplier applied to the sampled field velocity.
    /// At 1.0 the raw texture values are used unchanged. Negative values
    /// invert the field direction. Typical range: [-10, 10].
    float  intensity;

    /// World-space AABB maximum corner. Typically fieldOffset + bounds * 0.5.
    float3 boundsMax;

    /// Velocity blending factor in [0, 1].
    ///   0 = additive mode:  velocity += fieldVel * dt
    ///  >0 = blend mode:     velocity  = lerp(velocity, fieldVel,
    ///                                         tightness * dt)
    /// At tightness == 1 and large dt the particle velocity fully snaps to the
    /// field; at tightness == 0 the field accumulates like any other force.
    float  tightness;

    /// Time scroll offset for SampleAnimatedVectorField. Advances each frame
    /// as timeOffset += scrollSpeed * dt. The fractional part is taken inside
    /// the sampling function so this can grow without bound.
    float  timeOffset;

    /// Explicit padding to reach 48 bytes (three 16-byte float4 rows).
    float3 _pad;
};

// ─────────────────────────────────────────────────────────────────────────────
// WorldToFieldUVW — world-space position to [0,1]^3 texture coordinates
// ─────────────────────────────────────────────────────────────────────────────

/// Transforms a world-space position to [0,1] UVW coordinates within the
/// field's axis-aligned bounding box.
///
/// The mapping is a standard AABB remap: uvw = (pos - min) / (max - min).
/// If the position is inside the box the result is in [0,1]^3. Outside the
/// box the result may exceed those limits — callers should test with
/// IsInsideField before sampling to avoid undefined edge clamping behaviour.
///
/// A minimum extent of kMinBoundsExtent = 0.001 is clamped per-axis to guard
/// against degenerate (zero-thickness) bounding volumes producing a divide
/// by zero.
///
/// @ref Standard AABB interval remapping — (pos - min) / (max - min)
///
/// @param worldPos   Particle world-space position (cm units, Z-up).
/// @param boundsMin  World-space AABB minimum corner.
/// @param boundsMax  World-space AABB maximum corner.
/// @return           UVW coordinates. [0,1]^3 when inside the AABB; may be
///                   outside that range for particles beyond the field volume.
float3 WorldToFieldUVW(float3 worldPos, float3 boundsMin, float3 boundsMax)
{
    float3 range = boundsMax - boundsMin;

    // Guard against a zero-size (degenerate) AABB on any axis.
    // kMinBoundsExtent = 0.001 world units (~1 mm for a cm-unit scene).
    range = max(range, float3(0.001, 0.001, 0.001));

    return (worldPos - boundsMin) / range;
}

// ─────────────────────────────────────────────────────────────────────────────
// IsInsideField — [0,1]^3 containment test
// ─────────────────────────────────────────────────────────────────────────────

/// Returns true if the supplied UVW coordinate is within the [0, 1] valid
/// sampling range, i.e. the particle is inside the field's AABB.
///
/// This must be called with the output of WorldToFieldUVW before any texture
/// fetch so that particles outside the volume receive no velocity contribution.
///
/// @param uvw  UVW coordinate in [0,1]^3 for an inside particle.
/// @return     True when all components are in [0, 1].
bool IsInsideField(float3 uvw)
{
    return all(uvw >= 0.0) && all(uvw <= 1.0);
}

// ─────────────────────────────────────────────────────────────────────────────
// SampleVectorField — static 3D texture field velocity contribution
// ─────────────────────────────────────────────────────────────────────────────

/// Samples a static 3D vector field texture and returns the updated particle
/// velocity after applying the field's contribution.
///
/// The 3D texture stores directional velocity as RGB = XYZ. A trilinear
/// sampler (SamplerState, FILTER_MIN_MAG_MIP_LINEAR) must be provided for
/// smooth sub-voxel interpolation — nearest-neighbour sampling produces
/// visible banding artifacts in particle motion.
///
/// The tightness parameter selects one of two integration modes:
///   tightness == 0  Additive:  velocity += fieldVelocity * dt
///   tightness  > 0  Blend:     velocity  = lerp(velocity, fieldVelocity,
///                                               saturate(tightness * dt))
/// The saturate ensures large dt values cannot invert or overshoot the blend.
///
/// Particles whose WorldToFieldUVW result lies outside [0,1]^3 are returned
/// unchanged — no force is applied beyond the field boundary.
///
/// @ref Stam 1999 "Stable Fluids" — 3D velocity field advection
/// @ref Bridson 2015 "Fluid Simulation for Computer Graphics" 2nd ed., ch. 3
///      — texture-based velocity field sampling for particle simulation
/// @constants kMinBoundsExtent = 0.001  guards WorldToFieldUVW divide-by-zero
///            SampleLevel mip 0         trilinear interpolation at base level
///
/// @param field      Texture3D storing velocity vectors (RGB = XYZ direction).
///                   Values are expected in a pre-scaled range suitable for the
///                   scene's unit system (cm/s for cm-unit scenes).
/// @param samp       SamplerState for trilinear interpolation. Must use
///                   CLAMP or BORDER address mode so out-of-bounds fetches do
///                   not wrap into unrelated field regions.
/// @param worldPos   Particle world-space position (cm, Z-up).
/// @param currentVel Current particle velocity (cm/s). Used as the base for
///                   both additive accumulation and tightness blending.
/// @param params     VectorFieldParams describing the AABB, intensity, and
///                   tightness for this field.
/// @param dt         Frame delta time in seconds. Used to scale additive force
///                   and to advance the blend fraction in tightness mode.
/// @return           Updated particle velocity after the field is applied.
///                   Equal to currentVel when the particle is outside the AABB.
float3 SampleVectorField(
    Texture3D<float4>  field,
    SamplerState       samp,
    float3             worldPos,
    float3             currentVel,
    VectorFieldParams  params,
    float              dt)
{
    float3 uvw = WorldToFieldUVW(worldPos, params.boundsMin, params.boundsMax);

    // Particles outside the field volume are completely unaffected.
    if (!IsInsideField(uvw))
    {
        return currentVel;
    }

    // Sample field velocity from the 3D texture at base mip level.
    // SampleLevel with mip 0 avoids mip selection artifacts when the
    // texture is viewed at oblique angles during particle simulation.
    float3 fieldVel = field.SampleLevel(samp, uvw, 0).xyz;
    fieldVel *= params.intensity;

    if (params.tightness > 0.0)
    {
        // Blend mode: move particle velocity toward the field velocity.
        // saturate prevents tightness * dt > 1 from overshooting.
        // @ref Bridson 2015 "Fluid Simulation for Computer Graphics" ch. 3
        float blendFactor = saturate(params.tightness * dt);
        return lerp(currentVel, fieldVel, blendFactor);
    }
    else
    {
        // Additive mode: treat the field as an acceleration source.
        // Euler integration: velocity += fieldVelocity * dt.
        return currentVel + fieldVel * dt;
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// SampleAnimatedVectorField — time-scrolling 3D texture field
// ─────────────────────────────────────────────────────────────────────────────

/// Samples a 3D vector field with time-based UV scrolling for continuously
/// animated flow, without requiring the field texture to be re-baked each
/// frame. The Z (W) component of the UVW address is offset by params.timeOffset
/// and wrapped via frac() so the tiled texture appears to advect over time.
///
/// This technique is a standard real-time approximation for time-varying
/// velocity fields: the CPU updates params.timeOffset += scrollSpeed * dt
/// each tick; the shader wraps it to [0,1) so the 3D texture tiles seamlessly
/// along its depth axis.
///
/// The tightness and intensity parameters behave identically to
/// SampleVectorField. Particles outside the AABB are returned unchanged.
///
/// @ref Stam 1999 "Stable Fluids" — time-varying velocity field representation
///      and advection through scrolling texture coordinates
/// @ref Latta 2004 "Building a Million Particle System" GDC — animated flow
///      fields via UV offset for GPU particle systems
/// @constants frac(uvw + float3(0,0,timeOffset)) — Z-axis time scroll
///            kMinBoundsExtent = 0.001           — guards AABB divide-by-zero
///
/// @param field      Texture3D storing velocity vectors (RGB = XYZ direction).
///                   Must tile seamlessly along all three axes for animation
///                   to loop without seams.
/// @param samp       SamplerState with WRAP address mode on all axes so that
///                   the frac-wrapped UVW samples the tile correctly.
/// @param worldPos   Particle world-space position (cm, Z-up).
/// @param currentVel Current particle velocity (cm/s).
/// @param params     VectorFieldParams with timeOffset providing the current
///                   scroll position along the field's depth axis.
/// @param dt         Frame delta time in seconds.
/// @return           Updated particle velocity after the animated field is
///                   applied. Equal to currentVel outside the AABB.
float3 SampleAnimatedVectorField(
    Texture3D<float4>  field,
    SamplerState       samp,
    float3             worldPos,
    float3             currentVel,
    VectorFieldParams  params,
    float              dt)
{
    float3 uvw = WorldToFieldUVW(worldPos, params.boundsMin, params.boundsMax);

    // Particles outside the field volume are completely unaffected.
    if (!IsInsideField(uvw))
    {
        return currentVel;
    }

    // Scroll along the Z axis by the accumulated time offset and wrap to
    // [0,1) using frac so the 3D texture tiles seamlessly. X and Y are not
    // scrolled here; only Z advances to simulate a flow along one primary axis.
    // To scroll all three axes, replace with frac(uvw + float3(sx, sy, sz)).
    // @ref Stam 1999 "Stable Fluids" — time-varying field via UVW offset
    uvw = frac(uvw + float3(0.0, 0.0, params.timeOffset));

    // Sample field velocity at the time-offset coordinate.
    float3 fieldVel = field.SampleLevel(samp, uvw, 0).xyz;
    fieldVel *= params.intensity;

    if (params.tightness > 0.0)
    {
        // Blend mode: saturate prevents overshoot at large dt values.
        float blendFactor = saturate(params.tightness * dt);
        return lerp(currentVel, fieldVel, blendFactor);
    }
    else
    {
        // Additive mode: Euler integration step.
        return currentVel + fieldVel * dt;
    }
}

#endif // VFX_VECTOR_FIELD_HLSLI
