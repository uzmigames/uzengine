/// Scattering Density Compute Shader — N-th order scattering density precomputation.
///
/// @ref Bruneton & Neyret 2017 "Precomputed Atmospheric Scattering"
///      Section 4 — ComputeScatteringDensityTexture
/// @ref Reference implementation: github.com/ebruneton/precomputed_atmospheric_scattering
///      atmosphere/functions.glsl (BSD-3 license)
///
/// Computes the density of N-th order scattered light at each point in the
/// atmosphere. This is an intermediate step: for each texel in the 256x128x32
/// 3D scattering texture, it reads the PREVIOUS order's scattering and
/// irradiance to compute what density of light is scattered into the view
/// direction at that point.
///
/// Two sources contribute:
///   A) Rayleigh+Mie scattering of (N-1)-th order scattered light from all
///      sphere directions, weighted by the phase functions.
///   B) Ground-reflected irradiance (Lambertian reflection of incoming skylight).
///
/// # Resources
/// - b0: 8 DWORD root constants (atmosphere medium coefficients)
/// - b2: CBV (ozone params, texture indices, scattering order)
/// - s0: SamplerState linear clamp
///
/// # Dispatch
/// [numthreads(8,8,1)], dispatch (32, 16, 32) = 256x128x32 total threads.
///
/// @constants NUM_SPHERE_DIRECTIONS=16, MIE_EXTINCTION_RATIO=1.11
/// @deviations NONE

#include "../include/math_util.hlsli"
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

/// @ref Bruneton 2017 — ozone, scattering order, and texture indices.
cbuffer ScatteringDensityParams : register(b2) {
    float3 ozoneAbsorption;         ///< Ozone absorption extinction (km^-1).
    float  ozoneLayerWidth;         ///< Boundary altitude between two linear segments (km).
    float  ozoneLinTerm0;           ///< Linear coefficient for h < layerWidth.
    float  ozoneConstTerm0;         ///< Constant term for h < layerWidth.
    float  ozoneLinTerm1;           ///< Linear coefficient for h >= layerWidth.
    float  ozoneConstTerm1;         ///< Constant term for h >= layerWidth.
    uint   transmittanceLutSrvIdx;  ///< SRV heap index for transmittance LUT (256x64 RGBA16F).
    uint   deltaRayleighSrvIdx;     ///< Order 1: single scatter Rayleigh. Order > 2: delta multiple scatter.
    uint   deltaMieSrvIdx;          ///< Order 1: single scatter Mie. Order > 2: UINT32_MAX (unused).
    uint   deltaIrradianceSrvIdx;   ///< Previous order ground irradiance (64x16 RGBA16F).
    uint   scatteringDensityUavIdx; ///< Output UAV for scattering density (256x128x32 RGBA16F).
    float  mieAnisotropy;           ///< Henyey-Greenstein g parameter for Mie phase.
    float3 groundAlbedo;            ///< Planet surface Lambertian albedo.
    int    scatteringOrder;         ///< Current scattering order (2, 3, 4...).
    float  muSMin;                  ///< Minimum mu_s for scattering LUT mapping.
    float  _pad[2];
};

// ─────────────────────────────────────────────────────────────────────────────
// Sampler
// ─────────────────────────────────────────────────────────────────────────────

SamplerState linearClampSamp : register(s0);

// ─────────────────────────────────────────────────────────────────────────────
// SM5 register-bound fallback (when UZ_BINDLESS is not defined)
// ─────────────────────────────────────────────────────────────────────────────
#ifndef UZ_BINDLESS
Texture2D<float4>   g_TransmittanceLut     : register(t0);
Texture3D<float4>   g_DeltaRayleighTex     : register(t1);
Texture3D<float4>   g_DeltaMieTex          : register(t2);
Texture2D<float4>   g_DeltaIrradianceTex   : register(t3);
RWTexture3D<float4> g_DensityOut           : register(u0);
#endif

// ─────────────────────────────────────────────────────────────────────────────
// Constants
// ─────────────────────────────────────────────────────────────────────────────

/// Number of uniform sphere directions for the angular integration.
/// @ref Bruneton 2017 reference uses 16 for precomputation balance.
/// @constants 16 (precompute quality)
/// @deviations NONE
static const int NUM_SPHERE_DIRECTIONS = 16;

/// Golden ratio for Fibonacci sphere sampling.
static const float GOLDEN_RATIO = 0.6180339887498949f;

/// Mie extinction-to-scattering ratio.
/// @ref Bruneton 2017 — standard convention.
/// @constants 1.11
/// @deviations NONE
static const float MIE_EXTINCTION_RATIO = 1.11f;

// ─────────────────────────────────────────────────────────────────────────────
// Phase Functions
// ─────────────────────────────────────────────────────────────────────────────

/// Rayleigh phase function: (3/16pi)(1 + cos^2 theta).
/// @ref Bruneton 2017 — standard Rayleigh scattering phase.
/// @constants 3/(16*PI)
/// @deviations NONE
float RayleighPhase(float cosTheta) {
    return (3.0f / (16.0f * PI)) * (1.0f + cosTheta * cosTheta);
}

/// Henyey-Greenstein phase function for Mie scattering.
/// @ref Bruneton 2017 — standard HG phase.
/// @constants 1/(4*PI) normalization
/// @deviations NONE
float MiePhase(float cosTheta, float g) {
    float g2    = g * g;
    float denom = 1.0f + g2 - 2.0f * g * cosTheta;
    return (1.0f / (4.0f * PI)) * (1.0f - g2) / (denom * sqrt(denom));
}

// ─────────────────────────────────────────────────────────────────────────────
// Medium Sampling
// ─────────────────────────────────────────────────────────────────────────────

/// Computes scattering and extinction at a given altitude above planet surface.
/// @ref Bruneton 2017 — atmosphere medium properties.
/// @constants MIE_EXTINCTION_RATIO=1.11
/// @deviations NONE
void GetScatteringAndExtinction(float altKm,
    out float3 rayleighScatter, out float mieScatter, out float3 extinction)
{
    float densR = exp(-altKm / rayleighScaleH);
    float densM = exp(-altKm / mieScaleH);
    float densO = (altKm < ozoneLayerWidth)
        ? saturate(ozoneLinTerm0 * altKm + ozoneConstTerm0)
        : saturate(ozoneLinTerm1 * altKm + ozoneConstTerm1);
    rayleighScatter = rayleighBeta * densR;
    mieScatter = mieBeta * densM;
    float mieExt = mieScatter * MIE_EXTINCTION_RATIO;
    extinction = rayleighScatter + mieExt + ozoneAbsorption * densO;
}

// ─────────────────────────────────────────────────────────────────────────────
// Irradiance LUT Sampling
// ─────────────────────────────────────────────────────────────────────────────

/// Samples the ground irradiance from the 2D irradiance LUT.
/// @ref Bruneton 2017 — GetIrradiance
/// @constants Uses GetIrradianceTextureUvFromRMuS from atmosphere_lut.hlsli.
/// @deviations NONE
float3 SampleIrradiance(Texture2D<float4> irradianceTex, float r, float mu_s) {
    float2 uv = GetIrradianceTextureUvFromRMuS(r, mu_s, bottomRadiusKm, topRadiusKm);
    return irradianceTex.SampleLevel(linearClampSamp, uv, 0).rgb;
}

// ─────────────────────────────────────────────────────────────────────────────
// Main Compute Kernel
// ─────────────────────────────────────────────────────────────────────────────

/// @ref Bruneton 2017 "Precomputed Atmospheric Scattering" — ComputeScatteringDensityTexture
///
/// For each texel in the 256x128x32 scattering texture:
/// 1. Decode (r, mu, mu_s, nu, rayIntersectsGround) from texel coordinates.
/// 2. Reconstruct the geometric directions (zenith, view, sun).
/// 3. Integrate over NUM_SPHERE_DIRECTIONS uniform sphere directions:
///    - For each direction omega_i, look up the (N-1)-th order scattering
///      coming FROM that direction (at the current point).
///    - Weight by the phase function for scattering from omega_i INTO the
///      view direction.
///    - Accumulate the scattering density.
/// 4. Add ground-reflected irradiance contribution at the bottom of the
///    atmosphere (if the current point is at planet surface level).
///
/// @constants NUM_SPHERE_DIRECTIONS=16
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
#ifdef UZ_BINDLESS
    Texture2D<float4>   tLut              = ResourceDescriptorHeap[transmittanceLutSrvIdx];
    Texture3D<float4>   deltaRayleighTex  = ResourceDescriptorHeap[deltaRayleighSrvIdx];
    Texture3D<float4>   deltaMieTex       = ResourceDescriptorHeap[deltaMieSrvIdx];
    Texture2D<float4>   deltaIrradianceTex = ResourceDescriptorHeap[deltaIrradianceSrvIdx];
    RWTexture3D<float4> densityOut        = ResourceDescriptorHeap[scatteringDensityUavIdx];
#else
    Texture2D<float4>   tLut              = g_TransmittanceLut;
    Texture3D<float4>   deltaRayleighTex  = g_DeltaRayleighTex;
    Texture3D<float4>   deltaMieTex       = g_DeltaMieTex;
    Texture2D<float4>   deltaIrradianceTex = g_DeltaIrradianceTex;
    RWTexture3D<float4> densityOut        = g_DensityOut;
#endif

    // ── Decode texel to atmosphere parameters ────────────────────────────
    float3 fragCoord = float3(tid) + 0.5f;

    float r, mu, mu_s, nu;
    bool rayIntersectsGround_;
    GetRMuMuSNuFromScatteringTextureFragCoord(
        fragCoord, bottomRadiusKm, topRadiusKm, muSMin,
        r, mu, mu_s, nu, rayIntersectsGround_);

    // ── Reconstruct geometric directions ─────────────────────────────────
    // In the local coordinate system at the current point:
    //   zenithDir = normalize(position) = (0, 0, 1) in local frame
    //   viewDir has cos(zenith) = mu
    //   sunDir has cos(zenith) = mu_s and dot(viewDir, sunDir) = nu
    //
    // We need to construct viewDir and sunDir in a consistent coordinate frame.
    // @ref Bruneton 2017 functions.glsl:ComputeScatteringDensity
    //
    // zenithDir = (0, 0, 1) in local Z-up frame at the sample point.
    // viewDir = (sin_theta * cos_phi_v, sin_theta * sin_phi_v, mu)
    //         = (sqrt(1-mu^2), 0, mu) (choosing phi_v = 0 WLOG by azimuthal symmetry)
    // sunDir must satisfy: dot(sunDir, zenith) = mu_s AND dot(sunDir, viewDir) = nu
    //
    // sunDir = (sin_theta_s * cos_phi_s, sin_theta_s * sin_phi_s, mu_s)
    // where cos_phi_s and sin_phi_s are determined by the nu constraint.

    float3 zenithDir = float3(0.0f, 0.0f, 1.0f);

    float sin_theta = SafeSqrt(1.0f - mu * mu);
    float3 viewDir = float3(sin_theta, 0.0f, mu);

    // Solve for sunDir: dot(viewDir, sunDir) = nu
    // viewDir.x * sunDir.x + viewDir.z * sunDir.z = nu
    // sin_theta * sin_theta_s * cos_phi_s + mu * mu_s = nu
    // cos_phi_s = (nu - mu * mu_s) / (sin_theta * sin_theta_s)
    float sin_theta_s = SafeSqrt(1.0f - mu_s * mu_s);
    float cos_phi_s = (sin_theta == 0.0f || sin_theta_s == 0.0f)
        ? 1.0f
        : clamp((nu - mu * mu_s) / (sin_theta * sin_theta_s), -1.0f, 1.0f);
    float sin_phi_s = SafeSqrt(1.0f - cos_phi_s * cos_phi_s);

    float3 sunDir = float3(sin_theta_s * cos_phi_s, sin_theta_s * sin_phi_s, mu_s);

    // ── Integrate over uniform sphere directions ─────────────────────────
    // For each incoming direction omega_i, compute:
    //   density += scattering_from_omega_i * (rayleighScat * RayleighPhase(cos_view)
    //              + mieScat * MiePhase(cos_view, g))
    //
    // where cos_view = dot(omega_i, viewDir) is the scattering angle from
    // omega_i into the view direction.

    // Get scattering coefficients at this altitude.
    float alt = max(0.0f, r - bottomRadiusKm);
    float3 rayleighScat;
    float mieScat;
    float3 extinction;
    GetScatteringAndExtinction(alt, rayleighScat, mieScat, extinction);

    float3 densitySum = float3(0.0f, 0.0f, 0.0f);

    for (int i = 0; i < NUM_SPHERE_DIRECTIONS; ++i)
    {
        // ── Fibonacci sphere sampling for uniform direction ──────────
        // @ref Bruneton 2017 reference — uniform sphere distribution.
        float theta_i = acos(clamp(1.0f - 2.0f * (float(i) + 0.5f) / float(NUM_SPHERE_DIRECTIONS), -1.0f, 1.0f));
        float phi_i   = 2.0f * PI * frac(float(i) * GOLDEN_RATIO);
        float sin_theta_i = sin(theta_i);
        float cos_theta_i = cos(theta_i);
        float3 omega_i = float3(sin_theta_i * cos(phi_i),
                                sin_theta_i * sin(phi_i),
                                cos_theta_i);

        // ── Solid angle weight for uniform sphere sampling ───────────
        // Each direction covers 4*PI / NUM_SPHERE_DIRECTIONS steradians.
        float dOmega = 4.0f * PI / float(NUM_SPHERE_DIRECTIONS);

        // ── Scattering angle: from omega_i into view direction ───────
        // cos_view = dot(omega_i, viewDir)
        float cos_view = dot(omega_i, viewDir);

        // ── Look up incoming scattering from omega_i ─────────────────
        // At the current point (r, mu_i, mu_s_i, nu_i), what is the
        // (N-1)-th order scattering arriving from direction omega_i?
        //
        // mu_i  = dot(zenithDir, omega_i) = omega_i.z
        // mu_s_i depends on the sun angle relative to omega_i's zenith.
        // nu_i = dot(omega_i, sunDir)
        float mu_i   = omega_i.z;
        float mu_s_i = dot(omega_i, sunDir);
        float nu_i   = dot(omega_i, sunDir); // = mu_s_i for the incoming direction

        // For the incoming scattering lookup, we need the parameters at the
        // current position but looking FROM direction omega_i. The point is the
        // same (radius r), the "view direction" for the lookup is omega_i
        // (looking in the direction light is coming FROM), so:
        //   lookup_mu = dot(zenithDir, omega_i) = omega_i.z = mu_i
        //   lookup_mu_s = mu_s (sun angle relative to zenith doesn't change)
        //   lookup_nu = dot(omega_i, sunDir)
        //   lookup_rayIntersectsGround = RayIntersectsGround(r, mu_i)
        bool omega_hits_ground = RayIntersectsGround(r, mu_i, bottomRadiusKm);
        float nu_i_sun = dot(omega_i, sunDir);

        float3 incomingScattering;

        if (scatteringOrder == 2) {
            // Order 2: read single-scatter (order 1) from separate Rayleigh + Mie textures
            // and apply phase functions to get the actual inscattered luminance from omega_i.
            //
            // @ref Bruneton 2017 — for order 2, the previous order (1) stores Rayleigh and
            // Mie scattering WITHOUT phase functions, so we must apply them here.
            float3 rayleigh1 = SampleScatteringTexture(
                deltaRayleighTex, linearClampSamp,
                r, mu_i, mu_s, nu_i_sun, omega_hits_ground,
                bottomRadiusKm, topRadiusKm, muSMin);
            float3 mie1 = SampleScatteringTexture(
                deltaMieTex, linearClampSamp,
                r, mu_i, mu_s, nu_i_sun, omega_hits_ground,
                bottomRadiusKm, topRadiusKm, muSMin);

            incomingScattering = rayleigh1 * RayleighPhase(nu_i_sun) +
                                 mie1 * MiePhase(nu_i_sun, mieAnisotropy);
        } else {
            // Order > 2: read the combined delta scattering texture (already
            // has phase functions folded in from the previous density computation).
            // deltaRayleighSrvIdx points to the delta scattering from last order.
            incomingScattering = SampleScatteringTexture(
                deltaRayleighTex, linearClampSamp,
                r, mu_i, mu_s, nu_i_sun, omega_hits_ground,
                bottomRadiusKm, topRadiusKm, muSMin);
        }

        // ── Weight by scattering coefficients and phase ──────────────
        // The density of light scattered from omega_i into the view direction
        // is: incoming * (rayleighScat * RayleighPhase(cos_view) + mieScat * MiePhase(cos_view))
        // @ref Bruneton 2017 functions.glsl:ComputeScatteringDensity
        float3 scatterWeight = rayleighScat * RayleighPhase(cos_view)
                             + mieScat * MiePhase(cos_view, mieAnisotropy);

        densitySum += incomingScattering * scatterWeight * dOmega;
    }

    // ── Source B: Ground-reflected irradiance contribution ────────────────
    // @ref Bruneton 2017 functions.glsl:ComputeScatteringDensity — ground term
    //
    // If the current point can see the ground (i.e., looking down from this
    // altitude), add the contribution from ground-reflected skylight.
    // This is weighted by the scattering from the ground normal direction
    // (zenith pointed downward at ground = -zenithDir from above).
    //
    // The ground irradiance is the integral of incoming skylight over the
    // upper hemisphere at the ground, which is stored in the irradiance LUT.
    //
    // Ground contribution applies when the view looks DOWN toward the ground,
    // and we compute the scattering of the reflected light.
    // The ground is at r = bottomRadiusKm, reflecting isotropically (Lambertian).
    {
        // Compute the irradiance arriving at the ground directly below.
        // mu_s at the ground point = mu_s (same sun direction relative to zenith
        // at the current position, since we're directly above the ground point).
        float3 groundIrradiance = SampleIrradiance(deltaIrradianceTex,
            bottomRadiusKm, mu_s);

        // Lambertian BRDF: albedo / PI
        float3 groundReflectance = groundAlbedo / PI;

        // The ground normal points radially outward (= zenithDir).
        // The incoming direction from the ground toward the current point has
        // cos(zenith) = mu (from current point looking down = -mu from ground looking up).
        // We want the scattering of ground-reflected light into the view direction.
        //
        // The "incoming direction" from the ground is -zenithDir (pointing downward
        // from current point toward ground), so cos_view = dot(-zenithDir, viewDir) = -mu.
        //
        // At the current altitude, the medium scatters this reflected light:
        float3 groundScatterWeight = rayleighScat * RayleighPhase(-mu)
                                   + mieScat * MiePhase(-mu, mieAnisotropy);

        // Get transmittance from current point to the ground.
        // @ref Bruneton 2017 — transmittance from (r, -1) to (bottomRadius)
        // We use the transmittance LUT: from r looking straight down (mu_ground = -1
        // if at exact zenith above ground). In practice we need T(r, mu_ground_dir).
        // For the ground contribution, the geometry is: a column of atmosphere
        // between altitude r and bottomRadius. The transmittance is T(r, -1).
        float3 T_to_ground = GetTransmittanceToTopAtmosphereBoundary(
            tLut, linearClampSamp, r, -1.0f, bottomRadiusKm, topRadiusKm);
        // Note: this is T from r looking DOWN to bottom. The LUT stores T from r
        // looking UP to top. For the downward path, we use the identity:
        // T(r, down_to_bottom) = T(bottom, up_to_r) which we get by reversing.
        // Actually GetTransmittanceToTopAtmosphereBoundary with mu=-1 returns T
        // from r looking straight down to atmosphere TOP (which wraps around).
        // We need to use GetTransmittance helper with the correct distance.
        float d_to_ground = DistanceToBottomAtmosphereBoundary(r, -1.0f, bottomRadiusKm);
        float3 T_ground = GetTransmittance(tLut, linearClampSamp,
            r, -1.0f, d_to_ground, true, bottomRadiusKm, topRadiusKm);

        // Add ground contribution, weighted by transmittance.
        densitySum += groundReflectance * groundIrradiance * T_ground * groundScatterWeight;
    }

    // ── Write output ─────────────────────────────────────────────────────
    densityOut[tid] = float4(densitySum, 0.0f);
}
