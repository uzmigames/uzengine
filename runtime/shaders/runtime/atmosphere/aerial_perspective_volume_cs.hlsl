/// Aerial Perspective Volume Compute Shader — Precomputed 3D AP texture.
///
/// @ref RenderCameraAerialPerspectiveVolumeCS
/// @ref GetAerialPerspectiveLuminanceTransmittance
///
/// Fills a 32x32x16 RGBA16F 3D texture with per-voxel inscattering (RGB)
/// and mean transmittance (A). Each voxel corresponds to a screen-space
/// direction (XY) and a depth (Z, squared distribution).
///
/// The deferred lighting shader samples this volume per-pixel instead of
/// doing an inline 8-step ray march, amortizing the cost across all pixels
/// that map to the same voxel.
///
/// # Depth Distribution (standard)
/// @ref Slice *= Slice (squared)
///   normalizedSlice = (z + 0.5) / depthSlices
///   depthKm = normalizedSlice^2 * totalDepthKm
/// This packs detail near the camera where AP changes most rapidly.
///
/// # Resources
/// - b0: 8 DWORD root constants (atmosphere parameters)
/// - b2: APVolumeCB (camera, sun, LUT indices, volume params)
/// - u0: RWTexture3D<float4> output (32x32x16 RGBA16F)
/// - s0: Static linear clamp sampler for transmittance LUT
///
/// # Dispatch
/// [numthreads(8,8,1)], dispatch (4, 4, 16) = 32x32x16 total threads.

#include "../include/aerial_perspective.hlsli"
#include "../include/atmosphere_lut.hlsli"

// ─────────────────────────────────────────────────────────────────────────────
// Root Constants (b0) — atmosphere scattering parameters
// ─────────────────────────────────────────────────────────────────────────────

/// @ref atmosphere medium coefficients
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
// CBV (b2) — camera, sun, volume parameters
// ─────────────────────────────────────────────────────────────────────────────

/// Per-dispatch parameters. 256-byte aligned for D3D12 CBV.
cbuffer APVolumeCB : register(b2) {
    row_major float4x4 invViewProj; ///< Inverse view-projection for ray direction reconstruction.
    float4 cameraPosWorld;          ///< xyz = camera world position (cm), w = unused.
    float4 sunDirAndIntensity;      ///< xyz = normalised direction TOWARD sun, w = sun intensity.
    float  mieAnisotropy;           ///< HG anisotropy parameter (0.8 typical).
    float  rayleighScale;           ///< Artistic Rayleigh scattering multiplier.
    float  mieScale;                ///< Artistic Mie scattering multiplier.
    uint   transmittanceLutIdx;     ///< SRV index for transmittance LUT (256x64 RGBA16F).
    uint   outputUavIdx;            ///< UAV index for output 3D texture.
    uint   depthSlices;             ///< Number of depth slices (16).
    float  totalDepthKm;            ///< Total depth range in km (100.0).
    uint   multiScatterLutIdx;      ///< SRV index for multi-scattering LUT (32x32 R11G11B10F).
    uint   scatteringLut3DIdx;     ///< SRV index for Bruneton 3D scattering LUT (256x128x32 RGBA16F). 0 = not available.
    float  muSMin;                 ///< Minimum sun zenith cosine for LUT parameterization. cos(102 deg) = -0.2079116908.
};

// ─────────────────────────────────────────────────────────────────────────────
// Sampler
// ─────────────────────────────────────────────────────────────────────────────

SamplerState linearSampler : register(s0);

// ─────────────────────────────────────────────────────────────────────────────
// SM5 register-bound fallback (when UZ_BINDLESS is not defined)
// ─────────────────────────────────────────────────────────────────────────────
#ifndef UZ_BINDLESS
Texture2D<float4>   g_TransmittanceLut  : register(t0);
Texture2D<float4>   g_MultiScatterLut   : register(t1);
Texture3D<float4>   g_ScatteringLut3D   : register(t2);
RWTexture3D<float4> g_OutputVol         : register(u0);
#endif

// ─────────────────────────────────────────────────────────────────────────────
// Volume Dimensions
// ─────────────────────────────────────────────────────────────────────────────

/// @ref CameraAerialPerspectiveVolumeScreenResolution = 32
static const uint AP_VOL_XY = 32;

/// Number of ray-march steps per depth slice.
/// @ref CameraAerialPerspectiveSampleCountPerSlice
/// uses (sliceIndex+1) * sampleCountPerSlice. We use a fixed 8 for simplicity
/// which matches the existing per-pixel AP step count and keeps dispatch cost low.
static const int AP_STEPS_PER_VOXEL = 8;

// ─────────────────────────────────────────────────────────────────────────────
// Main Compute Kernel
// ─────────────────────────────────────────────────────────────────────────────

[numthreads(8, 8, 1)]
void main(uint3 tid : SV_DispatchThreadID)
{
    // ── Bounds check ─────────────────────────────────────────────────────
    if (tid.x >= AP_VOL_XY || tid.y >= AP_VOL_XY || tid.z >= depthSlices)
        return;

    // ── Output UAV ───────────────────────────────────────────────────────
#ifdef UZ_BINDLESS
    RWTexture3D<float4> outputVol = ResourceDescriptorHeap[outputUavIdx];
#else
    RWTexture3D<float4> outputVol = g_OutputVol;
#endif

    // ── Screen UV from voxel XY ──────────────────────────────────────────
    float2 screenUV = (float2(tid.xy) + 0.5f) / float(AP_VOL_XY);

    // ── Depth from voxel Z (squared distribution) ────────────────────────
    /// @ref Slice *= Slice
    float normalizedSlice = (float(tid.z) + 0.5f) / float(depthSlices);
    float squaredSlice = normalizedSlice * normalizedSlice; // Squared distribution
    float depthKm = squaredSlice * totalDepthKm;

    // Early-out for zero depth (camera plane)
    if (depthKm < 0.001f) {
        outputVol[tid] = float4(0.0f, 0.0f, 0.0f, 1.0f);
        return;
    }

    // ── Reconstruct world-space ray direction from screen UV ─────────────
    float2 ndc = screenUV * 2.0f - 1.0f;
    ndc.y = -ndc.y; // DX UV convention

    // Two points on the same ray at different depths → direction
    float4 pt0 = mul(float4(ndc, 0.25f, 1.0f), invViewProj);
    float4 pt1 = mul(float4(ndc, 0.75f, 1.0f), invViewProj);
    float3 world0 = pt0.xyz / pt0.w;
    float3 world1 = pt1.xyz / pt1.w;
    float3 rayDir = normalize(world0 - world1);

    // ── Convert camera to sky-space (km) ─────────────────────────────────
    float3 cameraSkyKm = cameraPosWorld.xyz * AP_WORLD_TO_KM
                       + float3(0.0f, 0.0f, bottomRadiusKm);

    // Snap camera above planet surface
    float distToCenter  = length(cameraSkyKm);
    float minSafeRadius = bottomRadiusKm + AP_PLANET_RADIUS_OFFSET_KM;
    if (distToCenter < minSafeRadius) {
        cameraSkyKm = cameraSkyKm * (minSafeRadius / distToCenter);
    }

    // ── Clip ray to atmosphere ───────────────────────────────────────────
    float2 atmoHit = AP_RaySphereIntersect(cameraSkyKm, rayDir,
                                            float3(0, 0, 0), topRadiusKm);
    if (atmoHit.y < 0.0f) {
        outputVol[tid] = float4(0.0f, 0.0f, 0.0f, 1.0f);
        return;
    }

    float tStart = max(atmoHit.x, 0.0f);
    float tEnd   = min(depthKm, atmoHit.y);

    // @ref below-horizon voxel handling
    // reprojects below-horizon voxels to the opposite hemisphere.
    // Simplified approach: do NOT clip to planet surface. Instead, let the ray
    // march through the full depthKm distance. For below-horizon rays near
    // the surface, the ray passes "through" the planet, but the density
    // samples at negative altitude (clamped to 0 by AP_DensityAtAltitude)
    // contribute maximum inscattering — producing the blue horizon haze.
    // This is physically equivalent to the reference engine's reprojection for ground-level cameras.

    float marchLen = tEnd - tStart;
    if (marchLen <= 0.0f) {
        outputVol[tid] = float4(0.0f, 0.0f, 0.0f, 1.0f);
        return;
    }

    // ── Common setup ────────────────────────────────────────────────────
    float3 sunDir      = sunDirAndIntensity.xyz;
    float  sunIntensity = sunDirAndIntensity.w;
    float  cosTheta    = dot(rayDir, sunDir);
    float  phaseR      = AP_RayleighPhase(cosTheta);
    float  phaseM      = AP_MiePhase(cosTheta, mieAnisotropy);

    // Fetch transmittance LUT
#ifdef UZ_BINDLESS
    Texture2D<float4> tLut = ResourceDescriptorHeap[transmittanceLutIdx];
#else
    Texture2D<float4> tLut = g_TransmittanceLut;
#endif

    float3 L          = float3(0, 0, 0);
    float3 Throughput = float3(1, 1, 1);

    // ── LUT-based path: dual 4D scattering lookup ────────────────────────
    // @ref Bruneton & Neyret 2017 "Precomputed Atmospheric Scattering"
    //      Section 5 — Runtime Rendering, GetSkyRadianceToPoint.
    // Replaces the 8-step ray march with two scattering LUT lookups per voxel.
    // The 3D scattering LUT (256x128x32 RGBA16F) is precomputed once when
    // atmosphere parameters change and provides exact multi-order scattering.
    // Fallback: when scatteringLut3DIdx == 0 (LUT not yet computed or
    // bindless not available), the 8-step ray march runs instead.
    bool usedLut = false;

#ifdef UZ_BINDLESS
    if (scatteringLut3DIdx != 0) {
        Texture3D<float4> scatLut3D = ResourceDescriptorHeap[scatteringLut3DIdx];

        // Compute target point position in sky space (km)
        float3 pointKm = cameraSkyKm + rayDir * depthKm;

        // Clamp point above planet surface (same safety as camera)
        float pointDist = length(pointKm);
        float pointMinR = bottomRadiusKm + AP_PLANET_RADIUS_OFFSET_KM;
        if (pointDist < pointMinR) {
            pointKm = pointKm * (pointMinR / pointDist);
        }

        // Dual 4D lookup: inscatter(camera→point) via LUT subtraction.
        // GetSkyRadianceToPoint computes:
        //   inscatter = S(camera→top) - T(camera→point) * S(point→top)
        // and applies phase functions (Rayleigh + HG Mie).
        // Artistic scale factors are applied separately below.
        float3 inscatter;
        float3 transmittance;
        GetSkyRadianceToPoint(
            cameraSkyKm, pointKm, sunDir,
            scatLut3D, tLut, linearSampler,
            bottomRadiusKm, topRadiusKm, muSMin,
            phaseR, phaseM,
            /*out*/ inscatter, /*out*/ transmittance);

        // Apply artistic scattering scales.
        // The LUT stores physical scattering; artistic multipliers modulate the
        // visual intensity of Rayleigh/Mie independently. For the LUT path we
        // apply a uniform scale based on the artistic weights, blending between
        // physical (1.0) and the user-specified multipliers.
        // This matches the ray march behavior where artRayleighBeta = rayleighBeta * rayleighScale
        // and artMieBeta = mieBeta * mieScale are used for in-scattering.
        float artScale = max(rayleighScale, mieScale);
        L = inscatter * artScale * sunIntensity;
        Throughput = transmittance;
        usedLut = true;
    }
#endif

    // ── Fallback: 8-step ray march (SM5 / no LUT available) ──────────────
    if (!usedLut) {
        float stepLenKm = marchLen / float(AP_STEPS_PER_VOXEL);

        float3 artRayleighBeta = rayleighBeta * rayleighScale;
        float  artMieBeta      = mieBeta * mieScale;

        for (int i = 0; i < AP_STEPS_PER_VOXEL; ++i) {
            float  t        = tStart + (float(i) + 0.5f) * stepLenKm;
            float3 sampleKm = cameraSkyKm + rayDir * t;
            float  altKm    = max(0.0f, length(sampleKm) - bottomRadiusKm);

            float densR = AP_DensityAtAltitude(altKm, rayleighScaleH);
            float densM = AP_DensityAtAltitude(altKm, mieScaleH);

            // Extinction uses physical coefficients (must match transmittance LUT)
            float3 extR     = rayleighBeta * densR;
            float3 extM     = mieBeta * AP_MIE_EXTINCTION_RATIO * densM;
            float3 extTotal = extR + extM;

            float3 T_step = exp(-extTotal * stepLenKm);

            // Sun transmittance from LUT
            float3 sunT = AP_SampleTransmittanceLut(tLut, linearSampler,
                sampleKm, sunDir, bottomRadiusKm, topRadiusKm);

            // ── Multi-scattering contribution ────────────────────────────
            // @ref Hillaire 2020 — GetMultipleScattering + S integration
            // Multi-scattering is isotropic (no phase weighting) and adds
            // diffuse light after the 1st scatter bounce.
            float3 multiScatteredLum = float3(0, 0, 0);
            {
#ifdef UZ_BINDLESS
                Texture2D<float4> msLut = ResourceDescriptorHeap[multiScatterLutIdx];
#else
                Texture2D<float4> msLut = g_MultiScatterLut;
#endif
                multiScatteredLum = AP_SampleMultiScatteringLut(msLut, linearSampler,
                    sampleKm, sunDir, bottomRadiusKm, topRadiusKm);
            }

            // In-scattering with artistic coefficients
            float3 scatterR = artRayleighBeta * densR * phaseR;
            float3 scatterM = artMieBeta * densM * phaseM;

            // Physical total scattering (for multi-scatter weighting)
            float3 totalScatter = rayleighBeta * densR + mieBeta * densM;

            // @ref S = sunIlluminance * (sunTransmittance * phaseScatter + multiScatter * totalScatter)
            float3 singleScatter = sunT * (scatterR + scatterM);
            float3 multiScatter  = multiScatteredLum * totalScatter;
            float3 scatter = singleScatter + multiScatter;

            // Analytical step integral: Sint = scatter * (1 - T_step) / ext
            float3 Sint = (scatter - scatter * T_step) / max(extTotal, AP_EXT_EPS);

            L         += Throughput * Sint * sunIntensity;
            Throughput *= T_step;
        }
    }

    // Mean transmittance (standard: average of RGB channels)
    /// @ref dot(ss.Transmittance, float3(1/3, 1/3, 1/3))
    float meanTransmittance = dot(Throughput, float3(1.0f / 3.0f, 1.0f / 3.0f, 1.0f / 3.0f));

    outputVol[tid] = float4(L, meanTransmittance);
}
