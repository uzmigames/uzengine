/// Indirect Scattering Compute Shader — Integrates scattering density along view rays.
///
/// @ref Bruneton & Neyret 2017 "Precomputed Atmospheric Scattering"
///      Section 4 — ComputeMultipleScatteringTexture
/// @ref Reference implementation: github.com/ebruneton/precomputed_atmospheric_scattering
///      atmosphere/functions.glsl (BSD-3 license)
///
/// For each texel in the 256x128x32 3D scattering texture, integrates the
/// scattering density (computed by scattering_density_cs.hlsl) along the view
/// ray to produce the N-th order indirect scattering contribution. This
/// contribution is then:
///   1. Written to a delta scattering texture (for the next order's density
///      computation, if any).
///   2. ADDED to the accumulated total scattering texture (sum of all orders).
///
/// The ray-march follows the same geometry as single_scattering_lut_cs.hlsl:
/// starting from radius r in direction mu, marching to the atmosphere boundary
/// (or ground), sampling the 3D density texture at each step, and multiplying
/// by the transmittance from the camera to that point.
///
/// # Resources
/// - b0: 8 DWORD root constants (atmosphere medium coefficients)
/// - b2: CBV (ozone params, texture indices)
/// - s0: SamplerState linear clamp
///
/// # Dispatch
/// [numthreads(8,8,1)], dispatch (32, 16, 32) = 256x128x32 total threads.
///
/// @constants NUM_MARCH_STEPS=50, MIE_EXTINCTION_RATIO=1.11, EXT_EPS=1e-7
/// @deviations NONE

#include "../include/atmosphere_lut.hlsli"

// ─────────────────────────────────────────────────────────────────────────────
// Root Constants (b0) — atmosphere scattering parameters
// ─────────────────────────────────────────────────────────────────────────────

/// Same layout as all atmosphere precompute shaders.
cbuffer AtmosphereParams : register(b0) {
    float3 rayleighBeta;     ///< Rayleigh scattering coefficients (km^-1).
    float  rayleighScaleH;   ///< Rayleigh scale height (km).
    float  mieBeta;          ///< Mie scattering coefficient (km^-1).
    float  mieScaleH;        ///< Mie scale height (km).
    float  bottomRadiusKm;   ///< Planet surface radius (km).
    float  topRadiusKm;      ///< Top of atmosphere radius (km).
};

// ─────────────────────────────────────────────────────────────────────────────
// CBV (b2) — per-dispatch parameters
// ─────────────────────────────────────────────────────────────────────────────

/// @ref Bruneton 2017 — ozone and texture indices for the indirect scatter pass.
cbuffer IndirectScatteringParams : register(b2) {
    float3 ozoneAbsorption;         ///< Ozone absorption extinction (km^-1).
    float  ozoneLayerWidth;         ///< Boundary altitude between two linear segments (km).
    float  ozoneLinTerm0;           ///< Linear coefficient for h < layerWidth.
    float  ozoneConstTerm0;         ///< Constant term for h < layerWidth.
    float  ozoneLinTerm1;           ///< Linear coefficient for h >= layerWidth.
    float  ozoneConstTerm1;         ///< Constant term for h >= layerWidth.
    uint   transmittanceLutSrvIdx;  ///< SRV heap index for transmittance LUT (256x64 RGBA16F).
    uint   scatteringDensitySrvIdx; ///< SRV heap index for scattering density from density pass.
    uint   scatteringUavIdx;        ///< UAV heap index for accumulated total scattering (additive).
    uint   deltaScatteringUavIdx;   ///< UAV heap index for delta N-th order scattering (overwrite).
    float  muSMin;                  ///< Minimum mu_s for scattering LUT mapping.
    float  _pad[3];
};

// ─────────────────────────────────────────────────────────────────────────────
// Sampler
// ─────────────────────────────────────────────────────────────────────────────

SamplerState linearClampSamp : register(s0);

// ─────────────────────────────────────────────────────────────────────────────
// Constants
// ─────────────────────────────────────────────────────────────────────────────

/// Number of integration steps along the view ray.
/// @ref Bruneton 2017 reference implementation uses 50 for precomputation.
/// Same as single_scattering_lut_cs.hlsl for consistency.
/// @constants 50
/// @deviations NONE
static const int NUM_MARCH_STEPS = 50;

/// Mie extinction-to-scattering ratio.
/// @ref Bruneton 2017 — standard convention.
/// @constants 1.11
/// @deviations NONE
static const float MIE_EXTINCTION_RATIO = 1.11f;

/// Epsilon for extinction denominator to avoid division by zero.
/// @ref units.hlsli:44 UZ_EXTINCTION_EPS
/// @constants 1e-7
/// @deviations NONE
static const float EXT_EPS = 1e-7f;

// ─────────────────────────────────────────────────────────────────────────────
// Medium Sampling
// ─────────────────────────────────────────────────────────────────────────────

/// Computes total extinction at a given altitude above planet surface.
/// @ref Bruneton 2017 — atmosphere medium properties.
/// @constants MIE_EXTINCTION_RATIO=1.11
/// @deviations NONE
float3 GetExtinction(float altKm) {
    float densR = exp(-altKm / rayleighScaleH);
    float densM = exp(-altKm / mieScaleH);
    float densO = (altKm < ozoneLayerWidth)
        ? saturate(ozoneLinTerm0 * altKm + ozoneConstTerm0)
        : saturate(ozoneLinTerm1 * altKm + ozoneConstTerm1);
    float3 rayleighExt = rayleighBeta * densR;
    float mieExt = mieBeta * densM * MIE_EXTINCTION_RATIO;
    float3 ozoneExt = ozoneAbsorption * densO;
    return rayleighExt + mieExt + ozoneExt;
}

// ─────────────────────────────────────────────────────────────────────────────
// Scattering Density Texture Sampling
// ─────────────────────────────────────────────────────────────────────────────

/// Samples the 3D scattering density texture at the given atmosphere parameters.
/// Uses the same 4D-to-3D coordinate mapping as the scattering textures.
///
/// @ref Bruneton 2017 — same coordinate packing as scattering texture.
/// @constants Uses SCATTERING_*_SIZE from atmosphere_lut.hlsli.
/// @deviations NONE
float3 SampleScatteringDensity(
    Texture3D<float4> densityTex,
    float r, float mu, float mu_s, float nu,
    bool rayIntersectsGround_)
{
    // Use the standard scattering texture coordinate mapping.
    // The density texture uses identical parameterization.
    return SampleScatteringTexture(densityTex, linearClampSamp,
        r, mu, mu_s, nu, rayIntersectsGround_,
        bottomRadiusKm, topRadiusKm, muSMin);
}

// ─────────────────────────────────────────────────────────────────────────────
// Main Compute Kernel
// ─────────────────────────────────────────────────────────────────────────────

/// @ref Bruneton 2017 "Precomputed Atmospheric Scattering" — ComputeMultipleScatteringTexture
///
/// For each texel in the 3D scattering texture:
/// 1. Decode (r, mu, mu_s, nu, rayIntersectsGround) from texel coordinates.
/// 2. Ray-march from the camera position (radius r, direction mu) to the
///    atmosphere boundary (or ground).
/// 3. At each step, sample the scattering density texture and multiply by the
///    transmittance from camera to that point.
/// 4. The result is the N-th order indirect scattering for this (r, mu, mu_s, nu).
/// 5. Write to deltaScattering (for next order) and ADD to total scattering.
///
/// @constants NUM_MARCH_STEPS=50, MIE_EXTINCTION_RATIO=1.11, EXT_EPS=1e-7
/// @deviations NONE
[numthreads(8, 8, 1)]
void main(uint3 tid : SV_DispatchThreadID)
{
    // ── Bounds check ─────────────────────────────────────────────────────
    if (tid.x >= SCATTERING_TEX_WIDTH ||
        tid.y >= SCATTERING_TEX_HEIGHT ||
        tid.z >= SCATTERING_TEX_DEPTH)
        return;

    // ── Fetch resources via bindless ─────────────────────────────────────
    Texture2D<float4>   tLut        = ResourceDescriptorHeap[transmittanceLutSrvIdx];
    Texture3D<float4>   densityTex  = ResourceDescriptorHeap[scatteringDensitySrvIdx];
    RWTexture3D<float4> scatUav     = ResourceDescriptorHeap[scatteringUavIdx];
    RWTexture3D<float4> deltaUav    = ResourceDescriptorHeap[deltaScatteringUavIdx];

    // ── Decode texel to atmosphere parameters ────────────────────────────
    float3 fragCoord = float3(tid) + 0.5f;

    float r, mu, mu_s, nu;
    bool rayIntersectsGround_;
    GetRMuMuSNuFromScatteringTextureFragCoord(
        fragCoord, bottomRadiusKm, topRadiusKm, muSMin,
        r, mu, mu_s, nu, rayIntersectsGround_);

    // ── Compute ray length to atmosphere boundary ────────────────────────
    float rayLength = DistanceToNearestAtmosphereBoundary(
        r, mu, bottomRadiusKm, topRadiusKm, rayIntersectsGround_);

    // Early-out for zero-length rays.
    if (rayLength <= 0.0f) {
        deltaUav[tid] = float4(0.0f, 0.0f, 0.0f, 0.0f);
        // Do NOT write to scatUav here — we must not alter the accumulated value
        // when there is nothing to add.
        return;
    }

    float dt = rayLength / float(NUM_MARCH_STEPS);

    // ── Ray march and integrate scattering density ───────────────────────
    // At each step, the contribution is:
    //   T(camera, sample) * density(sample) * dt
    //
    // Uses Frostbite analytical per-step integration for the transmittance
    // within each step, same as single_scattering_lut_cs.hlsl.
    //
    // @ref Bruneton 2017 — ComputeMultipleScattering uses exact LUT transmittance
    // @ref Frostbite 2015 — analytical in-step integration
    float3 result = float3(0.0f, 0.0f, 0.0f);
    float3 throughput = float3(1.0f, 1.0f, 1.0f);

    for (int i = 0; i < NUM_MARCH_STEPS; ++i)
    {
        // Midpoint of current step along the ray.
        float t_i = (float(i) + 0.5f) * dt;

        // ── Position along ray ───────────────────────────────────────
        // |P + t_i * w|^2 = r^2 + t_i^2 + 2*r*t_i*mu
        float r_i = ClampRadius(
            SafeSqrt(r * r + t_i * t_i + 2.0f * r * t_i * mu),
            bottomRadiusKm, topRadiusKm);

        // Altitude above planet surface.
        float alt_i = max(0.0f, r_i - bottomRadiusKm);

        // ── Zenith cosine at sample point ────────────────────────────
        // mu_i = (r * mu + t_i) / r_i
        float mu_i = ClampCosine((r * mu + t_i) / r_i);

        // ── Sun angle at sample point ────────────────────────────────
        // mu_s_i = (r * mu_s + t_i * nu) / r_i
        float mu_s_i = ClampCosine((r * mu_s + t_i * nu) / r_i);

        // ── nu at sample point ───────────────────────────────────────
        // nu remains the same along the ray (view and sun directions
        // are constant, only the local zenith changes).
        // However, we need to re-derive nu_i for the sample point
        // to be consistent with the LUT parameterization.
        // Actually nu = dot(viewDir, sunDir) is invariant along the ray.

        // ── Transmittance from camera to sample ──────────────────────
        // Uses the LUT-based exact transmittance.
        float3 T_camera_to_sample = GetTransmittance(
            tLut, linearClampSamp,
            r, mu, t_i, rayIntersectsGround_,
            bottomRadiusKm, topRadiusKm);

        // ── Sample scattering density at this point ──────────────────
        // The density texture stores the amount of scattered light per
        // unit length per unit solid angle at each atmosphere point.
        bool sample_hits_ground = RayIntersectsGround(r_i, mu_i, bottomRadiusKm);
        float3 density_i = SampleScatteringDensity(densityTex,
            r_i, mu_i, mu_s_i, nu, sample_hits_ground);

        // ── Extinction at this altitude ──────────────────────────────
        float3 extinction_i = GetExtinction(alt_i);

        // ── Frostbite analytical per-step integration ────────────────
        // @ref Frostbite 2015 "Physically Based Unified Volumetric Rendering"
        //   integral of density * exp(-ext * s) ds from 0 to dt
        //   ≈ density * (1 - exp(-ext * dt)) / ext
        float3 expTerm = exp(-extinction_i * dt);
        float3 safeExt = max(extinction_i, float3(EXT_EPS, EXT_EPS, EXT_EPS));
        float3 intFactor = (1.0f - expTerm) / safeExt;

        // Accumulate: T(camera, sample) * density * integrated step factor.
        result += T_camera_to_sample * density_i * intFactor;
    }

    // ── Write outputs ────────────────────────────────────────────────────
    // Delta scattering: overwrite with this order's contribution.
    // This will be used by the next scattering density computation
    // (if more orders are computed).
    deltaUav[tid] = float4(result, 0.0f);

    // Accumulated scattering: ADD this order to the running sum.
    // The scattering texture starts with order 1 (single scatter),
    // and each subsequent order is added here.
    // @ref Bruneton 2017 — additive accumulation of scattering orders.
    float4 existing = scatUav[tid];
    existing.rgb += result;
    scatUav[tid] = existing;
}
