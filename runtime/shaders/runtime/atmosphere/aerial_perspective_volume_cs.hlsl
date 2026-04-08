/// UzEngine Aerial Perspective Volume — compute shader.
///
/// Fills a 32x32x16 RGBA16F 3D texture with per-voxel inscattering (RGB)
/// and mean transmittance (A) for the camera's aerial perspective.
///
/// @ref Hillaire 2020 "A Scalable and Production Ready Sky and Atmosphere
///      Rendering Technique" — Section 5.3 Aerial Perspective
/// @ref Bruneton & Neyret 2017 "Precomputed Atmospheric Scattering"
///
/// Dispatch: [4, 4, 16] thread groups of [8, 8, 1] = 32x32x16 total threads.

#include "../common/constants.hlsli"

// ─── Root constants at b0 — atmosphere parameters (8 DWORDs) ───────────────
cbuffer AtmosphereParams : register(b0) {
    float rayleighBetaX;     ///< Rayleigh scattering R (km^-1).
    float rayleighBetaY;     ///< Rayleigh scattering G (km^-1).
    float rayleighBetaZ;     ///< Rayleigh scattering B (km^-1).
    float rayleighScaleH;    ///< Rayleigh scale height (km).
    float mieBeta;           ///< Mie scattering coefficient (km^-1).
    float mieScaleH;         ///< Mie scale height (km).
    float bottomRadiusKm;    ///< Planet surface radius (km).
    float topRadiusKm;       ///< Top of atmosphere radius (km).
};

// ─── APVolumeCB at b2 — per-dispatch parameters (256 bytes) ────────────────
cbuffer APVolumeCB : register(b2) {
    row_major float4x4 invViewProj;
    float4 cameraPosWorld;   ///< Camera world position (cm), w unused.
    float4 sunDirIntensity;  ///< Sun direction (xyz), intensity (w).
    float  mieAnisotropy;
    float  rayleighScale;    ///< Artistic Rayleigh multiplier.
    float  mieScale;         ///< Artistic Mie multiplier.
    uint   transmittanceLutIdx;
    uint   outputUavIdx;
    uint   depthSlices;
    float  totalDepthKm;
    uint   multiScatterLutIdx;
    uint   scatteringLut3DIdx;
    float  muSMin;
};

SamplerState linearSampler : register(s0);

// ─── Constants ─────────────────────────────────────────────────────────────

/// @ref Bruneton 2017 — Mie extinction/scattering ratio (1.11 for Earth)
/// @constants MieExtinctionScale = 1.11
static const float kMieExtinctionScale = 1.11f;

/// Number of ray-march steps per voxel slice.
/// @ref Hillaire 2020 — 8 steps for AP volume is sufficient
/// @constants NUM_AP_STEPS = 8
static const int NUM_AP_STEPS = 8;

// ─── Atmosphere helpers (shared with sky_atmosphere_ps.hlsl) ───────────────

/// @ref Bruneton 2017 — transmittance LUT UV parameterization
void TransmittanceLutParamsToUv(float viewHeight, float viewZenithCos,
    float bR, float tR, out float2 uv)
{
    float H   = sqrt(max(0.0, tR * tR - bR * bR));
    float rho = sqrt(max(0.0, viewHeight * viewHeight - bR * bR));
    float disc = viewHeight * viewHeight * (viewZenithCos * viewZenithCos - 1.0)
               + tR * tR;
    float d = max(0.0, -viewHeight * viewZenithCos + sqrt(max(0.0, disc)));
    uv = float2(
        (d - (tR - viewHeight)) / (rho + H - (tR - viewHeight) + 1e-6),
        rho / (H + 1e-6)
    );
    uv = saturate(uv);
}

/// Sample the precomputed transmittance LUT for sun → sample point.
float3 SampleTransmittanceLut(float3 samplePos, float3 queryDir,
    float bR, float tR)
{
    float h = length(samplePos);
    float cosZ = dot(normalize(samplePos), queryDir);
    float2 uv;
    TransmittanceLutParamsToUv(h, cosZ, bR, tR, uv);
    if (transmittanceLutIdx != kSentinelSrvIndex) {
        Texture2D<float4> tLut = ResourceDescriptorHeap[transmittanceLutIdx];
        return tLut.SampleLevel(linearSampler, uv, 0).rgb;
    }
    return float3(1, 1, 1);
}

/// Sample transmittance between two points using the LUT.
/// @ref Bruneton 2017 — T(a→b) = T(origin→b) / T(origin→a)
float3 TransmittanceBetween(float3 posA, float3 posB, float3 dir,
    float bR, float tR)
{
    float3 Ta = SampleTransmittanceLut(posA, dir, bR, tR);
    float3 Tb = SampleTransmittanceLut(posB, dir, bR, tR);
    return max(Tb, 1e-6) / max(Ta, 1e-6);
}

/// @ref Cornette-Shanks 1992 — Rayleigh phase function
/// @constants Rayleigh coefficient = 3 / (16 * PI)
float RayleighPhase(float cosTheta)
{
    return (3.0 / (16.0 * PI)) * (1.0 + cosTheta * cosTheta);
}

/// @ref Henyey-Greenstein 1941 — Mie phase function
/// @constants HG normalization = 1 / (4 * PI)
float MiePhaseHG(float cosTheta, float g)
{
    float g2 = g * g;
    float denom = 1.0 + g2 - 2.0 * g * cosTheta;
    return (1.0 / (4.0 * PI)) * (1.0 - g2) / (denom * sqrt(denom));
}

/// Ray-sphere intersection (centered at origin).
/// Returns (tNear, tFar). Both negative = no hit.
float2 RaySphereIntersect(float3 origin, float3 dir, float radius)
{
    float b = dot(origin, dir);
    float c = dot(origin, origin) - radius * radius;
    float disc = b * b - c;
    if (disc < 0.0) return float2(-1, -1);
    float sq = sqrt(disc);
    return float2(-b - sq, -b + sq);
}

// ─── Depth slicing — exponential distribution ──────────────────────────────

/// @ref Hillaire 2020 — exponential depth distribution for AP volume
/// More slices near camera where AP changes fastest.
/// @constants depthExponent = 2.0 (quadratic bias toward near)
float SliceToDepthKm(uint slice, uint totalSlices, float totalDepth)
{
    float t = float(slice + 0.5) / float(totalSlices);
    return totalDepth * t * t;
}

// ─── Main compute ──────────────────────────────────────────────────────────

[numthreads(8, 8, 1)]
void main(uint3 DTid : SV_DispatchThreadID)
{
    uint3 volumeSize = uint3(32, 32, depthSlices);
    if (any(DTid >= volumeSize)) return;

    // ── Screen UV from voxel XY ───────────────────────────────────────────
    float2 uv = (float2(DTid.xy) + 0.5) / float2(volumeSize.xy);

    // ── Reconstruct view ray from UV (2-point unproject for stability) ────
    float2 ndc = uv * 2.0 - 1.0;
    ndc.y = -ndc.y;
    float4 pt0 = mul(float4(ndc, 0.25, 1.0), invViewProj);
    float4 pt1 = mul(float4(ndc, 0.75, 1.0), invViewProj);
    float3 rayDir = normalize(pt0.xyz / pt0.w - pt1.xyz / pt1.w);

    // ── Camera in atmosphere space (km, planet center at origin) ──────────
    float3 cameraSkyKm = cameraPosWorld.xyz * kKmPerCm
                       + float3(0, 0, bottomRadiusKm);

    // Snap camera above surface (5m minimum altitude)
    float distToCenter = length(cameraSkyKm);
    float minSafe = bottomRadiusKm + 0.005;
    if (distToCenter < minSafe)
        cameraSkyKm = cameraSkyKm * (minSafe / distToCenter);

    // ── Depth range for this slice ────────────────────────────────────────
    float depthNearKm = (DTid.z > 0)
        ? SliceToDepthKm(DTid.z - 1, depthSlices, totalDepthKm)
        : 0.0;
    float depthFarKm = SliceToDepthKm(DTid.z, depthSlices, totalDepthKm);

    // ── Intersect atmosphere shell ────────────────────────────────────────
    float2 atmoHit = RaySphereIntersect(cameraSkyKm, rayDir, topRadiusKm);
    if (atmoHit.y < 0.0) {
        // Ray misses atmosphere entirely — no AP
        RWTexture3D<float4> outTex = ResourceDescriptorHeap[outputUavIdx];
        outTex[DTid] = float4(0, 0, 0, 1);
        return;
    }

    // Clip ray to atmosphere bounds
    float tStart = max(atmoHit.x, 0.0) + depthNearKm;
    float tEnd   = max(atmoHit.x, 0.0) + depthFarKm;
    tEnd = min(tEnd, atmoHit.y);

    // Clip at planet surface
    float2 groundHit = RaySphereIntersect(cameraSkyKm, rayDir, bottomRadiusKm);
    if (groundHit.x > 0.0) {
        tEnd = min(tEnd, groundHit.x);
    }

    if (tEnd <= tStart) {
        RWTexture3D<float4> outTex = ResourceDescriptorHeap[outputUavIdx];
        outTex[DTid] = float4(0, 0, 0, 1);
        return;
    }

    // ── Scattering parameters ─────────────────────────────────────────────
    float3 sunDir   = normalize(sunDirIntensity.xyz);
    float  sunI     = sunDirIntensity.w;
    float  cosTheta = dot(rayDir, sunDir);
    float  phaseR   = RayleighPhase(cosTheta);
    float  phaseM   = MiePhaseHG(cosTheta, mieAnisotropy);

    float3 betaR = float3(rayleighBetaX, rayleighBetaY, rayleighBetaZ)
                 * rayleighScale;
    float  betaM = mieBeta * mieScale;

    // ── Ray march — accumulate inscattering and transmittance ─────────────
    /// @ref Hillaire 2015 "Physically Based & Unified Volumetric Rendering
    ///      in Frostbite" — analytical integration within each step
    float stepLen = (tEnd - tStart) / float(NUM_AP_STEPS);

    float3 inscattering = float3(0, 0, 0);
    float3 throughput   = float3(1, 1, 1);

    for (int i = 0; i < NUM_AP_STEPS; ++i) {
        float t = tStart + (float(i) + 0.5) * stepLen;
        float3 sampleKm = cameraSkyKm + rayDir * t;
        float altKm = max(0.0, length(sampleKm) - bottomRadiusKm);

        // Density at sample point
        float densR = exp(-altKm / rayleighScaleH);
        float densM = exp(-altKm / mieScaleH);

        // Extinction
        float3 extR = float3(rayleighBetaX, rayleighBetaY, rayleighBetaZ) * densR;
        float3 extM = mieBeta * kMieExtinctionScale * densM;
        float3 ext  = extR + extM;

        // Step transmittance
        float3 T_step = exp(-ext * stepLen);

        // Sun transmittance from precomputed LUT
        float3 sunT = SampleTransmittanceLut(sampleKm, sunDir,
                                              bottomRadiusKm, topRadiusKm);

        // In-scattering (Rayleigh + Mie, phase-weighted)
        float3 scatterR = betaR * densR * phaseR;
        float3 scatterM = betaM * densM * phaseM;
        float3 scatter = sunT * (scatterR + scatterM);

        // Multi-scattering contribution (isotropic)
        if (multiScatterLutIdx != 0 && multiScatterLutIdx != kSentinelSrvIndex) {
            float h = length(sampleKm);
            float cosS = dot(normalize(sampleKm), sunDir);
            // Multi-scatter LUT: U = (cosS*0.5+0.5), V = (h-bR)/(tR-bR)
            float2 msUv = float2(cosS * 0.5 + 0.5,
                                  saturate((h - bottomRadiusKm) / (topRadiusKm - bottomRadiusKm)));
            Texture2D<float4> msLut = ResourceDescriptorHeap[multiScatterLutIdx];
            float3 ms = msLut.SampleLevel(linearSampler, msUv, 0).rgb;
            // Multi-scatter adds isotropic light (no phase function)
            float3 msScatter = ms * (betaR * densR + betaM * densM);
            scatter += msScatter;
        }

        // Frostbite analytical integration within each step
        // @ref Hillaire 2015 — S_int = (S - S * T_step) / ext
        float3 Sint = (scatter - scatter * T_step) / max(ext, 1e-6);
        inscattering += throughput * Sint * sunI;
        throughput *= T_step;
    }

    // Mean transmittance (single scalar for alpha channel)
    // @ref Hillaire 2020 — use luminance-weighted mean for perceptual correctness
    float meanTransmittance = dot(throughput, float3(0.2126, 0.7152, 0.0722));

    // ── Write output ──────────────────────────────────────────────────────
    RWTexture3D<float4> outTex = ResourceDescriptorHeap[outputUavIdx];
    outTex[DTid] = float4(max(inscattering, 0.0), saturate(meanTransmittance));
}
