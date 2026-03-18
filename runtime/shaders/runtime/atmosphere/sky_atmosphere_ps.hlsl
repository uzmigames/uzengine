/// Sky atmosphere pixel shader — Hillaire 2020 single-scatter approximation.
///
/// Follows UE5 SkyAtmosphere coordinate conventions:
/// - All distances in KILOMETERS (1 world unit = 1 centimeter = 0.00001 km, UE5 parity)
/// - Planet center at (0, 0, 0) in sky-space; camera offset by bottomRadiusKm
/// - Camera snapped to minimum 5m above planet surface (prevents near-surface artifacts)
/// - Light direction points TOWARD the sun (PerFrameCB.lightDir = toward-sun direction)
/// - Scattering coefficients in km^-1
///
/// # Integration Method
/// Uses Frostbite analytical per-step integration (same as UE5 SkyAtmosphere.usf):
///   Sint = (scatter - scatter * exp(-ext * dt)) / max(ext, eps)
///   L   += Throughput * Sint
///   T   *= exp(-ext * dt)
/// This avoids float underflow from accumulated exp(-tau) that zeroed out scatter.
///
/// # Inputs
/// - PerFrameCB at b1: camera, invViewProj, lightDir, lightColor
/// - SkyAtmosphereCB at b2: atmosphere parameters (all distances in km)
/// - Depth texture (t0): to skip pixels with geometry
/// - Transmittance LUT (t1 / bindless): precomputed 256x64 RGBA16F texture
///   encoding sun transmittance per (height, zenith angle)
///
/// # Output
/// Linear HDR color tonemapped via ACES and gamma-corrected to sRGB.

#include "../include/pbr_common.hlsli"

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
    uint _pad1;
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
    // ── Ozone absorption (UE5 "Other Absorption") ─────────────────────────
    // @source UE5 SkyAtmosphereCommon.ush:316-318 — SampleAtmosphereMediumRGB
    // @source UE5 SkyAtmosphereCommonData.h:38-42 — AbsorptionDensity tent coefficients
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
// Constants — UE5 convention
// ─────────────────────────────────────────────────────────────────────────────

/// 1 world unit (centimeter) → km. UE5 parity: 1 unit = 1 cm.
/// CmToKm = 0.00001 (same as UE5 CmToSkyUnit).
static const float WORLD_TO_KM            = 0.00001f;

/// Safety margin: always place camera at least 5m above ground in sky space.
/// UE5 uses PlanetRadiusOffset = 0.005 km (5 meters).
static const float PLANET_RADIUS_OFFSET_KM = 0.005f;

/// Small epsilon to avoid division by zero in analytical integration.
static const float EXT_EPS = 1e-7f;

/// @source UE5 SkyAtmosphere.usf:1039 — SkyAtmosphere.SampleCountMax (CB parameter)
/// UE5 uses variable sample count per ray (lerp between Min/Max based on ray length).
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

/// Ozone density at a given altitude using the UE5 tent distribution.
/// @source UE5 SkyAtmosphereCommon.ush:316-318 — SampleAtmosphereMediumRGB
/// @constants ozoneLayerWidth=25.0 (SkyAtmosphereCommonData.cpp:38)
/// @deviations NONE
float OzoneDensityAtAltitude(float altKm) {
    return saturate(altKm < ozoneAbsorptionPacked.w
        ? ozoneLinTerm0 * altKm + ozoneConstTerm0
        : ozoneLinTerm1 * altKm + ozoneConstTerm1);
}

/// Samples the precomputed multi-scattering LUT.
/// @source UE5 SkyAtmosphere.usf:337-343 — GetMultipleScattering
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
    /// @source UE5 Common.ush — TranslatedWorldToClip (camera-relative rendering)
    /// @deviations Near/far clip subtraction used instead of UE5's TranslatedWorld
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

    // ── Snap camera to minimum 5m above planet surface (UE5 pattern) ─────────
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

    // Clip to planet surface if ray points downward
    float2 groundHit = RaySphereIntersect(cameraSkyKm, rayDir, float3(0, 0, 0), bottomRadiusKm);
    if (groundHit.x > 0.0f)
        tEnd = min(tEnd, groundHit.x);

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
    // extinction (optical depth). This is how UE5 implements its artistic
    // scale parameters: they affect "how much color" the sky has, not
    // "how opaque" the atmosphere is.
    float3 betaR_art = rayleighScattering.xyz * rayleighScatteringScale;
    float  betaM_art = mieScattering.x * mieScatteringScale;

    float  scaleHR = rayleighScattering.w;   // Rayleigh scale height (km)
    float  scaleHM = mieScattering.z;        // Mie scale height (km)

    // ── Frostbite analytical integration (UE5 SkyAtmosphere.usf pattern) ────
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
    float3 L          = float3(0, 0, 0);
    float3 Throughput = float3(1, 1, 1);

    for (int i = 0; i < NUM_STEPS_VIEW; ++i) {
        float  t        = tStart + (float(i) + 0.5f) * stepLenKm;
        float3 sampleKm = cameraSkyKm + rayDir * t;
        float  altKm    = max(0.0f, length(sampleKm) - bottomRadiusKm);

        float densR = DensityAtAltitude(altKm, scaleHR);
        float densM = DensityAtAltitude(altKm, scaleHM);

        // @source UE5 SkyAtmosphereCommon.ush:310-336 — SampleAtmosphereMediumRGB
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
        // @source UE5 SkyAtmosphere.usf:651-655,697 — GetMultipleScattering + S integration
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

        // @source UE5 SkyAtmosphere.usf:697
        // S = sunIlluminance * (sunTransmittance * phaseScattering + multiScatteredLum * totalScattering)
        float3 singleScatter = sunT * (scatterR + scatterM);
        float3 multiScatter  = multiScatteredLum * totalScatter;
        float3 scatter = singleScatter + multiScatter;

        float3 Sint = (scatter - scatter * T_step) / max(extTotal, EXT_EPS);

        // Accumulate luminance weighted by path throughput
        L         += Throughput * Sint * sunIlluminance.xyz;
        Throughput *= T_step;
    }

    // ── Sky luminance art direction ──────────────────────────────────────────
    // Per-channel multiplier for artistic control of sky brightness and tint.
    // Default (1,1,1) = physically correct. Can be used to warm/cool the sky.
    L *= skyLuminanceFactor.xyz;

    // ── Ground contribution ─────────────────────────────────────────────────
    // When the view ray hits the planet surface, add Lambertian reflection of
    // sunlight attenuated by atmosphere transmittance (both sun→ground and
    // ground→camera). This gives the ground its base color when viewed from
    // above or at the horizon. UE5: SkyAtmosphere.usf "PlanetSurfaceTransmittance".
    if (groundHit.x > 0.0f) {
        float3 groundPosKm = cameraSkyKm + rayDir * groundHit.x;
        float3 groundNormal = normalize(groundPosKm);

        // Sun-to-ground transmittance via LUT
        float3 groundSunT;
#ifdef UZ_BINDLESS
        Texture2D<float4> tLutGnd = ResourceDescriptorHeap[skyIndices.transmittanceLutIdx];
        groundSunT = SampleTransmittanceLut(tLutGnd, linearSampler,
            groundPosKm, sunDir, bottomRadiusKm, topRadiusKm);
#else
        groundSunT = SampleTransmittanceLut(transmittanceLut, linearSampler,
            groundPosKm, sunDir, bottomRadiusKm, topRadiusKm);
#endif

        // Lambertian BRDF: albedo / π × max(dot(N, L), 0)
        float NdotL = max(0.0f, dot(groundNormal, sunDir));
        float3 groundRadiance = groundAlbedo.xyz * (1.0f / PI) * NdotL
                              * sunIlluminance.xyz * groundSunT;

        // Attenuate by path transmittance from ground to camera
        L += Throughput * groundRadiance;
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
