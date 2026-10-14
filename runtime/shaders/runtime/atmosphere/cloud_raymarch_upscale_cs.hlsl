#define UZ_CLOUD_TEMPORAL_UPSCALE 1
/// Cloud ray-march compute shader — volumetric cloud rendering.
///
/// @ref Schneider & Vos 2015 "Real-Time Volumetric Cloudscapes of Horizon Zero Dawn"
/// @ref Hillaire 2020 "A Scalable and Production Ready Sky and Atmosphere Rendering Technique"
/// @ref Frostbite analytical integration: (S - S*T) / max(ext, eps)
///
/// # Root Signature Layout (Compute)
/// - b0 (slot 0): 4x uint32 root constants {outputUavIdx, depthSrvIdx, halfWidth, halfHeight}
/// - b2 (slot 2): CloudCB constant buffer (512 bytes)
/// - u0 (slot 4): UAV descriptor table for output write
///
/// # Output
/// Half-resolution RGBA16F texture:
/// - RGB: premultiplied cloud color
/// - A: transmittance (1 = transparent, 0 = opaque)

#include "../common/constants.hlsli"
#include "../common/bindless.hlsli"
#ifdef UZ_CLOUD_TEMPORAL_UPSCALE
#include "cloud_bayer.hlsli"
#endif

// ─────────────────────────────────────────────────────────────────────────────
// Root Constants (b0)
// ─────────────────────────────────────────────────────────────────────────────

cbuffer RootConstants : register(b0) {
    uint  g_outputUavIdx;
    uint  g_depthSrvIdx;
    uint  g_halfWidth;
    uint  g_halfHeight;
    uint4 _rcReserved0;
    uint4 _rcReserved1;
};

// ─────────────────────────────────────────────────────────────────────────────
// Cloud Constant Buffer (b2) — matches CloudCB in cloud_pass.cpp (512 bytes)
// ─────────────────────────────────────────────────────────────────────────────

cbuffer CloudCB : register(b2) {
    row_major float4x4 g_invViewProj;
    float3  g_cameraPos;
    float   g_time;
    float3  g_sunDir;
    float   g_coverage;
    float3  g_sunColor;
    float   g_density;
    float3  g_ambientColor;
    float   g_extinction;
    float   g_scatteringAlbedo;
    float   g_phaseG;
    float   g_phaseG2;
    float   g_phaseBlend;
    float   g_layerBottomKm;
    float   g_layerHeightKm;
    float   g_windVelocityX;          // scalars: a float3 at offset 152 would move to 160 and shift the tail
    float   g_windVelocityY;
    float   g_windVelocityZ;
    float   g_detailWindScale;
    float   g_ambientMultiplier;
    uint    g_primarySteps;
    uint    g_lightSteps;
    uint    g_shapeNoiseSrv;
    float   g_globalCoverage;
    // Extended CloudCB fields (offset 188-252)
    float   g_cloudType;
    float   g_tracingMaxDistanceKm;
    float   g_viewSampleCountScale;
    float   g_reflectionViewSampleCountScale;
    float   g_shadowViewSampleCountScale;
    float   g_shadowReflectionViewSampleCountScale;
    float   g_shadowTracingDistance;
    float   g_stopTracingTransmittanceThreshold;
    float   g_aerialPerspRayleighStartDist;
    float   g_aerialPerspRayleighFadeDist;
    float   g_aerialPerspMieStartDist;
    float   g_aerialPerspMieFadeDist;
    float   g_skyLightCloudBottomOcclusion;
    uint    g_tracingMaxDistanceMode;
    float   g_tracingStartMaxDistance;
    float   g_tracingStartDistanceFromCamera;
    uint    g_detailNoiseSrv;  // offset 252: detail noise 3D texture SRV (0xFFFFFFFF = none)
    float   g_minStepSize;
    float   g_maxStepSize;
    float   g_perspectiveStepScale;
    uint    g_stbnSrvIndex;
    uint    g_frameIndex;
    uint    g_proceduralNoise;
    float   g_noiseXYScale;
    float   g_noiseZScale;
    uint    g_shapeOctaves;
    float   g_fbmDecay;
    float   g_fbmLacunarity;
    float   g_detailErosion;
    float   g_densityScale;
    float   g_jitterScale;
    float   g_powderScale;
    float   g_powderExponent;
    uint    g_multiScatterOctaves;
#ifdef UZ_CLOUD_TEMPORAL_UPSCALE
    uint    g_temporalUpscale;
    uint    g_fullWidth;
    uint    g_fullHeight;
    uint    g_frontDepthUav;
    float3  _cbPad;
    float4  _cbReserved[10];
#else
    float3  _cbPad;
    float4  _cbReserved[11];
#endif
};

SamplerState linearSampler : register(s3);

#ifndef UZ_BINDLESS
Texture3D<float>    g_ShapeNoiseTex : register(t0);
RWTexture2D<float4> g_OutputTex     : register(u0);
#endif

/// @ref Bruneton 2017 — Earth radius = 6360 km
static const float PLANET_RADIUS_KM = 6360.0f;
static const float EXT_EPS = 1e-7f;

/// @ref Schneider 2015 — cloud density normalization (CVar r.Cloud.DensityScale)
#define DENSITY_SCALE g_densityScale

/// Planet surface safety offset (km).
/// @ref Bruneton 2008 — offset to avoid surface intersection
/// @constants 0.005 km = 5 meters
static const float PLANET_RADIUS_OFFSET_KM = 0.005f;

// ─────────────────────────────────────────────────────────────────────────────
// Ray-Sphere Intersection
// ─────────────────────────────────────────────────────────────────────────────

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
// Ray Direction Reconstruction (same method as sky_atmosphere_ps.hlsl)
// ─────────────────────────────────────────────────────────────────────────────

float3 ReconstructRayDir(uint2 pixelCoord) {
#ifdef UZ_CLOUD_TEMPORAL_UPSCALE
    uint2 fullPixel = min(pixelCoord * 4 + CloudBayerOffset(g_frameIndex),
                         uint2(g_fullWidth - 1, g_fullHeight - 1));
    float2 uv = (float2(fullPixel) + 0.5f) / float2(g_fullWidth, g_fullHeight);
#else
    float2 uv = (float2(pixelCoord) + 0.5f) / float2(g_halfWidth, g_halfHeight);
#endif
    float2 ndc = uv * 2.0f - 1.0f;
    ndc.y = -ndc.y;

    float4 pt0 = mul(float4(ndc, 0.25f, 1.0f), g_invViewProj);
    float4 pt1 = mul(float4(ndc, 0.75f, 1.0f), g_invViewProj);
    float3 world0 = pt0.xyz / pt0.w;
    float3 world1 = pt1.xyz / pt1.w;
    return normalize(world0 - world1);
}

// ─────────────────────────────────────────────────────────────────────────────
// Cloud Shell Intersection
// ─────────────────────────────────────────────────────────────────────────────

/// @ref three-geospatial getRayNearFar — 3-case cloud shell intersection
bool GetRayNearFar(float3 cameraSkyKm, float3 rayDir, float innerRadius, float outerRadius,
                   out float tNear, out float tFar) {
    tNear = 0.0f;
    tFar  = 0.0f;

    float cameraHeight = length(cameraSkyKm);
    float3 planetCenter = float3(0.0f, 0.0f, 0.0f);

    float2 groundHit = RaySphereIntersect(cameraSkyKm, rayDir, planetCenter, PLANET_RADIUS_KM);
    float2 innerHit = RaySphereIntersect(cameraSkyKm, rayDir, planetCenter, innerRadius);
    float2 outerHit = RaySphereIntersect(cameraSkyKm, rayDir, planetCenter, outerRadius);

    if (outerHit.y < 0.0f) return false;

    if (cameraHeight < innerRadius) {
        // Case 1: Below cloud layer
        if (innerHit.y < 0.0f) return false;
        tNear = innerHit.y;
        tFar  = outerHit.y;
        // Reject if looking down (ground hit before inner sphere exit)
        if (groundHit.x > 0.0f && groundHit.x < tNear) return false;

    } else if (cameraHeight > outerRadius) {
        // Case 3: Above cloud layer
        tNear = outerHit.x;
        tFar = (innerHit.x > 0.0f) ? innerHit.x : outerHit.y;

    } else {
        // Case 2: Inside cloud layer
        tNear = 0.0f;
        float exitInner = (innerHit.x > 0.0f) ? innerHit.x : -1.0f;
        float exitOuter = outerHit.y;
        tFar = (exitInner > 0.0f) ? min(exitInner, exitOuter) : exitOuter;
    }

    tNear = max(tNear, 0.0f);
    return (tFar > tNear);
}

// ─────────────────────────────────────────────────────────────────────────────
// Henyey-Greenstein Phase Function
// ─────────────────────────────────────────────────────────────────────────────

/// @ref Schneider 2015 — dual-lobe Henyey-Greenstein
float HenyeyGreenstein(float cosTheta, float g) {
    float g2 = g * g;
    float denom = 1.0f + g2 - 2.0f * g * cosTheta;
    return (1.0f / (4.0f * PI)) * (1.0f - g2) / (denom * sqrt(denom));
}

float DualLobeHG(float cosTheta, float g1, float g2, float blend) {
    return lerp(HenyeyGreenstein(cosTheta, g1),
                HenyeyGreenstein(cosTheta, g2),
                blend);
}

// Opt in at shader compilation; the default retains component-controlled dual HG.
#ifndef ACCURATE_PHASE_FUNCTION
#define ACCURATE_PHASE_FUNCTION 0
#endif

/// @ref Draine 2003 "Scattering by Interstellar Dust Grains. I. Optical and Ultraviolet"
/// @ref NVIDIA 2018 "Approximate Mie Scattering"
/// @constants gHG=0.988, gD=0.556, alpha=22, Draine weight=0.482.
float CloudPhase(float cosTheta, float eccentricityScale) {
#if ACCURATE_PHASE_FUNCTION
    float g = 0.556f * eccentricityScale;
    float alpha = 22.0f * eccentricityScale;
    float g2 = g * g;
    float denom = 1.0f + g2 - 2.0f * g * cosTheta;
    float draine = (1.0f - g2) * (1.0f + alpha * cosTheta * cosTheta)
        / (4.0f * PI * (1.0f + alpha * (1.0f + 2.0f * g2) / 3.0f)
           * denom * sqrt(denom));
    return lerp(HenyeyGreenstein(cosTheta, 0.988f * eccentricityScale), draine, 0.482f);
#else
    return DualLobeHG(cosTheta, g_phaseG * eccentricityScale,
                      g_phaseG2 * eccentricityScale, g_phaseBlend);
#endif
}

/// @ref Oz & Kulla 2017 "Production Volume Rendering"
/// @constants kMaxOctaves=8; scattering, extinction and eccentricity attenuation a=b=c=0.5.
float3 EvaluateMultiScattering(float opticalDepthToSun, float cosTheta) {
    float3 radiance = 0.0f;
    float a = 1.0f;
    float b = 1.0f;
    float c = 1.0f;
    static const uint kMaxOctaves = 8;
    [loop]
    for (uint octave = 0; octave < min(g_multiScatterOctaves, kMaxOctaves); ++octave) {
        float beerLambert = exp(-opticalDepthToSun * b);
        radiance += a * beerLambert * CloudPhase(cosTheta, c) * g_sunColor;
        a *= 0.5f;
        b *= 0.5f;
        c *= 0.5f;
    }
    return radiance;
}

// ─────────────────────────────────────────────────────────────────────────────
// Height Gradient
// ─────────────────────────────────────────────────────────────────────────────

/// Height gradient for cumulus cloud density profile.
/// Sharp flat bottom (clouds have well-defined bases), rounded top that
/// peaks around 30% height then gradually fades — the classic cumulus shape.
/// @ref Schneider 2015 — height-based density gradient for cumulus
float HeightGradient(float sampleHeight, float innerRadius, float layerHeight) {
    float h = saturate((sampleHeight - innerRadius) / layerHeight);
    // Sharp bottom ramp (flat cloud base)
    float bottom = smoothstep(0.0f, 0.07f, h);
    // Rounded top fade — peak density around 20-40%, gradual falloff to top
    float top = smoothstep(1.0f, 0.3f, h);
    return bottom * top;
}

// ─────────────────────────────────────────────────────────────────────────────
// Procedural Noise
// ─────────────────────────────────────────────────────────────────────────────

/// @ref Inigo Quilez — "Hash without Sine" procedural noise
float Hash3D(float3 p) {
    p = frac(p * float3(0.1031f, 0.1030f, 0.0973f));
    p += dot(p, p.yxz + 33.33f);
    return frac((p.x + p.y) * p.z);
}

float ValueNoise3D(float3 p) {
    float3 i = floor(p);
    float3 f = frac(p);
    f = f * f * (3.0f - 2.0f * f);

    return lerp(
        lerp(lerp(Hash3D(i + float3(0,0,0)), Hash3D(i + float3(1,0,0)), f.x),
             lerp(Hash3D(i + float3(0,1,0)), Hash3D(i + float3(1,1,0)), f.x), f.y),
        lerp(lerp(Hash3D(i + float3(0,0,1)), Hash3D(i + float3(1,0,1)), f.x),
             lerp(Hash3D(i + float3(0,1,1)), Hash3D(i + float3(1,1,1)), f.x), f.y),
        f.z);
}

/// FBM with N octaves — 3D isotropic noise for volumetric cloud shapes.
/// @ref Inigo Quilez, Shadertoy XslGRr — FBM clouds
/// @ref Schneider 2015 — multi-octave noise for cloud shapes
/// CVar-driven: r.Cloud.FBMDecay, r.Cloud.FBMLacunarity, r.Cloud.ShapeOctaves
float FBM(float3 p, uint octaves) {
    float value = 0.0f;
    float amplitude = 0.5f;
    float frequency = 1.0f;
    for (uint oct = 0; oct < min(octaves, 6u); ++oct) {
        value += amplitude * ValueNoise3D(p * frequency);
        amplitude *= g_fbmDecay;
        frequency *= g_fbmLacunarity;
    }
    return value;
}

// ─────────────────────────────────────────────────────────────────────────────
// Density Sampling
// ─────────────────────────────────────────────────────────────────────────────

/// Cloud density using 3D isotropic noise coordinates.
/// @ref Inigo Quilez, Shadertoy XslGRr — 3D world-space noise coords give
///   volumetric shapes (spheres, towers) rather than flat layers.
/// @ref Schneider 2015 — coverage remap + height gradient
///
/// Key difference from flat clouds: noise is sampled at uniform 3D coordinates
/// relative to the planet surface, NOT (XY, normalizedH). This produces
/// actual 3D structures instead of horizontal slabs.
float SampleDensity(float3 samplePosKm, float innerRadius, float layerHeight, float mipLevel) {
    float sampleHeight = length(samplePosKm);
    float hGrad = HeightGradient(sampleHeight, innerRadius, layerHeight);
    if (hGrad <= 0.0f) return 0.0f;

    float normalizedH = saturate((sampleHeight - innerRadius) / layerHeight);
    float3 windOffset = float3(g_windVelocityX, g_windVelocityY, g_windVelocityZ) * g_time;

    // Noise coordinates: XY from world-space scaled for ~8 km tiling,
    // Z from normalized height stretched for vertical detail.
    /// @ref Schneider 2015 — noise coordinate mapping
    /// @constants XY_SCALE = 0.12, Z_SCALE = 3.0
    float3 noisePos = float3(
        (samplePosKm.x + windOffset.x) * g_noiseXYScale,
        (samplePosKm.y + windOffset.y) * g_noiseXYScale,
        normalizedH * g_noiseZScale + windOffset.z * 0.05f
    );

    // Shape noise — FBM with CVar-driven octaves
    /// @ref Schneider 2015 — multi-octave noise for cloud shapes
    float shape;
#ifdef UZ_BINDLESS
    if (g_proceduralNoise == 0 && g_shapeNoiseSrv != 0xFFFFFFFF) {
        Texture3D<float> shapeTex = ResourceDescriptorHeap[g_shapeNoiseSrv];
        // shape.bin already contains combined Perlin-Worley, not four independent
        // channels. Remapping replicated RGBA as R minus GBA would erase it.
        shape = shapeTex.SampleLevel(linearSampler, noisePos, mipLevel);
    } else
#endif
    {
        shape = FBM(noisePos, g_shapeOctaves);
    }

    // Coverage-based density: shape noise modulated by coverage and height.
    // Use a softer remap than Schneider's hard threshold — our procedural FBM
    // has a narrow value range (~0.3-0.6) compared to Perlin-Worley textures
    // (~0.0-1.0), so a hard remap at 1-coverage cuts most of the signal.
    // Instead: bias the noise by coverage, then saturate.
    /// @ref Schneider 2015 slide 70 — adapted for procedural FBM range
    float shapeDensity = g_proceduralNoise != 0 || g_shapeNoiseSrv == 0xFFFFFFFF
        ? saturate(shape + g_coverage - 0.5f)
        : saturate((shape - (1.0f - g_coverage)) / max(g_coverage, EXT_EPS));
    // Apply a power curve to create contrast (thick core, thin edges)
    shapeDensity = shapeDensity * shapeDensity;

    // ── Detail noise erosion ──────────────────────────────────────────────
    /// @ref Schneider & Vos 2015 — detail noise erosion at higher frequency
    /// Subtracts high-frequency 3D detail noise from the base shape density
    /// to add fine cloud edge detail (wisps, tendrils).
    /// @constants DETAIL_FREQ_SCALE = 4.0, DETAIL_EROSION = 0.35
#ifdef UZ_BINDLESS
    if (g_detailNoiseSrv != 0xFFFFFFFF && shapeDensity > 0.0f) {
        Texture3D<float> detailTex = ResourceDescriptorHeap[g_detailNoiseSrv];
        float3 detailPos = noisePos * (g_fbmLacunarity * 2.0f) + float3(windOffset.x * g_detailWindScale,
                                                      windOffset.y * g_detailWindScale,
                                                      windOffset.z * 0.1f);
        // 32^3 detail has one quarter the shape volume's texel density at
        // the default lacunarity (2); keep both LOD footprints consistent.
        float detailMip = max(0.0f, mipLevel + log2(g_fbmLacunarity * 0.125f));
        float detailNoise = detailTex.SampleLevel(linearSampler, detailPos * 0.25f, detailMip);
        float erosionStrength = g_detailErosion * (1.0f - saturate(shapeDensity * 2.0f));
        shapeDensity = saturate(shapeDensity - detailNoise * erosionStrength);
    }
#endif

    return shapeDensity * hGrad * g_density;
}

// ─────────────────────────────────────────────────────────────────────────────
// Light March
// ─────────────────────────────────────────────────────────────────────────────

/// @ref Schneider & Vos 2015 - filtered noise for coarse ray segments.
/// @constants shape resolution = 128; coordinates are per-km derivatives.
float DensityMip(float3 direction, float stepLength, float distance) {
    float3 texelsPerKm = 128.0f * float3(g_noiseXYScale, g_noiseXYScale,
                                        g_noiseZScale / max(g_layerHeightKm, EXT_EPS));
    float3 segment = abs(direction * texelsPerKm) * stepLength;
    float segmentFootprint = max(segment.x, max(segment.y, segment.z));
    float pixelFootprint = distance * max(texelsPerKm.x, texelsPerKm.z) / float(g_halfHeight);
    return log2(max(1.0f, max(segmentFootprint, pixelFootprint)));
}

/// @ref Schneider 2015 — secondary light ray march toward sun.
/// Return optical depth, not transmittance: higher scattering orders must
/// attenuate depth before exponentiation, including optically thick samples.
float LightMarch(float3 samplePosKm, float3 sunDir, float innerRadius,
                 float outerRadius, float layerHeight) {
    float2 outerHit = RaySphereIntersect(samplePosKm, sunDir,
                                          float3(0, 0, 0), outerRadius);
    float lightDist = max(outerHit.y, 0.0f);
    if (lightDist <= 0.0f) return 0.0f;
    lightDist = min(lightDist, layerHeight * 2.0f);

    uint lightSteps = clamp(g_lightSteps, 1u, 8u);
    float stepLen = lightDist / float(lightSteps);
    float totalDensity = 0.0f;
    float mipLevel = DensityMip(sunDir, stepLen, 0.0f);

    for (uint i = 0; i < lightSteps; ++i) {
        float t = (float(i) + 0.5f) * stepLen;
        float3 lightPos = samplePosKm + sunDir * t;
        totalDensity += SampleDensity(lightPos, innerRadius, layerHeight, mipLevel) * stepLen;
    }

    return totalDensity * g_extinction * DENSITY_SCALE;
}

// ─────────────────────────────────────────────────────────────────────────────
// Main
// ─────────────────────────────────────────────────────────────────────────────

[numthreads(8, 8, 1)]
void main(uint3 DTid : SV_DispatchThreadID) {
    if (DTid.x >= g_halfWidth || DTid.y >= g_halfHeight) return;

#ifdef UZ_BINDLESS
    RWTexture2D<float4> output = ResourceDescriptorHeap[g_outputUavIdx];
#ifdef UZ_CLOUD_TEMPORAL_UPSCALE
    RWTexture2D<float> frontDepth = ResourceDescriptorHeap[g_frontDepthUav];
    frontDepth[DTid.xy] = 0.0f;
#endif
#else
    RWTexture2D<float4> output = g_OutputTex;
#endif

    float3 rayDir = ReconstructRayDir(DTid.xy);

    // Camera -> sky-space (planet center at origin, Z=0 is sea level)
    float3 cameraSkyKm = g_cameraPos + float3(0.0f, 0.0f, PLANET_RADIUS_KM);

    // Snap above planet surface
    float distToCenter = length(cameraSkyKm);
    float minSafe = PLANET_RADIUS_KM + PLANET_RADIUS_OFFSET_KM;
    if (distToCenter < minSafe) {
        cameraSkyKm = cameraSkyKm * (minSafe / distToCenter);
    }

    float innerRadius = PLANET_RADIUS_KM + g_layerBottomKm;
    float outerRadius = innerRadius + g_layerHeightKm;

    // ── Cloud shell intersection ──
    float tNear, tFar;
    bool hit = GetRayNearFar(cameraSkyKm, rayDir, innerRadius, outerRadius, tNear, tFar);

    if (!hit) {
        output[DTid.xy] = float4(0.0f, 0.0f, 0.0f, 1.0f);
        return;
    }

    tFar = min(tFar, tNear + g_tracingMaxDistanceKm);
#ifdef UZ_BINDLESS
    if (g_depthSrvIdx != 0xFFFFFFFF) {
        Texture2D<float> depthTex = ResourceDescriptorHeap[g_depthSrvIdx];
        uint width, height;
        depthTex.GetDimensions(width, height);
#ifdef UZ_CLOUD_TEMPORAL_UPSCALE
        uint2 depthPixel = min(DTid.xy * 4 + CloudBayerOffset(g_frameIndex),
                              uint2(width - 1, height - 1));
#else
        uint2 depthPixel = min(DTid.xy * 2, uint2(width - 1, height - 1));
#endif
        float depth = depthTex.Load(int3(depthPixel, 0));
        if (depth > 0.0f) {
            float2 uv = (float2(depthPixel) + 0.5f) / float2(width, height);
            float4 world = mul(float4(uv.x * 2 - 1, 1 - uv.y * 2, depth, 1), g_invViewProj);
            // Engine world coordinates are centimeters; cloud distances are km.
            tFar = min(tFar, length(world.xyz / world.w * 1e-5f - g_cameraPos));
        }
    }
#endif
    if (tFar <= tNear) {
        output[DTid.xy] = float4(0.0f, 0.0f, 0.0f, 1.0f);
        return;
    }

    // ── Phase function ──
    float3 sunDir = normalize(g_sunDir);
    float cosTheta = dot(rayDir, sunDir);

    // ── Ray march (Frostbite analytical integration) ──
    /// @ref Hillaire 2020 — (S - S*T)/max(ext, eps)
    float3 luminance    = float3(0.0f, 0.0f, 0.0f);
    float  transmittance = 1.0f;
#ifdef UZ_CLOUD_TEMPORAL_UPSCALE
    float weightedDepth = 0.0f;
#endif

    /// @ref Wolfe et al. 2022 "Spatiotemporal Blue Noise Masks"
    /// @constants kMaxIterations = 256, STBN extent = 128x128x64.
    static const uint kMaxIterations = 256;
    float minStep = max(g_minStepSize, 0.001f);
    float maxStep = max(minStep, g_maxStepSize);
    float stepT = clamp(max(tNear * (g_perspectiveStepScale - 1.0f),
                            (tFar - tNear) / float(kMaxIterations)), minStep, maxStep);
    float t = tNear;
#ifdef UZ_BINDLESS
    if (g_stbnSrvIndex != 0xFFFFFFFF) {
        Texture3D<float> stbn = ResourceDescriptorHeap[g_stbnSrvIndex];
        t += stepT * stbn.Load(int4(DTid.xy % 128, g_frameIndex % 64, 0)) * 2.0f * g_jitterScale;
    }
#endif
    for (uint i = 0; i < kMaxIterations && t < tFar; ++i) {
        if (transmittance < g_stopTracingTransmittanceThreshold) break;

        float stepLen = min(stepT, tFar - t);
        float3 samplePos = cameraSkyKm + rayDir * t;
        // Footprint in the 128^3 shape volume, including perspective growth.
        /// @ref Schneider & Vos 2015 - filtered volume sampling.
        float mipLevel = DensityMip(rayDir, stepLen, t);
        float density = SampleDensity(samplePos, innerRadius, g_layerHeightKm, mipLevel);
        t += stepLen;
        stepT = clamp(stepT * g_perspectiveStepScale, minStep, maxStep);
        if (density <= 0.0f) {
            stepT = min(maxStep, stepT * (2.0f + mipLevel));
            continue;
        }

        float ext = density * g_extinction * DENSITY_SCALE;
        float scat = ext * g_scatteringAlbedo;
        float T_step = exp(-ext * stepLen);

        // Light march toward sun for self-shadowing
        float opticalDepthToSun = LightMarch(samplePos, sunDir, innerRadius, outerRadius, g_layerHeightKm);
        float3 radiance = EvaluateMultiScattering(opticalDepthToSun, cosTheta);

        /// @ref Oz & Kulla 2017 "Production Volume Rendering" — isotropic sky source.
        /// sh_projection.cpp packs E/pi, not a solid-angle-integrated volume
        /// source. Convert that radiance proxy before applying isotropic phase.
        /// Preserve M1's base illumination while allowing brighter exposed tops.
        /// @constants bottom visibility=0.8, normalized to the legacy base fill.
        float heightFraction = saturate((length(samplePos) - innerRadius) / g_layerHeightKm);
        float3 skyIrradiance = g_ambientColor * (4.0f * PI);
        float skyGradient = lerp(0.8f, 1.0f, heightFraction);
        float skyLightScale = g_ambientMultiplier / 0.8f;
        radiance += skyIrradiance * (1.0f / (4.0f * PI)) * skyGradient * skyLightScale;

        /// @ref Schneider & Vos 2015 "Real-Time Volumetric Cloudscapes of Horizon Zero Dawn"
        /// @constants powderScale=0 disables powder; powderExponent=2 by default.
        radiance *= 1.0f - g_powderScale * exp(-ext * g_powderExponent);
        float3 scatter = scat * radiance;

        // Frostbite analytical integration
        float3 Sint = (scatter - scatter * T_step) / max(ext, EXT_EPS);

        luminance    += transmittance * Sint;
#ifdef UZ_CLOUD_TEMPORAL_UPSCALE
        weightedDepth += transmittance * (1.0f - T_step) * (t - stepLen * 0.5f);
#endif
        transmittance *= T_step;
    }

    // ── Aerial perspective — distant clouds fade toward sky color ─────
    // Approximates atmospheric scattering between camera and cloud.
    /// @ref Hillaire 2020 — aerial perspective on volumetric clouds
    float cloudDist = tNear; // distance to first cloud sample (km)
    float apFactor = 1.0f - exp(-cloudDist * 0.04f);
    float3 skyColor = g_ambientColor;
    if (dot(skyColor, skyColor) < 0.001f) {
        skyColor = float3(0.4f, 0.55f, 0.8f); // fallback blue sky
    }
    luminance = lerp(luminance, skyColor * (1.0f - transmittance), apFactor);

    output[DTid.xy] = float4(luminance, transmittance);
#if defined(UZ_BINDLESS) && defined(UZ_CLOUD_TEMPORAL_UPSCALE)
    // @ref Hillaire 2020 - extinction/transmittance weighted cloud distance (km).
    frontDepth[DTid.xy] = weightedDepth / max(1.0f - transmittance, EXT_EPS);
#endif
}
