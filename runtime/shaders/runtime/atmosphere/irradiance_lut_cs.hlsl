/// Irradiance LUT Compute Shader — Ground irradiance from direct sun and skylight.
///
/// @ref Bruneton & Neyret 2017 "Precomputed Atmospheric Scattering"
///      Section 4 — ComputeDirectIrradianceTexture, ComputeIndirectIrradianceTexture
/// @ref Reference implementation: github.com/ebruneton/precomputed_atmospheric_scattering
///      atmosphere/functions.glsl (BSD-3 license)
///
/// Computes the ground irradiance arriving at the planet surface. Two modes:
///
/// **Order 1 (Direct Irradiance):**
///   E_direct(r, mu_s) = sunIrradiance * T(r, sunDir) * max(mu_s, 0)
///   The sun illuminates the ground directly, attenuated by transmittance.
///
/// **Order N > 1 (Indirect Irradiance):**
///   E_indirect(r, mu_s) = integral over upper hemisphere of incoming scattered
///   light from the (N-1)-th order, weighted by cos(theta).
///   Uses hemisphere sampling with the scattering textures.
///
/// Output: 64x16 RGBA16F texture parameterized by (r, mu_s).
///
/// The irradiance LUT is written in two outputs:
///   - deltaIrradiance: this order's irradiance (used by scattering density pass)
///   - irradiance: accumulated total (additive across orders)
///
/// # Resources
/// - b0: 8 DWORD root constants (atmosphere medium coefficients)
/// - b2: CBV (ozone params, texture indices, scattering order)
/// - s0: SamplerState linear clamp
///
/// # Dispatch
/// [numthreads(8,8,1)], dispatch (8, 2, 1) = 64x16 total threads.
///
/// @constants NUM_HEMISPHERE_DIRECTIONS=32, PI=3.14159265358979323846
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
cbuffer IrradianceParams : register(b2) {
    float3 ozoneAbsorption;        ///< Ozone absorption extinction (km^-1) — unused for irradiance
                                   ///< but keeps consistent CBV layout with other atmosphere passes.
    float  ozoneLayerWidth;        ///< Boundary altitude between two linear segments (km).
    float  ozoneLinTerm0;          ///< Linear coefficient for h < layerWidth.
    float  ozoneConstTerm0;        ///< Constant term for h < layerWidth.
    float  ozoneLinTerm1;          ///< Linear coefficient for h >= layerWidth.
    float  ozoneConstTerm1;        ///< Constant term for h >= layerWidth.
    uint   transmittanceLutSrvIdx; ///< SRV heap index for transmittance LUT (256x64 RGBA16F).
    uint   deltaRayleighSrvIdx;    ///< Order 1: single scatter Rayleigh. Order > 2: delta multiple scatter.
    uint   deltaMieSrvIdx;         ///< Order 1: single scatter Mie. Order > 2: UINT32_MAX (unused).
    uint   irradianceUavIdx;       ///< UAV heap index for accumulated irradiance (additive).
    uint   deltaIrradianceUavIdx;  ///< UAV heap index for this order's delta irradiance.
    float  mieAnisotropy;          ///< Henyey-Greenstein g parameter for Mie phase.
    int    scatteringOrder;        ///< Current scattering order (1 = direct, 2+ = indirect).
    float  muSMin;                 ///< Minimum mu_s for scattering LUT mapping.
    float  _pad;
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
RWTexture2D<float4> g_IrradianceUav        : register(u0);
RWTexture2D<float4> g_DeltaIrradianceUav   : register(u1);
#endif

// ─────────────────────────────────────────────────────────────────────────────
// Constants
// ─────────────────────────────────────────────────────────────────────────────

/// Number of hemisphere directions for indirect irradiance integration.
/// @ref Bruneton 2017 reference uses 32 for precomputation balance.
/// @constants 32
/// @deviations NONE
static const int NUM_HEMISPHERE_DIRECTIONS = 32;

/// Golden ratio for Fibonacci sampling.
static const float GOLDEN_RATIO = 0.6180339887498949f;

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
// Main Compute Kernel
// ─────────────────────────────────────────────────────────────────────────────

/// @ref Bruneton 2017 "Precomputed Atmospheric Scattering"
///      ComputeDirectIrradianceTexture (order 1)
///      ComputeIndirectIrradianceTexture (order > 1)
///
/// For each texel in the 64x16 irradiance LUT:
///
/// **Order 1 (Direct):**
///   Decode (r, mu_s) from UV.
///   irradiance = T(r, sunDir) * max(mu_s, 0)
///   (Normalized to sunIrradiance = 1; actual sun color applied at runtime.)
///
/// **Order N > 1 (Indirect):**
///   Decode (r, mu_s) from UV.
///   Integrate over the upper hemisphere:
///     For each direction omega_i with cos(zenith) > 0:
///       Sample the (N-1)-th order scattering arriving from omega_i at (r, mu_i, mu_s, nu_i).
///       Weight by cos(zenith) * dOmega.
///   The sum gives the irradiance from scattered skylight at the ground.
///
/// @constants NUM_HEMISPHERE_DIRECTIONS=32
/// @deviations NONE
[numthreads(8, 8, 1)]
void main(uint3 tid : SV_DispatchThreadID)
{
    // ── Bounds check ─────────────────────────────────────────────────────
    if (tid.x >= IRRADIANCE_TEX_WIDTH || tid.y >= IRRADIANCE_TEX_HEIGHT)
        return;

    // ── Fetch resources via bindless ─────────────────────────────────────
#ifdef UZ_BINDLESS
    Texture2D<float4>   tLut          = ResourceDescriptorHeap[transmittanceLutSrvIdx];
    RWTexture2D<float4> irradianceUav = ResourceDescriptorHeap[irradianceUavIdx];
    RWTexture2D<float4> deltaIrrUav   = ResourceDescriptorHeap[deltaIrradianceUavIdx];
#else
    Texture2D<float4>   tLut          = g_TransmittanceLut;
    RWTexture2D<float4> irradianceUav = g_IrradianceUav;
    RWTexture2D<float4> deltaIrrUav   = g_DeltaIrradianceUav;
#endif

    // ── Decode texel to atmosphere parameters ────────────────────────────
    float2 uv = (float2(tid.xy) + 0.5f) / float2(IRRADIANCE_TEX_WIDTH, IRRADIANCE_TEX_HEIGHT);

    float r, mu_s;
    GetRMuSFromIrradianceTextureUv(uv, bottomRadiusKm, topRadiusKm, r, mu_s);

    float3 irradiance = float3(0.0f, 0.0f, 0.0f);

    if (scatteringOrder == 1) {
        // ── Direct irradiance (order 1) ──────────────────────────────
        // @ref Bruneton 2017 functions.glsl — ComputeDirectIrradiance
        //
        // E_direct = sunIrradiance * T(r, mu_s) * max(mu_s, 0)
        //
        // We compute with sunIrradiance = 1.0 (normalized). The actual
        // sun spectral irradiance is multiplied at runtime.
        //
        // GetTransmittanceToTopAtmosphereBoundary gives T from (r, mu_s)
        // to the top of the atmosphere along the sun direction.
        float3 T_sun = GetTransmittanceToTopAtmosphereBoundary(
            tLut, linearClampSamp,
            r, mu_s, bottomRadiusKm, topRadiusKm);

        // The solar irradiance at the ground is attenuated by the atmosphere
        // and modulated by the cosine of the sun zenith angle.
        // max(mu_s, 0) ensures no irradiance when the sun is below the horizon.
        irradiance = T_sun * max(mu_s, 0.0f);

    } else {
        // ── Indirect irradiance (order N > 1) ───────────────────────
        // @ref Bruneton 2017 functions.glsl — ComputeIndirectIrradiance
        //
        // E_indirect = integral_hemisphere [ L_incoming(omega_i) * cos(theta_i) * dOmega ]
        //
        // where L_incoming is the (N-1)-th order scattered light from
        // direction omega_i at the current point (r, mu_s).
        //
        // We sample the upper hemisphere using Fibonacci spiral directions,
        // weighted by cos(theta_i) for the Lambert cosine law.

        // Fetch the scattering textures for the previous order.
#ifdef UZ_BINDLESS
        Texture3D<float4> deltaRayleighTex = ResourceDescriptorHeap[deltaRayleighSrvIdx];
        Texture3D<float4> deltaMieTex      = ResourceDescriptorHeap[deltaMieSrvIdx];
#else
        Texture3D<float4> deltaRayleighTex = g_DeltaRayleighTex;
        Texture3D<float4> deltaMieTex      = g_DeltaMieTex;
#endif

        // Reconstruct the sun direction in the local coordinate frame.
        // At the ground point, zenithDir = (0,0,1) (Z-up).
        // sunDir has cos(zenith) = mu_s, so:
        float sin_theta_s = SafeSqrt(1.0f - mu_s * mu_s);
        float3 sunDir = float3(sin_theta_s, 0.0f, mu_s);

        for (int i = 0; i < NUM_HEMISPHERE_DIRECTIONS; ++i)
        {
            // ── Fibonacci hemisphere sampling ────────────────────────
            // For the upper hemisphere, we use Fibonacci spiral sampling
            // restricted to cos(theta) > 0 (z > 0).
            //
            // @ref Bruneton 2017 reference — uniform hemisphere distribution.
            // Map [0, NUM_HEMISPHERE_DIRECTIONS) to the upper hemisphere:
            //   cos_theta uniformly distributed in [0, 1]
            //   phi = golden angle spiral
            float cos_theta_i = (float(i) + 0.5f) / float(NUM_HEMISPHERE_DIRECTIONS);
            float sin_theta_i = SafeSqrt(1.0f - cos_theta_i * cos_theta_i);
            float phi_i = 2.0f * PI * frac(float(i) * GOLDEN_RATIO);

            float3 omega_i = float3(
                sin_theta_i * cos(phi_i),
                sin_theta_i * sin(phi_i),
                cos_theta_i);

            // Solid angle element for uniform hemisphere sampling:
            // Each direction covers 2*PI / NUM_HEMISPHERE_DIRECTIONS steradians.
            // The hemisphere has total solid angle 2*PI.
            float dOmega = 2.0f * PI / float(NUM_HEMISPHERE_DIRECTIONS);

            // ── Look up incoming scattering from omega_i ─────────────
            // At the current ground point (radius r), light arrives from
            // direction omega_i with the (N-1)-th order scattering.
            //
            // For the scattering LUT lookup:
            //   mu_i   = dot(zenithDir, omega_i) = omega_i.z = cos_theta_i
            //   mu_s_i = mu_s (sun angle relative to zenith is the same)
            //   nu_i   = dot(omega_i, sunDir) (angle between incoming direction and sun)
            float mu_i = omega_i.z;
            float nu_i = dot(omega_i, sunDir);

            // The ray from the ground point in direction omega_i goes upward
            // (cos_theta_i > 0), so it does NOT intersect the ground.
            bool hits_ground = false;

            float3 incomingScattering;

            if (scatteringOrder == 2) {
                // Order 2: previous order (1) is stored as separate Rayleigh + Mie
                // without phase functions. Apply phase functions here.
                // @ref Bruneton 2017 — single scatter uses phase-less storage.
                float3 rayleigh1 = SampleScatteringTexture(
                    deltaRayleighTex, linearClampSamp,
                    r, mu_i, mu_s, nu_i, hits_ground,
                    bottomRadiusKm, topRadiusKm, muSMin);
                float3 mie1 = SampleScatteringTexture(
                    deltaMieTex, linearClampSamp,
                    r, mu_i, mu_s, nu_i, hits_ground,
                    bottomRadiusKm, topRadiusKm, muSMin);

                incomingScattering = rayleigh1 * RayleighPhase(nu_i) +
                                     mie1 * MiePhase(nu_i, mieAnisotropy);
            } else {
                // Order > 2: delta scattering already has phase functions applied.
                // deltaRayleighSrvIdx points to the combined delta scattering.
                incomingScattering = SampleScatteringTexture(
                    deltaRayleighTex, linearClampSamp,
                    r, mu_i, mu_s, nu_i, hits_ground,
                    bottomRadiusKm, topRadiusKm, muSMin);
            }

            // ── Weight by Lambert cosine law ─────────────────────────
            // The irradiance contribution from direction omega_i is:
            //   L_incoming * cos(theta_i) * dOmega
            // cos(theta_i) = omega_i.z = cos_theta_i (already > 0 by construction).
            irradiance += incomingScattering * cos_theta_i * dOmega;
        }
    }

    // ── Write outputs ────────────────────────────────────────────────────
    // Delta irradiance: this order's contribution only.
    // Used by the scattering density computation for the next order.
    deltaIrrUav[tid.xy] = float4(irradiance, 0.0f);

    // Accumulated irradiance: ADD this order's contribution to the running total.
    // For order 1 (direct), this is the first write.
    // For order > 1 (indirect), this adds to the previous total.
    // @ref Bruneton 2017 — additive accumulation of irradiance orders.
    //
    // Note: for order 1 we still add (the accumulator should be cleared to zero
    // before the first dispatch). This keeps the logic uniform across all orders.
    float4 existing = irradianceUav[tid.xy];
    existing.rgb += irradiance;
    irradianceUav[tid.xy] = existing;
}
