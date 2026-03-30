/// Single-Scattering LUT Compute Shader — Precomputed 4D single-scatter table.
///
/// @ref Bruneton & Neyret 2017 "Precomputed Atmospheric Scattering"
/// @ref Hillaire 2020 "A Scalable and Production Ready Sky and Atmosphere Rendering Technique"
///
/// For each texel in a 256x128x32 3D texture, decodes (r, mu, mu_s, nu) using
/// the Bruneton 2017 4D-to-3D packing, then ray-marches the atmosphere
/// computing separate Rayleigh and Mie single scattering (without phase
/// functions — phase is applied at runtime lookup time).
///
/// Three outputs:
///   deltaRayleigh — float4(rayleigh.rgb, 0): Rayleigh-only scatter (no phase)
///   deltaMie      — float4(mie.rgb, 0):      Mie-only scatter (no phase)
///   scattering    — float4(rayleigh.rgb, mie.r): packed for runtime extrapolation
///
/// The packed scattering texture allows runtime phase separation:
///   rayleigh = s.rgb;
///   mie.rgb  = s.rgb * (s.a / max(s.r, 1e-6));  // extrapolate Mie from ratio
///   result   = rayleigh * RayleighPhase(nu) + mie * MiePhase(nu, g);
///
/// # Resources
/// - b0: 8 DWORD root constants (atmosphere medium coefficients)
/// - b2: CBV (ozone params, texture indices)
/// - s0: SamplerState linear clamp (for transmittance LUT)
///
/// # Dispatch
/// [numthreads(8,8,1)], dispatch (32, 16, 32) = 256x128x32 total threads.

#include "../include/atmosphere_lut.hlsli"

// ─────────────────────────────────────────────────────────────────────────────
// Root Constants (b0) — atmosphere scattering parameters
// ─────────────────────────────────────────────────────────────────────────────

/// @ref Bruneton 2008 "Precomputed Atmospheric Scattering" — medium coefficients layout
/// Same layout as transmittance_lut_cs.hlsl root constants.
cbuffer AtmosphereParams : register(b0) {
    float3 rayleighBeta;     ///< Rayleigh scattering coefficients (km^-1).
    float  rayleighScaleH;   ///< Rayleigh scale height (km).
    float  mieBeta;          ///< Mie scattering coefficient (km^-1).
    float  mieScaleH;        ///< Mie scale height (km).
    float  bottomRadiusKm;   ///< Planet surface radius (km).
    float  topRadiusKm;      ///< Top of atmosphere radius (km).
};

// ─────────────────────────────────────────────────────────────────────────────
// CBV (b2) — ozone and output texture indices
// ─────────────────────────────────────────────────────────────────────────────

/// Per-dispatch parameters. 256-byte aligned for D3D12 CBV.
/// @ref Bruneton 2008 "Precomputed Atmospheric Scattering" — ozone piecewise-linear density
cbuffer ScatteringParams : register(b2) {
    float3 ozoneAbsorption;         ///< Ozone absorption extinction (km^-1).
    float  ozoneLayerWidth;         ///< Boundary altitude between two linear segments (km).
    float  ozoneLinTerm0;           ///< Linear coefficient for h < layerWidth.
    float  ozoneConstTerm0;         ///< Constant term for h < layerWidth.
    float  ozoneLinTerm1;           ///< Linear coefficient for h >= layerWidth.
    float  ozoneConstTerm1;         ///< Constant term for h >= layerWidth.
    uint   transmittanceLutSrvIdx;  ///< SRV heap index for transmittance LUT (256x64 RGBA16F).
    uint   deltaRayleighUavIdx;     ///< UAV heap index for Rayleigh-only output (256x128x32).
    uint   deltaMieUavIdx;          ///< UAV heap index for Mie-only output (256x128x32).
    uint   scatteringUavIdx;        ///< UAV heap index for combined scattering output (256x128x32).
};

// ─────────────────────────────────────────────────────────────────────────────
// Sampler
// ─────────────────────────────────────────────────────────────────────────────

SamplerState linearClampSamp : register(s0);

// ─────────────────────────────────────────────────────────────────────────────
// SM5 register-bound fallback (when UZ_BINDLESS is not defined)
// ─────────────────────────────────────────────────────────────────────────────
#ifndef UZ_BINDLESS
Texture2D<float4>   g_TransmittanceLut   : register(t0);
RWTexture3D<float4> g_DeltaRayleighOut   : register(u0);
RWTexture3D<float4> g_DeltaMieOut        : register(u1);
RWTexture3D<float4> g_ScatteringOut      : register(u2);
#endif

// ─────────────────────────────────────────────────────────────────────────────
// Constants
// ─────────────────────────────────────────────────────────────────────────────

/// Number of integration steps per ray.
/// @ref Bruneton 2017 reference implementation uses 50 for precomputation.
/// Higher than the 20-step runtime march — this runs once at init time.
static const int NUM_SCATTER_STEPS = 50;

/// Mie extinction-to-scattering ratio.
/// @ref Bruneton 2008 "Precomputed Atmospheric Scattering" — MieExtinction / MieScattering
/// @constants 1.11 (standard convention: slight Mie absorption)
/// @deviations NONE
static const float MIE_EXTINCTION_RATIO = 1.11f;

/// Epsilon for extinction denominator to avoid division by zero.
/// @ref Bruneton 2008 "Precomputed Atmospheric Scattering" — max(extinction, epsilon) guard
/// @constants 1e-7
/// @deviations NONE
static const float EXT_EPS = 1e-7f;

// ─────────────────────────────────────────────────────────────────────────────
// Medium Sampling
// ─────────────────────────────────────────────────────────────────────────────

/// Computes ozone density at a given altitude above the planet surface.
/// @ref Bruneton 2008 "Precomputed Atmospheric Scattering" — piecewise linear ozone density
/// @constants ozoneLayerWidth, ozoneLinTerm0/1, ozoneConstTerm0/1 (from CBV)
/// @deviations NONE
float OzoneDensity(float altKm)
{
    return (altKm < ozoneLayerWidth)
        ? saturate(ozoneLinTerm0 * altKm + ozoneConstTerm0)
        : saturate(ozoneLinTerm1 * altKm + ozoneConstTerm1);
}

/// Samples the atmospheric medium at a given altitude.
/// Returns separate Rayleigh scattering, Mie scattering, and total extinction.
/// @ref Bruneton 2008 "Precomputed Atmospheric Scattering" — SampleAtmosphereMedium
/// @constants MIE_EXTINCTION_RATIO=1.11
/// @deviations NONE
void SampleMedium(float altKm,
    out float3 rayleighScat, out float mieScat, out float3 totalExtinction)
{
    // Rayleigh: exponential density, scattering = extinction (no absorption)
    float densR = exp(-altKm / rayleighScaleH);
    rayleighScat = rayleighBeta * densR;

    // Mie: exponential density, extinction > scattering (slight absorption)
    float densM = exp(-altKm / mieScaleH);
    mieScat = mieBeta * densM;
    float mieExt = mieScat * MIE_EXTINCTION_RATIO;

    // Ozone: piecewise-linear density, absorption only (no scattering)
    float densO = OzoneDensity(altKm);
    float3 ozoneExt = ozoneAbsorption * densO;

    totalExtinction = rayleighScat + mieExt + ozoneExt;
}

// ─────────────────────────────────────────────────────────────────────────────
// Transmittance LUT Sampling
// ─────────────────────────────────────────────────────────────────────────────

/// Maps (viewHeight, viewZenithCosAngle) to UV for the transmittance LUT.
/// @ref Bruneton 2017 — non-linear parameterization concentrating precision near horizon.
/// Same implementation as transmittance_lut_cs.hlsl:TransmittanceLutParamsToUv.
void TransmittanceLutParamsToUv(float viewHeight, float viewZenithCosAngle,
    out float2 uv)
{
    float H   = sqrt(max(0.0f, topRadiusKm * topRadiusKm - bottomRadiusKm * bottomRadiusKm));
    float rho = sqrt(max(0.0f, viewHeight * viewHeight - bottomRadiusKm * bottomRadiusKm));

    float discriminant = viewHeight * viewHeight
        * (viewZenithCosAngle * viewZenithCosAngle - 1.0f)
        + topRadiusKm * topRadiusKm;
    float d = max(0.0f, (-viewHeight * viewZenithCosAngle + sqrt(max(0.0f, discriminant))));

    float dMin = topRadiusKm - viewHeight;
    float dMax = rho + H;
    float uU   = (dMax > dMin) ? (d - dMin) / (dMax - dMin) : 0.0f;
    float uV   = (H > 0.0f) ? rho / H : 0.0f;

    uv = saturate(float2(uU, uV));
}

/// Samples the transmittance LUT from a given position toward a given direction.
/// Returns the transmittance from the position to the top of the atmosphere (or ground).
/// @ref Bruneton 2008 "Precomputed Atmospheric Scattering" — GetTransmittance
float3 SampleTransmittanceLut(Texture2D<float4> tLut, float3 pos, float3 dir)
{
    float h = length(pos);
    float cosZenith = dot(pos / h, dir);
    float2 uv;
    TransmittanceLutParamsToUv(h, cosZenith, uv);
    return tLut.SampleLevel(linearClampSamp, uv, 0).rgb;
}

/// Computes the transmittance between two points along a ray, using the LUT.
/// @ref Bruneton 2017 — T(P, Q) = T(P, top) / T(Q, top)
/// For rays toward the ground, uses the symmetry property:
///   T(P, Q) for downward ray = T(Q, P) for upward ray (same path).
float3 GetTransmittanceBetween(Texture2D<float4> tLut,
    float r, float mu, float d, bool rayIntersectsGround)
{
    // Position P is at (0, 0, r) in local coords, direction is (sinTheta, 0, mu).
    // After traveling distance d along the ray, the new position Q has:
    float r_d = sqrt(max(0.0f, r * r + d * d + 2.0f * r * d * mu));
    // The new cos zenith at Q:
    float mu_d = clamp((r * mu + d) / max(r_d, 1e-6f), -1.0f, 1.0f);

    float3 T_P, T_Q;

    if (rayIntersectsGround) {
        // For ground-intersecting rays, use the reverse-direction trick:
        // T(P→ground) = T(bottom, reverse_P) and T(Q→ground) = T(bottom, reverse_Q)
        // T(P→Q) = T(Q→ground) / T(P→ground)
        // In LUT coords: use -mu and -mu_d (reverse direction to look upward from ground).
        float2 uvP, uvQ;
        TransmittanceLutParamsToUv(r, -mu, uvP);
        TransmittanceLutParamsToUv(r_d, -mu_d, uvQ);
        T_P = tLut.SampleLevel(linearClampSamp, uvP, 0).rgb;
        T_Q = tLut.SampleLevel(linearClampSamp, uvQ, 0).rgb;
        // T(P→Q) = T(P→ground) / T(Q→ground) = T_P / T_Q (reversed)
        return min(T_P / max(T_Q, float3(1e-7f, 1e-7f, 1e-7f)), float3(1, 1, 1));
    } else {
        // Standard case: T(P→Q) = T(P→top) / T(Q→top)
        float2 uvP, uvQ;
        TransmittanceLutParamsToUv(r, mu, uvP);
        TransmittanceLutParamsToUv(r_d, mu_d, uvQ);
        T_P = tLut.SampleLevel(linearClampSamp, uvP, 0).rgb;
        T_Q = tLut.SampleLevel(linearClampSamp, uvQ, 0).rgb;
        return min(T_P / max(T_Q, float3(1e-7f, 1e-7f, 1e-7f)), float3(1, 1, 1));
    }
}

/// Computes the transmittance from a given position toward the sun (to top of atmosphere).
/// @ref Bruneton 2008 "Precomputed Atmospheric Scattering" — GetTransmittance to sun
float3 GetTransmittanceToSun(Texture2D<float4> tLut, float r, float mu_s)
{
    float2 uv;
    TransmittanceLutParamsToUv(r, mu_s, uv);
    return tLut.SampleLevel(linearClampSamp, uv, 0).rgb;
}

// ─────────────────────────────────────────────────────────────────────────────
// Ray-Sphere Geometry
// ─────────────────────────────────────────────────────────────────────────────

/// Distance from a point at height r looking at zenith angle cos(mu) to the
/// top of the atmosphere sphere.
/// @ref Bruneton 2017 — DistanceToTopAtmosphereBoundary
float DistanceToTopAtmosphereBoundary(float r, float mu)
{
    float discriminant = r * r * (mu * mu - 1.0f) + topRadiusKm * topRadiusKm;
    return max(0.0f, -r * mu + sqrt(max(0.0f, discriminant)));
}

/// Distance from a point at height r looking at zenith angle cos(mu) to the
/// bottom of the atmosphere (planet surface).
/// @ref Bruneton 2017 — DistanceToBottomAtmosphereBoundary
float DistanceToBottomAtmosphereBoundary(float r, float mu)
{
    float discriminant = r * r * (mu * mu - 1.0f) + bottomRadiusKm * bottomRadiusKm;
    return max(0.0f, -r * mu - sqrt(max(0.0f, discriminant)));
}

/// Returns true if a ray from height r with cos(zenith) = mu intersects the
/// planet surface.
/// @ref Bruneton 2017 — RayIntersectsGround
bool RayIntersectsGround(float r, float mu)
{
    return (mu < 0.0f) &&
           (r * r * (mu * mu - 1.0f) + bottomRadiusKm * bottomRadiusKm >= 0.0f);
}

// ─────────────────────────────────────────────────────────────────────────────
// Main Compute Kernel
// ─────────────────────────────────────────────────────────────────────────────

/// @ref Bruneton 2008 "Precomputed Atmospheric Scattering" — single scatter integration
/// @ref Hillaire 2020 "A Scalable and Production Ready Sky and Atmosphere Rendering Technique" — Frostbite integration
/// @constants NUM_SCATTER_STEPS=50, MIE_EXTINCTION_RATIO=1.11, EXT_EPS=1e-7
/// @deviations NONE — standard Bruneton 2008 single scatter precomputation
[numthreads(8, 8, 1)]
void main(uint3 tid : SV_DispatchThreadID)
{
    // ── Bounds check ─────────────────────────────────────────────────────
    if (tid.x >= SCATTERING_TEX_WIDTH ||
        tid.y >= SCATTERING_TEX_HEIGHT ||
        tid.z >= SCATTERING_TEX_DEPTH)
        return;

    // ── Fetch output UAVs via bindless ───────────────────────────────────
#ifdef UZ_BINDLESS
    RWTexture3D<float4> deltaRayleighOut = ResourceDescriptorHeap[deltaRayleighUavIdx];
    RWTexture3D<float4> deltaMieOut      = ResourceDescriptorHeap[deltaMieUavIdx];
    RWTexture3D<float4> scatteringOut    = ResourceDescriptorHeap[scatteringUavIdx];
#else
    RWTexture3D<float4> deltaRayleighOut = g_DeltaRayleighOut;
    RWTexture3D<float4> deltaMieOut      = g_DeltaMieOut;
    RWTexture3D<float4> scatteringOut    = g_ScatteringOut;
#endif

    // ── Fetch transmittance LUT ──────────────────────────────────────────
#ifdef UZ_BINDLESS
    Texture2D<float4> tLut = ResourceDescriptorHeap[transmittanceLutSrvIdx];
#else
    Texture2D<float4> tLut = g_TransmittanceLut;
#endif

    // ── Decode texel to atmosphere parameters ────────────────────────────
    // Bruneton 2017 4D→3D mapping: (r, mu, mu_s, nu) packed into (x, y, z).
    float3 fragCoord = float3(tid) + 0.5f;

    float r, mu, mu_s, nu;
    bool rayIntersectsGround_;
    // @ref Bruneton 2017 — mu_s_min = cos(102°) = -0.2079
    static const float kMuSMin = -0.2079116908f;
    GetRMuMuSNuFromScatteringTextureFragCoord(
        fragCoord, bottomRadiusKm, topRadiusKm, kMuSMin,
        r, mu, mu_s, nu, rayIntersectsGround_);

    // ── Compute ray length to atmosphere boundary ────────────────────────
    float rayLength = rayIntersectsGround_
        ? DistanceToBottomAtmosphereBoundary(r, mu)
        : DistanceToTopAtmosphereBoundary(r, mu);

    // Early-out for zero-length rays (camera at atmosphere edge looking outward)
    if (rayLength <= 0.0f) {
        deltaRayleighOut[tid] = float4(0, 0, 0, 0);
        deltaMieOut[tid]      = float4(0, 0, 0, 0);
        scatteringOut[tid]    = float4(0, 0, 0, 0);
        return;
    }

    float dt = rayLength / float(NUM_SCATTER_STEPS);

    // ── Ray march and accumulate scattering ──────────────────────────────
    // Separate Rayleigh and Mie accumulation WITHOUT phase functions.
    // Phase is applied at runtime lookup time.
    //
    // Uses Frostbite analytical per-step integration:
    //   Sint = (S - S * exp(-ext * dt)) / max(ext, eps)
    // @ref Hillaire 2016 "Physically Based & Unified Volumetric Rendering in Frostbite" (same formulation)
    float3 rayleighSum = float3(0, 0, 0);
    float3 mieSum      = float3(0, 0, 0);
    float3 throughput  = float3(1, 1, 1);

    for (int i = 0; i < NUM_SCATTER_STEPS; ++i)
    {
        // Midpoint of current step along the ray
        float t_i = (float(i) + 0.5f) * dt;

        // Position along ray: distance from planet center and derived angles
        // r_i = |P + t_i * w| where P is at height r along the up axis
        // and w has cos(zenith) = mu.
        // |P + tw|^2 = r^2 + t^2 + 2*r*t*mu
        float r_i = sqrt(max(0.0f, r * r + t_i * t_i + 2.0f * r * t_i * mu));

        // Altitude above planet surface (clamped to 0)
        // @ref Bruneton 2008 "Precomputed Atmospheric Scattering" — altitude clamped to [0, topRadius - bottomRadius]
        float alt_i = max(0.0f, r_i - bottomRadiusKm);

        // Cosine of sun zenith angle at this sample point:
        //   mu_s_i = dot(normalize(P_i), sunDir)
        // Using the relation: r_i * mu_s_i = r * mu_s + t_i * nu
        // where nu = dot(viewDir, sunDir)
        float mu_s_i = clamp((r * mu_s + t_i * nu) / max(r_i, 1e-6f), -1.0f, 1.0f);

        // ── Transmittance from camera to sample ──────────────────────────
        // Uses the LUT-based computation: T(camera, sample) via ratio.
        float3 T_camera_to_sample = GetTransmittanceBetween(
            tLut, r, mu, t_i, rayIntersectsGround_);

        // ── Transmittance from sample to sun ─────────────────────────────
        // Direct lookup: T(sample_pos, sun_dir) to top of atmosphere.
        // @ref Bruneton 2008 "Precomputed Atmospheric Scattering" — GetTransmittanceToSun
        float3 T_sample_to_sun = GetTransmittanceToSun(tLut, r_i, mu_s_i);

        // ── Planet shadow check ──────────────────────────────────────────
        // If the sun ray from this sample hits the planet, no direct light.
        // @ref Bruneton 2008 "Precomputed Atmospheric Scattering" — planet shadow via ray-sphere intersection
        float planetShadow = 1.0f;
        {
            // Ray from sample toward sun: check intersection with planet.
            // r_i * mu_s_i < 0 means looking downward; check discriminant.
            float disc = r_i * r_i * (mu_s_i * mu_s_i - 1.0f)
                       + bottomRadiusKm * bottomRadiusKm;
            if (mu_s_i < 0.0f && disc >= 0.0f) {
                planetShadow = 0.0f;
            }
        }

        // ── Sample medium at this altitude ───────────────────────────────
        float3 rayleighScat;
        float  mieScat;
        float3 extinction;
        SampleMedium(alt_i, rayleighScat, mieScat, extinction);

        // ── Frostbite analytical per-step integration ────────────────────
        // @ref Hillaire 2016 "Physically Based & Unified Volumetric Rendering in Frostbite"
        //   Sint = (S - S * exp(-ext*dt)) / max(ext, eps)
        // where S = scatter * T_to_sun * planetShadow
        float3 expTerm = exp(-extinction * dt);

        // Source term: scattering coefficient * sun transmittance * shadow
        float3 sunContrib = T_sample_to_sun * planetShadow;

        float3 S_rayleigh = rayleighScat * sunContrib;
        float3 S_mie      = mieScat * sunContrib;

        // Analytical integration of source over the step:
        //   integral of S * exp(-ext * s) ds from 0 to dt
        //   = S * (1 - exp(-ext * dt)) / ext
        float3 safeExt = max(extinction, float3(EXT_EPS, EXT_EPS, EXT_EPS));
        float3 intFactor = (1.0f - expTerm) / safeExt;

        rayleighSum += throughput * T_camera_to_sample * S_rayleigh * intFactor;
        mieSum      += throughput * T_camera_to_sample * S_mie      * intFactor;

        // Note: throughput here is 1.0 for the entire march because we use
        // the exact T_camera_to_sample from the LUT at each step, rather than
        // accumulating throughput incrementally. This is the precomputation
        // approach: each step independently looks up the exact transmittance
        // from camera to that point.
        //
        // In the incremental approach (runtime marching), throughput *= expTerm.
        // Here, T_camera_to_sample already encodes the full transmittance,
        // so we do NOT multiply throughput by expTerm (it stays at 1.0).
        //
        // The intFactor handles the within-step integration while
        // T_camera_to_sample handles the camera-to-step transmittance.
    }

    // ── Write outputs ────────────────────────────────────────────────────
    // deltaRayleigh: Rayleigh scattering (no phase function applied)
    deltaRayleighOut[tid] = float4(rayleighSum, 0.0f);

    // deltaMie: Mie scattering (no phase function applied)
    deltaMieOut[tid] = float4(mieSum, 0.0f);

    // Combined scattering: .rgb = Rayleigh, .a = Mie red channel only.
    // At runtime, Mie is extrapolated from the red channel ratio:
    //   mie.rgb = s.rgb * (s.a / max(s.r, 1e-6))
    // This saves one full 3D texture worth of VRAM compared to storing
    // Rayleigh and Mie separately.
    //
    // @ref Bruneton 2008 "Precomputed Atmospheric Scattering" — scattering texture packing convention
    scatteringOut[tid] = float4(rayleighSum, mieSum.r);
}
