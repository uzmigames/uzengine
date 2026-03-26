/// Atmosphere LUT coordinate mapping functions — Bruneton 2017.
///
/// Provides mapping functions to convert between physical atmosphere parameters
/// (r, mu, mu_s, nu) and texture coordinates for precomputed scattering LUTs.
///
/// The 4D scattering parameter space is packed into a 3D texture:
///   width  = NU_SIZE(8) x MU_S_SIZE(32) = 256
///   height = MU_SIZE(128)
///   depth  = R_SIZE(32)
///
/// All distances are in KILOMETERS (matching transmittance_lut_cs.hlsl and
/// sky_atmosphere_ps.hlsl).
///
/// @ref Bruneton & Neyret 2017 "Precomputed Atmospheric Scattering"
///      Section 4 — Parameterization of the scattering texture.
/// @ref Reference implementation: github.com/ebruneton/precomputed_atmospheric_scattering
///      atmosphere/functions.glsl (BSD-3 license)
/// @constants All LUT dimensions from Bruneton 2017 reference defaults.
/// @deviations NONE

#ifndef ATMOSPHERE_LUT_HLSLI
#define ATMOSPHERE_LUT_HLSLI

#include "units.hlsli"

// ─── Scattering LUT Dimensions ──────────────────────────────────────────────
// @ref Bruneton 2017, reference implementation default constants.
// R_SIZE=32, MU_SIZE=128, MU_S_SIZE=32, NU_SIZE=8

/// Number of altitude layers in the scattering 3D texture (depth).
static const uint SCATTERING_R_SIZE = 32;

/// Number of view zenith angle samples (height of 3D texture).
static const uint SCATTERING_MU_SIZE = 128;

/// Number of sun zenith angle samples (packed into width).
static const uint SCATTERING_MU_S_SIZE = 32;

/// Number of sun-view angle (nu) samples (packed into width).
static const uint SCATTERING_NU_SIZE = 8;

/// Width = NU_SIZE * MU_S_SIZE. The nu and mu_s dimensions are interleaved
/// along the X axis of the 3D texture.
static const uint SCATTERING_TEX_WIDTH = SCATTERING_NU_SIZE * SCATTERING_MU_S_SIZE; // 256

/// Height = MU_SIZE.
static const uint SCATTERING_TEX_HEIGHT = SCATTERING_MU_SIZE; // 128

/// Depth = R_SIZE.
static const uint SCATTERING_TEX_DEPTH = SCATTERING_R_SIZE; // 32

// ─── Irradiance LUT Dimensions ──────────────────────────────────────────────
// @ref Bruneton 2017, reference implementation default constants.

/// Width of the 2D ground irradiance texture.
static const uint IRRADIANCE_TEX_WIDTH = 64;

/// Height of the 2D ground irradiance texture.
static const uint IRRADIANCE_TEX_HEIGHT = 16;

// ─── Transmittance LUT Dimensions ───────────────────────────────────────────
// Must match the allocations in transmittance_lut_cs.hlsl and scene_renderer.

/// Width of the 2D transmittance LUT.
static const uint TRANSMITTANCE_TEX_WIDTH = 256;

/// Height of the 2D transmittance LUT.
static const uint TRANSMITTANCE_TEX_HEIGHT = 64;

// ─── Helpers ────────────────────────────────────────────────────────────────

/// Clamps a cosine value to [-1, 1].
/// @ref Bruneton 2017 functions.glsl — ClampCosine
float ClampCosine(float mu) {
    return clamp(mu, -1.0f, 1.0f);
}

/// Clamps a distance to [0, +inf).
/// @ref Bruneton 2017 functions.glsl — ClampDistance
float ClampDistance(float d) {
    return max(d, 0.0f);
}

/// Clamps a radius to [bottomRadius, topRadius].
/// @ref Bruneton 2017 functions.glsl — ClampRadius
float ClampRadius(float r, float bottomRadius, float topRadius) {
    return clamp(r, bottomRadius, topRadius);
}

/// Returns sqrt(max(a, 0)) to avoid NaN from negative arguments caused by
/// floating-point rounding in discriminant computations.
/// @ref Bruneton 2017 functions.glsl — SafeSqrt
float SafeSqrt(float a) {
    return sqrt(max(a, 0.0f));
}

// ─── Geometry — Ray-Sphere Distances ────────────────────────────────────────

/// Distance from a point at radius r, looking in direction with cosine mu
/// relative to the zenith, to the top of the atmosphere (outer sphere).
///
/// Derived from the quadratic ray-sphere intersection formula:
///   d = -r*mu + sqrt(r^2*mu^2 - r^2 + top_radius^2)
///
/// @ref Bruneton 2017 functions.glsl — DistanceToTopAtmosphereBoundary
/// @constants None — purely geometric.
/// @deviations NONE
float DistanceToTopAtmosphereBoundary(float r, float mu, float topRadius) {
    float discriminant = r * r * (mu * mu - 1.0f) + topRadius * topRadius;
    return ClampDistance(-r * mu + SafeSqrt(discriminant));
}

/// Distance from a point at radius r, looking in direction with cosine mu
/// relative to the zenith, to the planet surface (inner sphere).
///
/// Uses the NEGATIVE root of the quadratic (ray enters sphere from outside).
///
/// @ref Bruneton 2017 functions.glsl — DistanceToBottomAtmosphereBoundary
/// @constants None — purely geometric.
/// @deviations NONE
float DistanceToBottomAtmosphereBoundary(float r, float mu, float bottomRadius) {
    float discriminant = r * r * (mu * mu - 1.0f) + bottomRadius * bottomRadius;
    return ClampDistance(-r * mu - SafeSqrt(discriminant));
}

/// Tests whether a ray from radius r in direction mu intersects the ground.
///
/// The ray hits the ground if mu < 0 (looking downward) AND the discriminant
/// of the bottom-sphere intersection is non-negative.
///
/// @ref Bruneton 2017 functions.glsl — RayIntersectsGround
/// @constants None — purely geometric.
/// @deviations NONE
bool RayIntersectsGround(float r, float mu, float bottomRadius) {
    return mu < 0.0f &&
           r * r * (mu * mu - 1.0f) + bottomRadius * bottomRadius >= 0.0f;
}

/// Distance from radius r in direction mu to whichever atmosphere boundary
/// the ray hits first (ground or top).
///
/// @ref Bruneton 2017 functions.glsl — DistanceToNearestAtmosphereBoundary
/// @constants None — purely geometric.
/// @deviations NONE
float DistanceToNearestAtmosphereBoundary(float r, float mu,
    float bottomRadius, float topRadius, bool rayIntersectsGround_) {
    if (rayIntersectsGround_) {
        return DistanceToBottomAtmosphereBoundary(r, mu, bottomRadius);
    } else {
        return DistanceToTopAtmosphereBoundary(r, mu, topRadius);
    }
}

// ─── Texture Coordinate ↔ Unit Range ────────────────────────────────────────

/// Maps a value in [0, 1] to a texture coordinate with half-texel offset.
///
/// This ensures that the unit range [0, 1] maps exactly to the centers of the
/// first and last texels, avoiding border artifacts with bilinear filtering.
///
/// @ref Bruneton 2017 functions.glsl — GetTextureCoordFromUnitRange
/// @constants 0.5/texSize offset, (1 - 1/texSize) scale — standard half-texel mapping.
/// @deviations NONE
float GetTextureCoordFromUnitRange(float x, int texSize) {
    return 0.5f / float(texSize) + x * (1.0f - 1.0f / float(texSize));
}

/// Maps a texture coordinate back to [0, 1] unit range. Inverse of the above.
///
/// @ref Bruneton 2017 functions.glsl — GetUnitRangeFromTextureCoord
/// @constants Inverse of half-texel mapping.
/// @deviations NONE
float GetUnitRangeFromTextureCoord(float u, int texSize) {
    return (u - 0.5f / float(texSize)) / (1.0f - 1.0f / float(texSize));
}

// ─── Transmittance LUT Mapping ──────────────────────────────────────────────

/// Maps (r, mu) to UV coordinates for the 2D transmittance LUT.
///
/// Uses Bruneton 2017 non-linear parameterization that concentrates precision
/// near the horizon where transmittance gradients are steepest:
///   U = f(distance to atmosphere top along ray)
///   V = f(distance from planet center to tangent point)
///
/// @ref Bruneton 2017 functions.glsl — GetTransmittanceTextureUvFromRMu
/// @constants None — purely geometric remapping.
/// @deviations NONE
float2 GetTransmittanceTextureUvFromRMu(float r, float mu,
    float bottomRadius, float topRadius) {
    // H = distance from the ground tangent point to the atmosphere top.
    float H = SafeSqrt(topRadius * topRadius - bottomRadius * bottomRadius);
    // rho = distance from the camera to the ground tangent point.
    float rho = SafeSqrt(r * r - bottomRadius * bottomRadius);
    // d = distance along the ray from (r, mu) to the atmosphere top.
    float d = DistanceToTopAtmosphereBoundary(r, mu, topRadius);
    float d_min = topRadius - r;
    float d_max = rho + H;
    float x_mu = (d - d_min) / (d_max - d_min);
    float x_r = rho / H;
    return float2(
        GetTextureCoordFromUnitRange(x_mu, (int)TRANSMITTANCE_TEX_WIDTH),
        GetTextureCoordFromUnitRange(x_r, (int)TRANSMITTANCE_TEX_HEIGHT));
}

/// Maps UV coordinates back to (r, mu) for the transmittance LUT. Inverse of above.
///
/// @ref Bruneton 2017 functions.glsl — GetRMuFromTransmittanceTextureUv
/// @constants None.
/// @deviations NONE
void GetRMuFromTransmittanceTextureUv(float2 uv, float bottomRadius, float topRadius,
    out float r, out float mu) {
    float x_mu = GetUnitRangeFromTextureCoord(uv.x, (int)TRANSMITTANCE_TEX_WIDTH);
    float x_r = GetUnitRangeFromTextureCoord(uv.y, (int)TRANSMITTANCE_TEX_HEIGHT);
    // H = distance from ground tangent point to atmosphere top.
    float H = SafeSqrt(topRadius * topRadius - bottomRadius * bottomRadius);
    // rho = distance from camera to ground tangent point, recovered from x_r.
    float rho = H * x_r;
    r = SafeSqrt(rho * rho + bottomRadius * bottomRadius);
    // d = distance along ray to atmosphere top, recovered from x_mu.
    float d_min = topRadius - r;
    float d_max = rho + H;
    float d = d_min + x_mu * (d_max - d_min);
    // Reconstruct mu from the quadratic solution.
    mu = (d == 0.0f) ? 1.0f : ClampCosine((H * H - rho * rho - d * d) / (2.0f * r * d));
}

// ─── Transmittance LUT Sampling ─────────────────────────────────────────────

/// Samples the precomputed transmittance LUT from a point at radius r looking
/// in a direction with zenith cosine mu, to the top of the atmosphere.
///
/// @ref Bruneton 2017 functions.glsl — GetTransmittanceToTopAtmosphereBoundary
/// @constants None.
/// @deviations NONE
float3 GetTransmittanceToTopAtmosphereBoundary(
    Texture2D<float4> transmittanceLut, SamplerState samp,
    float r, float mu, float bottomRadius, float topRadius) {
    float2 uv = GetTransmittanceTextureUvFromRMu(r, mu, bottomRadius, topRadius);
    return transmittanceLut.SampleLevel(samp, uv, 0).rgb;
}

/// Computes the transmittance between two points along a ray.
///
/// The ray starts at radius r with zenith cosine mu, and travels distance d.
/// Uses the identity: T(A→B) = T(A→top) / T(B→top) for sky-only rays,
/// or T(A→B) = T(B→bottom) / T(A→bottom) for ground-intersecting rays
/// (reversing the direction gives a path from B toward the top).
///
/// @ref Bruneton 2017 functions.glsl — GetTransmittance
/// @constants None.
/// @deviations NONE
float3 GetTransmittance(
    Texture2D<float4> transmittanceLut, SamplerState samp,
    float r, float mu, float d, bool rayIntersectsGround_,
    float bottomRadius, float topRadius) {
    // Compute the radius and zenith cosine at the end point of the ray segment.
    float r_d = ClampRadius(SafeSqrt(d * d + 2.0f * r * mu * d + r * r),
                            bottomRadius, topRadius);
    float mu_d = ClampCosine((r * mu + d) / r_d);

    if (rayIntersectsGround_) {
        // For ground-hitting rays, reverse the direction: T(A→B) = T(B→top) / T(A→top)
        // evaluated along the reversed ray (negate mu).
        return min(
            GetTransmittanceToTopAtmosphereBoundary(
                transmittanceLut, samp, r_d, -mu_d, bottomRadius, topRadius) /
            GetTransmittanceToTopAtmosphereBoundary(
                transmittanceLut, samp, r, -mu, bottomRadius, topRadius),
            float3(1.0f, 1.0f, 1.0f));
    } else {
        // For sky-only rays: T(A→B) = T(A→top) / T(B→top).
        return min(
            GetTransmittanceToTopAtmosphereBoundary(
                transmittanceLut, samp, r, mu, bottomRadius, topRadius) /
            GetTransmittanceToTopAtmosphereBoundary(
                transmittanceLut, samp, r_d, mu_d, bottomRadius, topRadius),
            float3(1.0f, 1.0f, 1.0f));
    }
}

/// Computes transmittance from a point toward the sun, including the smooth
/// transition at the geometric horizon (planet shadow edge).
///
/// The smoothstep term approximates the fraction of the sun disk visible above
/// the horizon, preventing a hard shadow cutoff at the terminator.
///
/// @ref Bruneton 2017 functions.glsl — GetTransmittanceToSun
/// @constants None — sunAngularRadius is a runtime parameter.
/// @deviations NONE
float3 GetTransmittanceToSun(
    Texture2D<float4> transmittanceLut, SamplerState samp,
    float r, float mu_s, float bottomRadius, float topRadius,
    float sunAngularRadius) {
    float sin_theta_h = bottomRadius / r;
    float cos_theta_h = -SafeSqrt(1.0f - sin_theta_h * sin_theta_h);
    return GetTransmittanceToTopAtmosphereBoundary(
               transmittanceLut, samp, r, mu_s, bottomRadius, topRadius) *
           smoothstep(-sin_theta_h * sunAngularRadius,
                       sin_theta_h * sunAngularRadius,
                       mu_s - cos_theta_h);
}

// ─── Scattering 4D ↔ 3D Mapping ────────────────────────────────────────────
//
// The 4D parameter space (r, mu, mu_s, nu) is packed into a 3D texture:
//   - X axis: nu (8 discrete samples) interleaved with mu_s (32 samples) → 256 texels
//   - Y axis: mu (128 samples)
//   - Z axis: r (32 samples)
//
// mu is split into two halves: the lower half (u_mu < 0.5) stores rays that
// intersect the ground, the upper half stores sky-only rays. This avoids
// interpolation artifacts at the horizon boundary.
//
// mu_s uses a non-linear mapping (Bruneton's "tent" function) that concentrates
// precision near the horizon where scattering changes fastest with sun angle.

/// Forward mapping: physical atmosphere parameters → 3D texture coordinates.
///
/// Returns float4(u_nu, u_mu_s, u_mu, u_r) where:
///   u_nu   ∈ [0,1] maps to the nu dimension (interleaved in X)
///   u_mu_s ∈ [0,1] maps to the mu_s dimension (interleaved in X)
///   u_mu   ∈ [0,1] maps to the Y axis
///   u_r    ∈ [0,1] maps to the Z axis
///
/// @ref Bruneton 2017 functions.glsl — GetScatteringTextureUvwzFromRMuMuSNu
/// @constants SCATTERING_*_SIZE from Bruneton 2017 reference defaults.
/// @deviations NONE
float4 GetScatteringTextureUvwzFromRMuMuSNu(
    float r, float mu, float mu_s, float nu,
    bool rayIntersectsGround_,
    float bottomRadius, float topRadius, float mu_s_min) {

    // ── r → u_r ──────────────────────────────────────────────────────────
    // H = maximum possible rho (from ground tangent to atmosphere top).
    float H = SafeSqrt(topRadius * topRadius - bottomRadius * bottomRadius);
    // rho = distance from the point to the ground tangent point.
    float rho = SafeSqrt(r * r - bottomRadius * bottomRadius);
    float u_r = GetTextureCoordFromUnitRange(rho / H, (int)SCATTERING_R_SIZE);

    // ── mu → u_mu (split into ground/sky halves) ─────────────────────────
    // r_mu = r * mu (projection of position onto ray direction).
    float r_mu = r * mu;
    // Discriminant for the bottom-sphere intersection.
    float discriminant = r_mu * r_mu - r * r + bottomRadius * bottomRadius;
    float u_mu;

    if (rayIntersectsGround_) {
        // Lower half [0, 0.5): rays that hit the ground.
        // d = distance to ground along the ray.
        float d = -r_mu - SafeSqrt(discriminant);
        float d_min = r - bottomRadius;
        float d_max = rho;
        float x = (d_max == d_min) ? 0.0f : (d - d_min) / (d_max - d_min);
        u_mu = 0.5f - 0.5f * GetTextureCoordFromUnitRange(x,
            (int)SCATTERING_MU_SIZE / 2);
    } else {
        // Upper half [0.5, 1.0): rays that go to the sky.
        // d = distance to atmosphere top along the ray.
        float d = -r_mu + SafeSqrt(discriminant + H * H);
        float d_min = topRadius - r;
        float d_max = rho + H;
        float x = (d - d_min) / (d_max - d_min);
        u_mu = 0.5f + 0.5f * GetTextureCoordFromUnitRange(x,
            (int)SCATTERING_MU_SIZE / 2);
    }

    // ── mu_s → u_mu_s (non-linear tent mapping) ─────────────────────────
    // This mapping concentrates precision near the horizon (mu_s ≈ 0)
    // where the scattering changes fastest with sun angle.
    // It maps mu_s through the distance-to-top-atmosphere function
    // evaluated at the ground level (r = bottomRadius), producing a
    // non-linear warping controlled by mu_s_min.
    float d = DistanceToTopAtmosphereBoundary(bottomRadius, mu_s, topRadius);
    float d_min = topRadius - bottomRadius;
    float d_max = H;
    float a = (d - d_min) / (d_max - d_min);
    // D and A use mu_s_min to define the extent of the mapping (how far
    // below the horizon the sun can go for precomputed twilight).
    float D = DistanceToTopAtmosphereBoundary(bottomRadius, mu_s_min, topRadius);
    float A = (D - d_min) / (d_max - d_min);
    // The tent function: maps [0, A] → [0, 1] with more precision near 0.
    float u_mu_s = GetTextureCoordFromUnitRange(
        max(1.0f - a / A, 0.0f) / (1.0f + a), (int)SCATTERING_MU_S_SIZE);

    // ── nu → u_nu (linear mapping) ──────────────────────────────────────
    float u_nu = (nu + 1.0f) / 2.0f;

    return float4(u_nu, u_mu_s, u_mu, u_r);
}

/// Inverse mapping: 3D texture coordinates → physical atmosphere parameters.
///
/// Takes float4(u_nu, u_mu_s, u_mu, u_r) and outputs (r, mu, mu_s, nu)
/// plus a boolean indicating whether the ray intersects the ground.
///
/// @ref Bruneton 2017 functions.glsl — GetRMuMuSNuFromScatteringTextureUvwz
/// @constants SCATTERING_*_SIZE from Bruneton 2017 reference defaults.
/// @deviations NONE
void GetRMuMuSNuFromScatteringTextureUvwz(
    float4 uvwz,
    float bottomRadius, float topRadius, float mu_s_min,
    out float r, out float mu, out float mu_s, out float nu,
    out bool rayIntersectsGround_) {

    // ── u_r → r ──────────────────────────────────────────────────────────
    float H = SafeSqrt(topRadius * topRadius - bottomRadius * bottomRadius);
    float rho = H * GetUnitRangeFromTextureCoord(uvwz.w, (int)SCATTERING_R_SIZE);
    r = SafeSqrt(rho * rho + bottomRadius * bottomRadius);

    // ── u_mu → mu (split at 0.5 for ground/sky) ─────────────────────────
    if (uvwz.z < 0.5f) {
        // Lower half: ray hits ground.
        float d_min = r - bottomRadius;
        float d_max = rho;
        float d = d_min + (d_max - d_min) * GetUnitRangeFromTextureCoord(
            1.0f - 2.0f * uvwz.z, (int)SCATTERING_MU_SIZE / 2);
        mu = (d == 0.0f) ? -1.0f :
            ClampCosine(-(rho * rho + d * d) / (2.0f * r * d));
        rayIntersectsGround_ = true;
    } else {
        // Upper half: ray goes to sky.
        float d_min = topRadius - r;
        float d_max = rho + H;
        float d = d_min + (d_max - d_min) * GetUnitRangeFromTextureCoord(
            2.0f * uvwz.z - 1.0f, (int)SCATTERING_MU_SIZE / 2);
        mu = (d == 0.0f) ? 1.0f :
            ClampCosine((H * H - rho * rho - d * d) / (2.0f * r * d));
        rayIntersectsGround_ = false;
    }

    // ── u_mu_s → mu_s (inverse of the non-linear tent mapping) ──────────
    float x_mu_s = GetUnitRangeFromTextureCoord(uvwz.y, (int)SCATTERING_MU_S_SIZE);
    float d_min = topRadius - bottomRadius;
    float d_max = H;
    // Recover the tent function parameter 'a' from x_mu_s.
    float D = DistanceToTopAtmosphereBoundary(bottomRadius, mu_s_min, topRadius);
    float A = (D - d_min) / (d_max - d_min);
    // Invert: x_mu_s = max(1 - a/A, 0) / (1 + a)
    // Solving for a: a = (A - x_mu_s * A) / (1 + x_mu_s * A)
    float a = (A - x_mu_s * A) / (1.0f + x_mu_s * A);
    float d = d_min + min(a, A) * (d_max - d_min);
    mu_s = (d == 0.0f) ? 1.0f :
        ClampCosine((H * H - d * d) / (2.0f * bottomRadius * d));

    // ── u_nu → nu (linear mapping) ──────────────────────────────────────
    nu = ClampCosine(uvwz.x * 2.0f - 1.0f);
}

/// Maps a 3D texel coordinate (fragCoord) to physical atmosphere parameters.
///
/// This is the function used during precomputation: given a dispatch thread ID
/// (as float3), decode the interleaved nu/mu_s packing and call through to
/// GetRMuMuSNuFromScatteringTextureUvwz.
///
/// After decoding, nu is clamped to the kinematically valid range for the
/// given (mu, mu_s): nu ∈ [mu*mu_s - sin_theta*sin_theta_s, mu*mu_s + sin_theta*sin_theta_s]
/// where sin_theta = sqrt(1 - mu^2) and sin_theta_s = sqrt(1 - mu_s^2).
///
/// @ref Bruneton 2017 functions.glsl — GetRMuMuSNuFromScatteringTextureFragCoord
/// @constants SCATTERING_TEXTURE_SIZE = (NU_SIZE-1, MU_S_SIZE, MU_SIZE, R_SIZE)
/// @deviations NONE
void GetRMuMuSNuFromScatteringTextureFragCoord(
    float3 fragCoord,
    float bottomRadius, float topRadius, float mu_s_min,
    out float r, out float mu, out float mu_s, out float nu,
    out bool rayIntersectsGround_) {

    // The 4D→3D packing interleaves nu and mu_s along the X axis:
    //   texel X = nu_index * MU_S_SIZE + mu_s_index
    // So we decompose fragCoord.x into discrete nu_index and mu_s_index.
    static const float4 SCATTERING_TEXTURE_SIZE = float4(
        float(SCATTERING_NU_SIZE - 1),
        float(SCATTERING_MU_S_SIZE),
        float(SCATTERING_MU_SIZE),
        float(SCATTERING_R_SIZE));

    float frag_coord_nu = floor(fragCoord.x / float(SCATTERING_MU_S_SIZE));
    float frag_coord_mu_s = fragCoord.x - frag_coord_nu * float(SCATTERING_MU_S_SIZE);

    // Normalize each coordinate to [0, 1] using the dimension sizes.
    // Note: nu uses (NU_SIZE - 1) because it has discrete samples at both
    // endpoints (-1 and +1), while the other dimensions are continuous.
    float4 uvwz = float4(frag_coord_nu, frag_coord_mu_s, fragCoord.y, fragCoord.z) /
        SCATTERING_TEXTURE_SIZE;

    GetRMuMuSNuFromScatteringTextureUvwz(
        uvwz, bottomRadius, topRadius, mu_s_min,
        r, mu, mu_s, nu, rayIntersectsGround_);

    // Clamp nu to the kinematically valid range for the decoded (mu, mu_s).
    // The angle between the view direction and sun direction is constrained by
    // the zenith angles of both: cos(view_zenith ± sun_zenith).
    float sin_theta = SafeSqrt(1.0f - mu * mu);
    float sin_theta_s = SafeSqrt(1.0f - mu_s * mu_s);
    nu = clamp(nu,
        mu * mu_s - sin_theta * sin_theta_s,
        mu * mu_s + sin_theta * sin_theta_s);
}

// ─── Scattering LUT Sampling ────────────────────────────────────────────────

/// Samples the scattering 3D texture with correct 4D interpolation.
///
/// Because nu is packed discretely along the X axis (not continuously
/// interpolated by the hardware sampler), we must manually interpolate between
/// two adjacent nu slices. Each "nu slice" is MU_S_SIZE texels wide, and there
/// are NU_SIZE such slices along the X axis.
///
/// The caller provides a Texture3D and sampler. This function performs two
/// SampleLevel calls at adjacent nu indices and lerps between them.
///
/// @ref Bruneton 2017 functions.glsl — scattering texture 4D lookup pattern
/// @constants Dimension sizes from Bruneton 2017 reference defaults.
/// @deviations NONE
float3 SampleScatteringTexture(
    Texture3D<float4> scatteringTex, SamplerState samp,
    float r, float mu, float mu_s, float nu,
    bool rayIntersectsGround_,
    float bottomRadius, float topRadius, float mu_s_min) {

    float4 uvwz = GetScatteringTextureUvwzFromRMuMuSNu(
        r, mu, mu_s, nu, rayIntersectsGround_,
        bottomRadius, topRadius, mu_s_min);

    // uvwz.x = u_nu in [0, 1], uvwz.y = u_mu_s texcoord, uvwz.z = u_mu, uvwz.w = u_r.
    // nu is discrete: compute the two adjacent nu indices and the lerp weight.
    float nu_coord = uvwz.x * float(SCATTERING_NU_SIZE - 1);
    float nu_floor = floor(nu_coord);
    float nu_frac = nu_coord - nu_floor;
    int nu_index0 = (int)nu_floor;
    int nu_index1 = min(nu_index0 + 1, (int)SCATTERING_NU_SIZE - 1);

    // For each nu index, the X texcoord within the 3D texture is:
    //   U = (nu_index * MU_S_SIZE + mu_s_texel) / TEX_WIDTH
    // where mu_s_texel = u_mu_s * MU_S_SIZE (already has half-texel offset
    // from GetTextureCoordFromUnitRange).
    float mu_s_texel = uvwz.y * float(SCATTERING_MU_S_SIZE);
    float u0 = (float(nu_index0) * float(SCATTERING_MU_S_SIZE) + mu_s_texel) /
               float(SCATTERING_TEX_WIDTH);
    float u1 = (float(nu_index1) * float(SCATTERING_MU_S_SIZE) + mu_s_texel) /
               float(SCATTERING_TEX_WIDTH);

    float3 uvw0 = float3(u0, uvwz.z, uvwz.w);
    float3 uvw1 = float3(u1, uvwz.z, uvwz.w);

    float3 s0 = scatteringTex.SampleLevel(samp, uvw0, 0).rgb;
    float3 s1 = scatteringTex.SampleLevel(samp, uvw1, 0).rgb;

    return lerp(s0, s1, nu_frac);
}

/// Variant that also returns the 4th component (used for Mie phase separation).
///
/// When the scattering texture stores Rayleigh in .rgb and Mie in .a (single-
/// scattering precomputation stores both to avoid a second 3D texture), this
/// function returns float4 with the interpolated Mie component in .w.
///
/// @ref Bruneton 2017 functions.glsl — combined Rayleigh+Mie lookup
/// @constants None.
/// @deviations NONE
float4 SampleScatteringTexture4(
    Texture3D<float4> scatteringTex, SamplerState samp,
    float r, float mu, float mu_s, float nu,
    bool rayIntersectsGround_,
    float bottomRadius, float topRadius, float mu_s_min) {

    float4 uvwz = GetScatteringTextureUvwzFromRMuMuSNu(
        r, mu, mu_s, nu, rayIntersectsGround_,
        bottomRadius, topRadius, mu_s_min);

    float nu_coord = uvwz.x * float(SCATTERING_NU_SIZE - 1);
    float nu_floor = floor(nu_coord);
    float nu_frac = nu_coord - nu_floor;
    int nu_index0 = (int)nu_floor;
    int nu_index1 = min(nu_index0 + 1, (int)SCATTERING_NU_SIZE - 1);

    float mu_s_texel = uvwz.y * float(SCATTERING_MU_S_SIZE);
    float u0 = (float(nu_index0) * float(SCATTERING_MU_S_SIZE) + mu_s_texel) /
               float(SCATTERING_TEX_WIDTH);
    float u1 = (float(nu_index1) * float(SCATTERING_MU_S_SIZE) + mu_s_texel) /
               float(SCATTERING_TEX_WIDTH);

    float4 s0 = scatteringTex.SampleLevel(samp, float3(u0, uvwz.z, uvwz.w), 0);
    float4 s1 = scatteringTex.SampleLevel(samp, float3(u1, uvwz.z, uvwz.w), 0);

    return lerp(s0, s1, nu_frac);
}

// ─── Irradiance LUT Mapping ─────────────────────────────────────────────────

/// Maps (r, mu_s) to UV coordinates for the 2D ground irradiance texture.
///
/// Simple linear parameterization:
///   x_r    = (r - bottomRadius) / (topRadius - bottomRadius)
///   x_mu_s = mu_s * 0.5 + 0.5   (maps [-1, 1] to [0, 1])
///
/// @ref Bruneton 2017 functions.glsl — GetIrradianceTextureUvFromRMuS
/// @constants Linear mapping — no non-linear warping for irradiance.
/// @deviations NONE
float2 GetIrradianceTextureUvFromRMuS(float r, float mu_s,
    float bottomRadius, float topRadius) {
    float x_r = (r - bottomRadius) / (topRadius - bottomRadius);
    float x_mu_s = mu_s * 0.5f + 0.5f;
    return float2(
        GetTextureCoordFromUnitRange(x_mu_s, (int)IRRADIANCE_TEX_WIDTH),
        GetTextureCoordFromUnitRange(x_r, (int)IRRADIANCE_TEX_HEIGHT));
}

/// Inverse mapping: UV coordinates → (r, mu_s) for the irradiance texture.
///
/// @ref Bruneton 2017 functions.glsl — GetRMuSFromIrradianceTextureUv
/// @constants None.
/// @deviations NONE
void GetRMuSFromIrradianceTextureUv(float2 uv, float bottomRadius, float topRadius,
    out float r, out float mu_s) {
    float x_mu_s = GetUnitRangeFromTextureCoord(uv.x, (int)IRRADIANCE_TEX_WIDTH);
    float x_r = GetUnitRangeFromTextureCoord(uv.y, (int)IRRADIANCE_TEX_HEIGHT);
    r = bottomRadius + x_r * (topRadius - bottomRadius);
    mu_s = ClampCosine(2.0f * x_mu_s - 1.0f);
}

// ─── Sky Radiance to Point (Aerial Perspective LUT Lookup) ─────────────────
//
// Computes the inscattered radiance between two points using dual 4D LUT
// lookups instead of ray marching. This is the key function for LUT-based
// aerial perspective.
//
// Algorithm:
//   inscatter(camera→point) = S(camera→top) - T(camera→point) * S(point→top)
//
// where S(p→top) is the path-integrated inscattering from point p to the
// atmosphere top, and T is the transmittance between the two points.
//
// @ref Bruneton & Neyret 2017 "Precomputed Atmospheric Scattering"
//      Section 5 — Runtime Rendering, GetSkyRadianceToPoint function.
// @ref Reference implementation: github.com/ebruneton/precomputed_atmospheric_scattering
//      atmosphere/functions.glsl, GetSkyRadianceToPoint (BSD-3 license)

/// Computes inscattered radiance and transmittance between camera and a point
/// in the atmosphere using two 4D LUT lookups.
///
/// @param cameraPos     Camera position in sky space (km), origin at planet center.
/// @param pointPos      Target point position in sky space (km).
/// @param sunDir        Normalised direction TOWARD the sun.
/// @param scatteringLut Precomputed 3D scattering texture (256x128x32 RGBA16F).
/// @param transmittanceLut Precomputed 2D transmittance texture (256x64 RGBA16F).
/// @param samp          Linear clamp sampler.
/// @param bottomRadius  Planet surface radius (km).
/// @param topRadius     Top of atmosphere radius (km).
/// @param mu_s_min      Minimum sun zenith cosine for LUT parameterization (cos(102 deg)).
/// @param phaseR        Rayleigh phase function value for the camera-to-point direction.
/// @param phaseM        Mie phase function value for the camera-to-point direction.
/// @param outInscatter  [out] Inscattered radiance between camera and point (no sun illuminance).
/// @param outTransmittance [out] Transmittance from camera to point.
///
/// @ref Bruneton 2017 functions.glsl — GetSkyRadianceToPoint
/// @constants mu_s_min = cos(102 deg) = -0.2079116908 (Bruneton 2017 default)
/// @deviations NONE
void GetSkyRadianceToPoint(
    float3 cameraPos, float3 pointPos, float3 sunDir,
    Texture3D<float4> scatteringLut,
    Texture2D<float4> transmittanceLut,
    SamplerState samp,
    float bottomRadius, float topRadius, float mu_s_min,
    float phaseR, float phaseM,
    out float3 outInscatter, out float3 outTransmittance) {

    // ── Compute atmosphere parameters for the camera position ────────────
    float3 viewDir = normalize(pointPos - cameraPos);
    float d = length(pointPos - cameraPos);

    // Camera: radius, view zenith cosine, sun zenith cosine, sun-view angle cosine
    float r_cam = length(cameraPos);
    float3 upCam = cameraPos / r_cam;
    float mu_cam = dot(upCam, viewDir);
    float mu_s_cam = dot(upCam, sunDir);
    float nu = dot(viewDir, sunDir);

    // Clamp radius to valid atmosphere range
    r_cam = ClampRadius(r_cam, bottomRadius, topRadius);

    bool rayHitsGround_cam = RayIntersectsGround(r_cam, mu_cam, bottomRadius);

    // ── Sample scattering at camera position ─────────────────────────────
    // S(camera → atmosphere boundary along view direction)
    // Returns float4(Rayleigh.rgb, Mie.r) — phase functions NOT applied.
    float4 scatCam = SampleScatteringTexture4(
        scatteringLut, samp,
        r_cam, mu_cam, mu_s_cam, nu, rayHitsGround_cam,
        bottomRadius, topRadius, mu_s_min);

    // ── Compute atmosphere parameters for the target point ───────────────
    float r_point = length(pointPos);
    r_point = ClampRadius(r_point, bottomRadius, topRadius);
    float3 upPoint = pointPos / r_point;

    // At the target point, the "view direction" for the LUT lookup is the SAME
    // direction (camera→point), because we need S(point → atmosphere boundary
    // along the SAME ray direction). The zenith cosine at the point is computed
    // relative to the point's local up vector.
    float mu_point = dot(upPoint, viewDir);
    float mu_s_point = dot(upPoint, sunDir);

    bool rayHitsGround_point = RayIntersectsGround(r_point, mu_point, bottomRadius);

    // ── Sample scattering at target point ────────────────────────────────
    // S(point → atmosphere boundary along same view direction)
    float4 scatPoint = SampleScatteringTexture4(
        scatteringLut, samp,
        r_point, mu_point, mu_s_point, nu, rayHitsGround_point,
        bottomRadius, topRadius, mu_s_min);

    // ── Transmittance from camera to point ───────────────────────────────
    // T(camera → point) using the precomputed transmittance LUT.
    outTransmittance = GetTransmittance(
        transmittanceLut, samp,
        r_cam, mu_cam, d, rayHitsGround_cam,
        bottomRadius, topRadius);

    // ── Inscattering = S(camera) - T(camera→point) * S(point) ───────────
    // @ref Bruneton 2017 functions.glsl — GetSkyRadianceToPoint:
    //   inscattering = S_camera - transmittance * S_point
    // This computes the path-integrated inscattering along just the segment
    // from camera to point (not the full path to the atmosphere boundary).

    // Rayleigh component: camera - attenuated point
    float3 rayleighCam   = scatCam.rgb;
    float3 rayleighPoint = scatPoint.rgb;
    float3 rayleighInscatter = rayleighCam - outTransmittance * rayleighPoint;

    // Mie component: extrapolate full-spectrum from red channel using Rayleigh ratio.
    // @ref Bruneton 2017 functions.glsl — GetScattering combined Rayleigh+Mie:
    //   mie = rayleigh * (scattering.a / max(rayleigh.r, 1e-4))
    // Apply this extrapolation to the DIFFERENCE (inscatter), not to each term
    // separately, to avoid numerical issues.
    float mieCamR   = scatCam.a;
    float miePointR = scatPoint.a;
    float mieInscatterR = mieCamR - dot(outTransmittance, float3(1, 0, 0)) * miePointR;

    float3 mieInscatter;
    if (rayleighInscatter.r > 1e-6f) {
        mieInscatter = rayleighInscatter * (mieInscatterR / rayleighInscatter.r);
    } else {
        // Fallback when Rayleigh red is negligible (deep twilight)
        mieInscatter = float3(mieInscatterR, mieInscatterR, mieInscatterR);
    }

    // Apply phase functions. The LUT stores scattering WITHOUT phase functions.
    // @ref Bruneton 2017 Section 5: phase functions applied at lookup time.
    outInscatter = rayleighInscatter * phaseR + mieInscatter * phaseM;
}

// ─── Sky Radiance (Camera to Atmosphere Boundary) ───────────────────────────
//
// Computes the inscattered radiance from the camera to the atmosphere boundary
// along a given ray direction. This is the infinite-distance version of
// GetSkyRadianceToPoint — used when the ray does not hit any geometry (sky
// pixels, cubemap capture).
//
// Algorithm:
//   inscatter = S(camera→boundary) with phase functions applied at lookup time.
//   transmittance = T(camera→boundary) via the transmittance LUT.
//
// For ground-intersecting rays, the scattering LUT already stores the correct
// integral terminated at the ground. The caller should handle ground shading
// separately (Lambertian reflection attenuated by transmittance).
//
// @ref Bruneton & Neyret 2017 "Precomputed Atmospheric Scattering"
//      Section 5 — Runtime Rendering, GetSkyRadiance function.
// @ref Reference implementation: github.com/ebruneton/precomputed_atmospheric_scattering
//      atmosphere/functions.glsl, GetSkyRadiance (BSD-3 license)

/// Computes inscattered radiance and transmittance from camera to the atmosphere
/// boundary (or ground) along a ray direction.
///
/// @param cameraPos        Camera position in sky space (km), origin at planet center.
/// @param rayDir           Normalised view ray direction.
/// @param sunDir           Normalised direction TOWARD the sun.
/// @param scatteringLut    Precomputed 3D scattering texture (256x128x32 RGBA16F).
/// @param transmittanceLut Precomputed 2D transmittance texture (256x64 RGBA16F).
/// @param samp             Linear clamp sampler.
/// @param bottomRadius     Planet surface radius (km).
/// @param topRadius        Top of atmosphere radius (km).
/// @param mu_s_min         Minimum sun zenith cosine for LUT parameterization (cos(102 deg)).
/// @param outRadiance      [out] Inscattered radiance along the ray (phase-weighted, no sun illuminance).
/// @param outTransmittance [out] Transmittance from camera to the atmosphere boundary (or ground).
///
/// @ref Bruneton 2017 functions.glsl — GetSkyRadiance
/// @constants mu_s_min = cos(102 deg) = -0.2079116908 (Bruneton 2017 default)
/// @deviations NONE
void GetSkyRadiance(
    float3 cameraPos, float3 rayDir, float3 sunDir,
    Texture3D<float4> scatteringLut,
    Texture2D<float4> transmittanceLut,
    SamplerState samp,
    float bottomRadius, float topRadius, float mu_s_min,
    float phaseR, float phaseM,
    out float3 outRadiance, out float3 outTransmittance) {

    // ── Compute atmosphere parameters for the camera position ────────────
    float r = length(cameraPos);
    r = ClampRadius(r, bottomRadius, topRadius);
    float3 up = cameraPos / r;
    float mu = dot(up, rayDir);
    float mu_s = dot(up, sunDir);
    float nu = dot(rayDir, sunDir);

    bool intersectsGround = RayIntersectsGround(r, mu, bottomRadius);

    // ── Sample scattering at camera position ─────────────────────────────
    // S(camera → atmosphere boundary along ray direction)
    // Returns float4(Rayleigh.rgb, Mie.r) — phase functions NOT applied.
    float4 scatSample = SampleScatteringTexture4(
        scatteringLut, samp,
        r, mu, mu_s, nu, intersectsGround,
        bottomRadius, topRadius, mu_s_min);

    // ── Extract Rayleigh and Mie components ──────────────────────────────
    float3 rayleigh = scatSample.rgb;

    // Mie extrapolation: full-spectrum Mie from red channel using Rayleigh ratio.
    // @ref Bruneton 2017 functions.glsl — GetScattering combined Rayleigh+Mie:
    //   mie = rayleigh * (scattering.a / max(rayleigh.r, 1e-4))
    float3 mie;
    if (rayleigh.r > 1e-6f) {
        mie = rayleigh * (scatSample.a / rayleigh.r);
    } else {
        // Fallback when Rayleigh red is negligible (deep twilight)
        mie = float3(scatSample.a, scatSample.a, scatSample.a);
    }

    // ── Apply phase functions ────────────────────────────────────────────
    // @ref Bruneton 2017 Section 5: phase functions applied at lookup time.
    outRadiance = rayleigh * phaseR + mie * phaseM;

    // ── Transmittance from camera to the atmosphere boundary ─────────────
    float d = DistanceToNearestAtmosphereBoundary(r, mu, bottomRadius, topRadius, intersectsGround);
    outTransmittance = GetTransmittance(
        transmittanceLut, samp,
        r, mu, d, intersectsGround,
        bottomRadius, topRadius);
}

#endif // ATMOSPHERE_LUT_HLSLI
