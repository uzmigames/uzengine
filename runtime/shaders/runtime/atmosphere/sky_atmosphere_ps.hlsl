/// Sky atmosphere pixel shader — Bruneton 2017 LUT-based with ray march fallback.
///
/// Follows industry-standard SkyAtmosphere coordinate conventions:
/// - All distances in KILOMETERS (1 world unit = 1 centimeter = 0.00001 km, standard)
/// - Planet center at (0, 0, 0) in sky-space; camera offset by bottomRadiusKm
/// - Camera snapped to minimum 5m above planet surface (prevents near-surface artifacts)
/// - Light direction points TOWARD the sun (PerFrameCB.lightDir = toward-sun direction)
/// - Scattering coefficients in km^-1
///
/// # Integration Method
/// PRIMARY: Precomputed 3D scattering LUT (Bruneton 2017) — single 4D->3D texture
/// lookup replaces the per-pixel ray march. The LUT stores Rayleigh scattering in
/// .rgb and Mie scattering (red channel only) in .a. Phase functions are applied at
/// lookup time, and Mie color is extrapolated from the Rayleigh/Mie ratio.
///
/// FALLBACK: 32-step ray march with Frostbite analytical per-step integration when
/// the precomputed scattering LUT is not available (SM5 path or first frames before
/// precomputation completes).
///
/// @ref Bruneton & Neyret 2017 "Precomputed Atmospheric Scattering"
///      Section 4 — Texture parameterization, Section 5 — Runtime rendering.
/// @ref Hillaire 2020 "A Scalable and Production Ready Sky and Atmosphere Rendering Technique"
///
/// # Inputs
/// - PerFrameCB at b1: camera, invViewProj, lightDir, lightColor
/// - SkyAtmosphereCB at b2: atmosphere parameters (all distances in km)
/// - Depth texture (t0): to skip pixels with geometry
/// - Transmittance LUT (t1 / bindless): precomputed 256x64 RGBA16F texture
///   encoding sun transmittance per (height, zenith angle)
/// - Scattering LUT 3D (bindless only): precomputed 256x128x32 RGBA16F texture
///   encoding single-scattering per (r, mu, mu_s, nu)
///
/// # Output
/// Linear HDR color (tonemap applied in separate fullscreen pass).

#include "../include/pbr_common.hlsli"
#include "../include/units.hlsli"
#include "../include/atmosphere_lut.hlsli"

// ─────────────────────────────────────────────────────────────────────────────
// Input Structure
// ─────────────────────────────────────────────────────────────────────────────

struct FSInput {
    float4 positionCS : SV_Position;
    float2 texcoord   : TEXCOORD0;
};

// ─────────────────────────────────────────────────────────────────────────────
// Constant Buffers
// ─────────────────────────────────────────────────────────────────────────────

#ifdef UZ_BINDLESS
struct SkyIndices {
    uint depthIdx;
    uint transmittanceLutIdx;       // RGBA16F 256x64 precomputed transmittance LUT
    uint multiScatteringLutIdx;     // R11G11B10F 32x32 multi-scattering LUT
    uint apVolumeSrvIdx;            // SRV for 32x32x16 RGBA16F AP volume (0xFFFFFFFF = none)
    uint scatteringLut3DIdx;        // RGBA16F 256x128x32 precomputed Bruneton single-scattering LUT
                                    // (0xFFFFFFFF = not available, fall back to ray march)
    uint irradianceLutIdx;          // RGBA16F 64x16 precomputed ground irradiance LUT
                                    // (0xFFFFFFFF = not available, fall back to direct-only)
};
ConstantBuffer<SkyIndices> skyIndices : register(b0);
#endif

cbuffer PerFrameCB : register(b1) {
    row_major float4x4 viewProj;
    float4   cameraPos;
    float4   timeParams;
    float4   lightDir;
    float4   lightColor;
    float4   ambientColor;
    row_major float4x4 invViewProj;
    float    nearPlane;
    float    farPlane;
    uint     irradianceSrvIdx;
    uint     prefilteredSrvIdx;
    uint     brdfLutSrvIdx;
    uint     prefilteredMipLevels;
    float    skyLightIntensity;
    float    skyLightColorR;
    float    skyLightColorG;
    float    skyLightColorB;
    float    skyLightMinAmbient;
    float    exposure;           // Auto-exposure multiplier (applied before tonemap)
};

cbuffer SkyAtmosphereCB : register(b2) {
    float4 rayleighScattering;      // xyz = coefficients (km^-1), w = scale height (km)
    float4 mieScattering;           // x = coefficient (km^-1), y = anisotropy g, z = scale height (km), w = unused
    float4 groundAlbedo;            // xyz = ground reflectance, w = unused
    float4 sunIlluminance;          // xyz = sun color * illuminance, w = angular radius (radians)
    float  bottomRadiusKm;          // Planet radius in km (e.g. 6360.0)
    float  atmosphereHeightKm;      // Atmosphere thickness in km (e.g. 60.0)
    float  sunDiskIntensity;        // Sun disk brightness multiplier
    float  multiScatteringFactor;   // Multi-scattering approximation (0=single, 1=full)
    float  rayleighScatteringScale; // Artistic multiplier for Rayleigh scattering
    float  mieScatteringScale;      // Artistic multiplier for Mie scattering
    float  mieAbsorptionScale;      // Artistic multiplier for Mie absorption
    float  mieAbsorption;           // Mie absorption coefficient (km^-1)
    float4 skyLuminanceFactor;      // Per-channel sky brightness art direction (default 1,1,1)
    // ── Ozone absorption (reference "Other Absorption") ─────────────────────────
    // @ref SampleAtmosphereMediumRGB
    // @ref AbsorptionDensity tent coefficients
    float4 ozoneAbsorptionPacked;   // xyz = ozone absorption (km^-1), w = layerWidth (km)
    float  ozoneLinTerm0;           // Linear coefficient for h < layerWidth
    float  ozoneConstTerm0;         // Constant term for h < layerWidth
    float  ozoneLinTerm1;           // Linear coefficient for h >= layerWidth
    float  ozoneConstTerm1;         // Constant term for h >= layerWidth
    float  _skyPad[28];             // Pad to 256-byte alignment
};

#ifndef UZ_BINDLESS
Texture2D<float>  sceneDepth           : register(t0);
Texture2D<float4> transmittanceLut     : register(t1); // 256x64 RGBA16F precomputed LUT
Texture2D<float4> multiScatteringLut   : register(t2); // 32x32 R11G11B10F multi-scattering LUT
#endif

/// s0 = bilinear wrap sampler (D3D12_FILTER_MIN_MAG_MIP_LINEAR).
/// s1 = shadow comparison sampler (D3D12_FILTER_COMPARISON_*) — NOT usable for normal SampleLevel.
SamplerState linearSampler : register(s0);

// ─────────────────────────────────────────────────────────────────────────────
// Constants — standard convention
// ─────────────────────────────────────────────────────────────────────────────

/// Local aliases for unit constants (from units.hlsli).
static const float WORLD_TO_KM            = UZ_CM_TO_KM;
static const float PLANET_RADIUS_OFFSET_KM = UZ_PLANET_RADIUS_OFFSET_KM;
static const float EXT_EPS                 = UZ_EXTINCTION_EPS;

/// @ref SkyAtmosphere.SampleCountMax (CB parameter)
/// uses variable sample count per ray (lerp between Min/Max based on ray length).
/// 32 is the typical MaxSampleCount for the render sky view pass.
static const int NUM_STEPS_VIEW  = 32;

// ─────────────────────────────────────────────────────────────────────────────
// Helpers
// ─────────────────────────────────────────────────────────────────────────────

/// Rayleigh phase function: (3/16π)(1 + cos²θ)
float RayleighPhase(float cosTheta) {
    return (3.0f / (16.0f * PI)) * (1.0f + cosTheta * cosTheta);
}

/// Henyey-Greenstein phase for Mie scattering.
float MiePhase(float cosTheta, float g) {
    float g2    = g * g;
    float denom = 1.0f + g2 - 2.0f * g * cosTheta;
    return (1.0f / (4.0f * PI)) * (1.0f - g2) / (denom * sqrt(denom));
}

/// Exponential density profile: exp(-altitude / scaleHeight)
float DensityAtAltitude(float altitudeKm, float scaleHeightKm) {
    return exp(-max(0.0f, altitudeKm) / scaleHeightKm);
}

/// Ozone density at a given altitude using the reference tent distribution.
/// @ref SampleAtmosphereMediumRGB
/// @constants ozoneLayerWidth=25.0 (SkyAtmosphereCommonData.cpp:38)
/// @deviations NONE
float OzoneDensityAtAltitude(float altKm) {
    return saturate(altKm < ozoneAbsorptionPacked.w
        ? ozoneLinTerm0 * altKm + ozoneConstTerm0
        : ozoneLinTerm1 * altKm + ozoneConstTerm1);
}

/// Samples the precomputed multi-scattering LUT.
/// @ref GetMultipleScattering
/// @constants UV mapping: x = dot(up,sunDir)*0.5+0.5, y = (h-bR)/(tR-bR)
/// @deviations NONE
float3 SampleMultiScatteringLut(Texture2D<float4> lut, SamplerState samp,
    float3 samplePos, float3 sunDir_, float bRadius, float tRadius)
{
    float3 upVec = normalize(samplePos);
    float viewZenithCos = dot(upVec, sunDir_);
    float h = length(samplePos);
    float2 uv = saturate(float2(
        viewZenithCos * 0.5f + 0.5f,
        (h - bRadius) / (tRadius - bRadius)
    ));
    return lut.SampleLevel(samp, uv, 0).rgb;
}

/// Ray-sphere intersection. Returns (tNear, tFar).
/// tNear < 0 means ray origin is inside sphere.
/// Returns (-1,-1) if no intersection.
float2 RaySphereIntersect(float3 origin, float3 dir, float3 center, float radius) {
    float3 oc   = origin - center;
    float  b    = dot(oc, dir);
    float  c    = dot(oc, oc) - radius * radius;
    float  disc = b * b - c;
    if (disc < 0.0f) return float2(-1.0f, -1.0f);
    float sq = sqrt(disc);
    return float2(-b - sq, -b + sq);
}

// ─────────────────────────────────────────────────────────────────────────────
// Transmittance LUT Sampling
// ─────────────────────────────────────────────────────────────────────────────

/// Maps (viewHeight, viewZenithCosAngle) to UV for the transmittance LUT.
///
/// Uses Bruneton 2017 non-linear parameterization that concentrates precision
/// near the horizon where transmittance gradients are steepest. Must match
/// the parameterization in transmittance_lut_cs.hlsl.
void TransmittanceLutParamsToUv(float viewHeight, float viewZenithCosAngle,
    float bRadius, float tRadius, out float2 uv)
{
    float H   = sqrt(max(0.0f, tRadius * tRadius - bRadius * bRadius));
    float rho = sqrt(max(0.0f, viewHeight * viewHeight - bRadius * bRadius));

    float discriminant = viewHeight * viewHeight
        * (viewZenithCosAngle * viewZenithCosAngle - 1.0f)
        + tRadius * tRadius;
    float d = max(0.0f, (-viewHeight * viewZenithCosAngle + sqrt(max(0.0f, discriminant))));

    float dMin = tRadius - viewHeight;
    float dMax = rho + H;
    float uU   = (d - dMin) / (dMax - dMin);
    float uV   = rho / H;

    uv = float2(uU, uV);
}

/// Samples the precomputed transmittance LUT for a given position and direction.
///
/// Computes the UV from the sample point's height and the cosine of the angle
/// between the position vector and the query direction (typically the sun dir).
///
/// # Arguments
/// * `lut` — The precomputed 256x64 RGBA16F transmittance texture.
/// * `samp` — Bilinear sampler for smooth interpolation between LUT texels.
/// * `samplePos` — Position in sky-space (km), relative to planet center at origin.
/// * `queryDir` — Normalized direction to look up (typically sunDir).
/// * `bRadius` — Planet surface radius (km).
/// * `tRadius` — Top-of-atmosphere radius (km).
///
/// # Returns
/// RGB transmittance in [0,1]^3.
float3 SampleTransmittanceLut(Texture2D<float4> lut, SamplerState samp,
    float3 samplePos, float3 queryDir,
    float bRadius, float tRadius)
{
    float viewHeight = length(samplePos);
    // Cosine of angle between the up-vector at samplePos and queryDir
    float viewZenithCosAngle = dot(normalize(samplePos), queryDir);

    float2 uv;
    TransmittanceLutParamsToUv(viewHeight, viewZenithCosAngle, bRadius, tRadius, uv);
    return lut.SampleLevel(samp, uv, 0).rgb;
}

// ─────────────────────────────────────────────────────────────────────────────
// Pixel Shader Entry Point
// ─────────────────────────────────────────────────────────────────────────────

float4 main(FSInput input) : SV_Target {
    float2 uv = input.texcoord;

    // ── Read depth — skip pixels with geometry ──────────────────────────────
#ifdef UZ_BINDLESS
    Texture2D<float> depthTex = ResourceDescriptorHeap[skyIndices.depthIdx];
    float depth = depthTex.Sample(linearSampler, uv).r;
#else
    float depth = sceneDepth.Sample(linearSampler, uv).r;
#endif

    // Only render sky where there is no geometry.
    // Reversed-Z: far plane = 0.0, so sky pixels have depth near 0.
    if (depth > 0.0001f)
        discard;

    // ── Reconstruct world-space ray direction ────────────────────────────────
    /// @ref TranslatedWorldToClip (camera-relative rendering)
    /// @deviations Near/far clip subtraction used instead of the reference engine's TranslatedWorld
    ///   system. Both achieve the same goal: the camera translation cancels in the
    ///   subtraction, eliminating catastrophic cancellation at large world coords.
    float2 ndc = uv * 2.0f - 1.0f;
    ndc.y      = -ndc.y;

    // Unproject two points along the same screen ray at different NDC depths.
    // Both go through the same invViewProj, so the camera translation in the
    // matrix cancels exactly in the subtraction, giving a precise ray direction
    // even when cameraPos is thousands of units from the origin.
    // Any two distinct z values work — they define two points on the same ray.
    // We use z=0.25 and z=0.75 (avoiding extremes for numerical stability).
    float4 pt0    = mul(float4(ndc, 0.25f, 1.0f), invViewProj);
    float4 pt1    = mul(float4(ndc, 0.75f, 1.0f), invViewProj);
    float3 world0 = pt0.xyz / pt0.w;
    float3 world1 = pt1.xyz / pt1.w;
    float3 rayDir = normalize(world0 - world1);

    // ── Convert world camera to sky-space (km) ───────────────────────────────
    // UzEngine is Z-up (X-forward, Y-right, Z-up).
    // Planet center is at world origin in sky-space.
    // World Z=0 is sea level → sky Z = bottomRadiusKm.
    // cameraSkyKm = cameraPos_cm * 0.00001 + (0, 0, bottomRadiusKm)
    float3 cameraSkyKm = cameraPos.xyz * WORLD_TO_KM + float3(0.0f, 0.0f, bottomRadiusKm);

    // ── Snap camera to minimum 5m above planet surface (standard pattern) ─────────
    float distToCenter  = length(cameraSkyKm);
    float minSafeRadius = bottomRadiusKm + PLANET_RADIUS_OFFSET_KM;
    if (distToCenter < minSafeRadius) {
        cameraSkyKm = cameraSkyKm * (minSafeRadius / distToCenter);
    }

    // ── Atmosphere geometry ──────────────────────────────────────────────────
    float topRadiusKm = bottomRadiusKm + atmosphereHeightKm;

    // ── Intersect ray with atmosphere top ────────────────────────────────────
    float2 atmoHit = RaySphereIntersect(cameraSkyKm, rayDir, float3(0, 0, 0), topRadiusKm);
    if (atmoHit.y < 0.0f)
        return float4(0, 0, 0, 1);

    float tStart = max(atmoHit.x, 0.0f);
    float tEnd   = atmoHit.y;

    // Check planet intersection (used for ground contribution decision, NOT for clipping).
    // When the camera is near the surface, clipping at groundHit truncates the ray to
    // near-zero length, killing inscattering for below-horizon pixels. Instead, let the
    // ray march through the full atmosphere thickness — density at negative altitude is
    // clamped to max by DensityAtAltitude(max(0,alt)), producing correct inscattering.
    // At orbital altitude (> 10 km), clip normally for physical ground contribution.
    float2 groundHit = RaySphereIntersect(cameraSkyKm, rayDir, float3(0, 0, 0), bottomRadiusKm);
    float cameraAltForClip = length(cameraSkyKm) - bottomRadiusKm;
    if (groundHit.x > 0.0f) {
        if (cameraAltForClip > 10.0f) {
            // Orbital: clip at planet surface for physical ground contribution
            tEnd = min(tEnd, groundHit.x);
        } else {
            // Ground level: don't clip at planet surface, but cap the ray march
            // distance to prevent the path from spanning the full atmosphere diameter
            // (hundreds of km) which would waste the 32 steps on a huge interval.
            // Cap at 200 km — enough to capture all inscattering while keeping
            // step size small enough for accurate density sampling near the surface.
            tEnd = min(tEnd, tStart + 200.0f);
        }
    }

    float stepLenKm = (tEnd - tStart) / float(NUM_STEPS_VIEW);
    if (stepLenKm <= 0.0f)
        return float4(0, 0, 0, 1);

    // ── Sun direction and phase functions ────────────────────────────────────
    float3 sunDir      = normalize(lightDir.xyz);
    float  cosTheta    = dot(rayDir, sunDir);
    float  phaseR      = RayleighPhase(cosTheta);
    float  phaseM      = MiePhase(cosTheta, mieScattering.y);

    // Physical scattering coefficients (unscaled) — used for extinction.
    // Extinction must use physical values so path transmittance is correct
    // and matches the precomputed transmittance LUT.
    float3 betaR_phys = rayleighScattering.xyz;
    float  betaM_phys = mieScattering.x;

    // Artistic scattering coefficients — used for in-scattering only.
    // rayleighScatteringScale / mieScatteringScale boost or reduce the
    // amount of light scattered toward the camera without changing the
    // extinction (optical depth). This is how reference implements its artistic
    // scale parameters: they affect "how much color" the sky has, not
    // "how opaque" the atmosphere is.
    float3 betaR_art = rayleighScattering.xyz * rayleighScatteringScale;
    float  betaM_art = mieScattering.x * mieScatteringScale;

    float  scaleHR = rayleighScattering.w;   // Rayleigh scale height (km)
    float  scaleHM = mieScattering.z;        // Mie scale height (km)

    // ── Scattering integration ───────────────────────────────────────────
    // Two paths: LUT-based (primary, when precomputed 3D scattering texture is available)
    // and ray-march fallback (SM5 path or before precomputation completes).
    float3 L          = float3(0, 0, 0);
    float3 Throughput = float3(1, 1, 1);

    // ── Determine which integration path to use ──────────────────────
    // The LUT path requires SM6.6 bindless (UZ_BINDLESS) and a valid scattering
    // LUT index. When neither is available, fall back to the 32-step ray march.
#ifdef UZ_BINDLESS
    if (skyIndices.scatteringLut3DIdx != 0xFFFFFFFF) {
        // ── PRIMARY: LUT-based sky radiance (Bruneton 2017) ──────────
        // Replace the 32-step ray march with a single 4D->3D texture lookup.
        // The precomputed 3D scattering texture stores:
        //   .rgb = single-scattering Rayleigh (no phase function applied)
        //   .a   = single-scattering Mie red channel (no phase function applied)
        // Phase functions are applied at lookup time.
        //
        // @ref Bruneton & Neyret 2017 "Precomputed Atmospheric Scattering"
        //      Section 5 — "Rendering" — GetSkyRadiance function.
        // @ref Reference implementation: github.com/ebruneton/precomputed_atmospheric_scattering
        //      atmosphere/functions.glsl, GetSkyRadiance (BSD-3 license)
        // @constants muSMin = cos(102 deg) = -0.2079116908 (Bruneton 2017 default)
        // @deviations NONE

        float r = length(cameraSkyKm);
        float mu = dot(normalize(cameraSkyKm), rayDir);          // view zenith cosine
        float mu_s = dot(normalize(cameraSkyKm), sunDir);        // sun zenith cosine
        float nu = cosTheta;                                      // dot(viewDir, sunDir)
        bool rayHitsGround = RayIntersectsGround(r, mu, bottomRadiusKm);

        // @ref Bruneton 2017 — minimum sun zenith cosine for the scattering LUT.
        // cos(102 deg) allows precomputed twilight scattering below the geometric horizon.
        // Must match the value used during precomputation (atmosphere_renderer.cpp).
        static const float MU_S_MIN = -0.2079116908f;

        Texture3D<float4> scatLut = ResourceDescriptorHeap[skyIndices.scatteringLut3DIdx];

        // Sample the 4D scattering LUT via 3D texture with manual nu interpolation.
        // SampleScatteringTexture4 handles the nu-interleaved packing and lerps between
        // two adjacent nu slices, returning float4(Rayleigh.rgb, Mie.r).
        float4 scatSample = SampleScatteringTexture4(
            scatLut, linearSampler,
            r, mu, mu_s, nu, rayHitsGround,
            bottomRadiusKm, topRadiusKm, MU_S_MIN);

        float3 rayleigh = scatSample.rgb;

        // Extrapolate full-spectrum Mie from the red channel using the Rayleigh ratio.
        // @ref Bruneton 2017 functions.glsl — GetScattering combined Rayleigh+Mie lookup:
        //   mie = rayleigh * (scattering.a / max(rayleigh.r, 1e-4))
        // The LUT stores only Mie.r to save memory (3D texture is large). Since Mie
        // scattering is nearly achromatic, we reconstruct Mie.gb from the spectral
        // shape of Rayleigh scattering (which IS wavelength-dependent).
        float3 mie;
        if (rayleigh.r > 1e-6f) {
            mie = rayleigh * (scatSample.a / rayleigh.r);
        } else {
            // When Rayleigh.r is near zero (deep twilight, below horizon), the ratio is
            // unreliable. Fall back to using the raw Mie.r for all channels.
            mie = float3(scatSample.a, scatSample.a, scatSample.a);
        }

        // Apply phase functions at lookup time and artistic scattering scales.
        // The LUT stores scattering coefficients WITHOUT phase functions (Bruneton 2017
        // precomputes beta * density integrated along the path, weighted by sun transmittance,
        // but NOT phase-weighted). Phase functions are applied per-pixel for correct
        // angular distribution. Artistic scale factors (rayleighScatteringScale,
        // mieScatteringScale) multiply the respective scattering terms, decoupling
        // "how much color" from extinction — same as the ray march path.
        L = rayleigh * phaseR * rayleighScatteringScale
          + mie * phaseM * mieScatteringScale;

        // Apply sun illuminance. The LUT stores scattering already multiplied by sun
        // transmittance along the path, but NOT by the sun's spectral illuminance.
        L *= sunIlluminance.xyz;

        // Multi-scattering contribution for the LUT path.
        // Multi-scattering adds isotropic diffuse light from higher-order bounces.
        // For the LUT path, we need to add multi-scattered luminance integrated along
        // the view ray. This requires either:
        //   (a) A separate multi-scattering 3D LUT (Bruneton precomputes this), or
        //   (b) A single-sample approximation at the camera position.
        // For now, we use approach (b): sample the 2D multi-scattering LUT at the
        // camera position and apply it as a uniform ambient contribution weighted by
        // the path's optical depth (1 - Throughput).
        if (multiScatteringFactor > 0.0f) {
            Texture2D<float4> msLut = ResourceDescriptorHeap[skyIndices.multiScatteringLutIdx];
            float3 ms = SampleMultiScatteringLut(msLut, linearSampler,
                cameraSkyKm, sunDir, bottomRadiusKm, topRadiusKm);
            // The multi-scattering contribution is integrated along the view ray.
            // As an approximation, weight by the total scattering optical depth
            // (how much single-scattering occurred along the path — proportional to
            // how much multi-scattering should contribute).
            // The LUT single-scatter magnitude serves as a proxy for optical depth.
            float3 scatterMag = rayleigh + mie;
            L += ms * scatterMag * sunIlluminance.xyz * multiScatteringFactor;
        }

        // Compute camera-to-endpoint transmittance from the transmittance LUT.
        // For sky rays: transmittance from camera to the top of the atmosphere.
        // For ground rays: transmittance from camera to the ground hit point.
        Texture2D<float4> tLut_cam = ResourceDescriptorHeap[skyIndices.transmittanceLutIdx];
        if (rayHitsGround) {
            // Transmittance from camera to ground point along the view ray.
            float d_ground = DistanceToBottomAtmosphereBoundary(r, mu, bottomRadiusKm);
            Throughput = GetTransmittance(
                tLut_cam, linearSampler,
                r, mu, d_ground, true,
                bottomRadiusKm, topRadiusKm);
        } else {
            // Transmittance from camera to the atmosphere top boundary.
            Throughput = GetTransmittanceToTopAtmosphereBoundary(
                tLut_cam, linearSampler, r, mu, bottomRadiusKm, topRadiusKm);
        }

    } else
#endif // UZ_BINDLESS
    {
        // FALLBACK_RAYMARCH: kept for SM5/non-LUT debugging
        // ── FALLBACK: 32-step ray march (SM5 or LUT not yet precomputed) ─
        // Frostbite analytical per-step integration (Bruneton atmosphere pattern).
        // For each step we compute the analytical integral of scattered luminance
        // over the step interval, weighting by the current path transmittance.
        //
        //   Sint_R = (scatter_R - scatter_R * T_step) / max(ext, eps)
        //   Sint_M = (scatter_M - scatter_M * T_step) / max(ext, eps)
        //   L     += Throughput * sunTransmittance * (Sint_R * phaseR + Sint_M * phaseM)
        //   Throughput *= T_step
        //
        // This avoids float underflow that occurs when using the naive
        // accumulation: totalR += dens * exp(-tau_total) * dt
        // (which approaches 0 due to large tau_total at horizon)

        for (int i = 0; i < NUM_STEPS_VIEW; ++i) {
            float  t        = tStart + (float(i) + 0.5f) * stepLenKm;
            float3 sampleKm = cameraSkyKm + rayDir * t;
            float  altKm    = max(0.0f, length(sampleKm) - bottomRadiusKm);

            float densR = DensityAtAltitude(altKm, scaleHR);
            float densM = DensityAtAltitude(altKm, scaleHM);

            // @ref SampleAtmosphereMediumRGB
            // Extinction uses PHYSICAL coefficients — must match transmittance LUT.
            // Rayleigh extinction = betaR_phys (scattering IS extinction for Rayleigh).
            // Mie extinction = betaM_phys * 1.11 (absorption ratio included).
            // Ozone extinction = ozoneAbsorption * ozone tent density.
            float3 extR      = betaR_phys * densR;
            float3 extM      = betaM_phys * 1.11f * densM;
            float  densO     = OzoneDensityAtAltitude(altKm);
            float3 extO      = ozoneAbsorptionPacked.xyz * densO;
            float3 extTotal  = extR + extM + extO;

            // Transmittance over this single step
            float3 T_step = exp(-extTotal * stepLenKm);

            // Transmittance from sample point toward sun (LUT lookup replaces 8-step ray march)
#ifdef UZ_BINDLESS
            Texture2D<float4> tLut = ResourceDescriptorHeap[skyIndices.transmittanceLutIdx];
            float3 sunT = SampleTransmittanceLut(tLut, linearSampler,
                sampleKm, sunDir, bottomRadiusKm, topRadiusKm);
#else
            float3 sunT = SampleTransmittanceLut(transmittanceLut, linearSampler,
                sampleKm, sunDir, bottomRadiusKm, topRadiusKm);
#endif

            // ── Multi-scattering contribution ───────────────────────────
            // @ref GetMultipleScattering + S integration
            // Multi-scattering is NOT phase-weighted (isotropic) and NOT affected by
            // PlanetShadow or TransmittanceToLight. It adds diffuse light after 1st scatter.
            // Total scattering = Rayleigh + Mie (physical, not artistic, for energy conservation).
            float3 multiScatteredLum = float3(0, 0, 0);
            if (multiScatteringFactor > 0.0f) {
#ifdef UZ_BINDLESS
                Texture2D<float4> msLut = ResourceDescriptorHeap[skyIndices.multiScatteringLutIdx];
                multiScatteredLum = SampleMultiScatteringLut(msLut, linearSampler,
                    sampleKm, sunDir, bottomRadiusKm, topRadiusKm);
#else
                multiScatteredLum = SampleMultiScatteringLut(multiScatteringLut, linearSampler,
                    sampleKm, sunDir, bottomRadiusKm, topRadiusKm);
#endif
            }

            // In-scattering uses ARTISTIC coefficients — controlled by scale params.
            // This decouples "how much color" (scattering) from "how opaque" (extinction).
            float3 scatterR = betaR_art * densR * phaseR;
            float3 scatterM = betaM_art * densM * phaseM;

            // Physical total scattering (for multi-scattering weighting)
            float3 totalScatter = betaR_phys * densR + betaM_phys * densM;

            // @ref implementation detail
            // S = sunIlluminance * (sunTransmittance * phaseScattering + multiScatteredLum * totalScattering)
            float3 singleScatter = sunT * (scatterR + scatterM);
            float3 multiScatter  = multiScatteredLum * totalScatter;
            float3 scatter = singleScatter + multiScatter;

            float3 Sint = (scatter - scatter * T_step) / max(extTotal, EXT_EPS);

            // Accumulate luminance weighted by path throughput
            L         += Throughput * Sint * sunIlluminance.xyz;
            Throughput *= T_step;
        }
    } // end ray march fallback

    // ── Sky luminance art direction ──────────────────────────────────────────
    // Per-channel multiplier for artistic control of sky brightness and tint.
    // Default (1,1,1) = physically correct. Can be used to warm/cool the sky.
    L *= skyLuminanceFactor.xyz;

    // ── Ground contribution (orbital/aerial view only) ─────────────────────
    // @ref Bruneton & Neyret 2017 "Precomputed Atmospheric Scattering"
    //      Section 5 — Runtime rendering, GetSkyRadianceToPoint ground term.
    // At ground level (< 10 km), the ray march runs through the full atmosphere
    // without ground clipping, producing correct Rayleigh inscattering for
    // below-horizon pixels. No ground contribution needed.
    // At orbital altitude (> 10 km), add Lambertian planet surface reflection.
    if (groundHit.x > 0.0f && cameraAltForClip > 10.0f) {
        float3 groundPosKm  = cameraSkyKm + rayDir * groundHit.x;
        float3 groundNormal = normalize(groundPosKm);
        float  r_ground     = length(groundPosKm);
        float  mu_s_ground  = dot(groundNormal, sunDir);

#ifdef UZ_BINDLESS
        if (skyIndices.irradianceLutIdx != 0xFFFFFFFF) {
            // ── Precomputed irradiance LUT path (direct + indirect sky light) ──
            // @ref Bruneton 2017 functions.glsl — GetSkyRadianceToPoint
            //      Ground irradiance = (direct sun transmittance * max(mu_s, 0)
            //                          + indirect sky irradiance from all scattering orders).
            // The irradiance LUT stores values normalized to sun irradiance = 1;
            // multiply by sunIlluminance at runtime.
            // @constants irradiance LUT: 64x16 RGBA16F, linear (r, mu_s) parameterization
            // @deviations NONE
            Texture2D<float4> irrLut = ResourceDescriptorHeap[skyIndices.irradianceLutIdx];
            float2 irrUv = GetIrradianceTextureUvFromRMuS(
                r_ground, mu_s_ground, bottomRadiusKm, topRadiusKm);
            float3 groundIrr = irrLut.SampleLevel(linearSampler, irrUv, 0).rgb;

            // Ground radiance: Lambertian BRDF (albedo / PI) * total irradiance * sun color.
            float3 groundRadiance = groundAlbedo.xyz * (1.0f / PI)
                                  * groundIrr * sunIlluminance.xyz;
            L += Throughput * groundRadiance;
        } else
#endif
        {
            // ── Fallback: direct sunlight only (no irradiance LUT available) ──
            float3 groundSunT;
#ifdef UZ_BINDLESS
            Texture2D<float4> tLutGnd = ResourceDescriptorHeap[skyIndices.transmittanceLutIdx];
            groundSunT = SampleTransmittanceLut(tLutGnd, linearSampler,
                groundPosKm, sunDir, bottomRadiusKm, topRadiusKm);
#else
            groundSunT = SampleTransmittanceLut(transmittanceLut, linearSampler,
                groundPosKm, sunDir, bottomRadiusKm, topRadiusKm);
#endif
            float NdotL = saturate(dot(groundNormal, sunDir));
            float3 groundRadiance = groundAlbedo.xyz * (1.0f / PI) * NdotL
                                  * sunIlluminance.xyz * groundSunT;
            L += Throughput * groundRadiance;
        }
    }

    // ── Sun disk ─────────────────────────────────────────────────────────────
    float sunCosAngle = cos(sunIlluminance.w);
    if (cosTheta > sunCosAngle) {
        float sunEdge = smoothstep(sunCosAngle, sunCosAngle + 0.0002f, cosTheta);
        L += sunIlluminance.xyz * sunDiskIntensity * sunEdge * Throughput;
    }

    // Output linear HDR — tonemap applied in separate fullscreen pass.
    return float4(L, 1.0f);
}
