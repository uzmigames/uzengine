/// Cloud ray-march compute shader — volumetric cloud rendering.
///
/// @ref Schneider & Vos 2015 "Real-Time Volumetric Cloudscapes of Horizon Zero Dawn"
/// @ref Hillaire 2020 "A Scalable and Production Ready Sky and Atmosphere Rendering Technique"
/// @ref Frostbite analytical integration: (S - S*T) / max(ext, eps)
///
/// # Root Signature Layout (Compute)
/// - b0 (slot 0): 4x uint32 root constants {outputUavIdx, depthSrvIdx, halfWidth, halfHeight}
/// - b2 (slot 2): CloudCB constant buffer (256 bytes)
/// - u0 (slot 4): UAV descriptor table for output write
///
/// # Output
/// Half-resolution RGBA16F texture:
/// - RGB: premultiplied cloud color
/// - A: transmittance (1 = transparent, 0 = opaque)

#include "../common/constants.hlsli"
#include "../common/bindless.hlsli"

// ─────────────────────────────────────────────────────────────────────────────
// Root Constants (b0)
// ─────────────────────────────────────────────────────────────────────────────

cbuffer RootConstants : register(b0) {
    uint  g_outputUavIdx;
    uint  g_depthSrvIdx;
    uint  g_halfWidth;
    uint  g_halfHeight;
};

// ─────────────────────────────────────────────────────────────────────────────
// Cloud Constant Buffer (b2) — matches CloudCB in cloud_pass.cpp (256 bytes)
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
    float3  g_windVelocity;
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
    // ── Shader CVar parameters (Phase 3) ──
    float   g_noiseXYScale;     // CVar r.Cloud.NoiseXYScale (default 0.12)
    float   g_noiseZScale;      // CVar r.Cloud.NoiseZScale (default 3.0)
    uint    g_shapeOctaves;     // CVar r.Cloud.ShapeOctaves (default 4)
    float   g_fbmDecay;         // CVar r.Cloud.FBMDecay (default 0.5)
    float   g_fbmLacunarity;    // CVar r.Cloud.FBMLacunarity (default 2.0)
    float   g_detailErosion;    // CVar r.Cloud.DetailErosion (default 0.35)
    float   g_aerialPerspDensity; // CVar r.Cloud.AerialPerspectiveDensity (default 0.04)
    float   g_densityScale;     // CVar r.Cloud.DensityScale (default 25.0)
};

SamplerState linearSampler : register(s0);

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
    float2 uv = (float2(pixelCoord) + 0.5f) / float2(g_halfWidth, g_halfHeight);
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
    for (uint oct = 0; oct < octaves; ++oct) {
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
float SampleDensity(float3 samplePosKm, float innerRadius, float layerHeight) {
    float sampleHeight = length(samplePosKm);
    float hGrad = HeightGradient(sampleHeight, innerRadius, layerHeight);
    if (hGrad <= 0.0f) return 0.0f;

    float normalizedH = saturate((sampleHeight - innerRadius) / layerHeight);
    float3 windOffset = g_windVelocity * g_time;

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
    float shape = FBM(noisePos, g_shapeOctaves);

    // Coverage-based density: shape noise modulated by coverage and height.
    // Use a softer remap than Schneider's hard threshold — our procedural FBM
    // has a narrow value range (~0.3-0.6) compared to Perlin-Worley textures
    // (~0.0-1.0), so a hard remap at 1-coverage cuts most of the signal.
    // Instead: bias the noise by coverage, then saturate.
    /// @ref Schneider 2015 slide 70 — adapted for procedural FBM range
    float shapeDensity = saturate(shape + g_coverage - 0.5f);
    // Apply a power curve to create contrast (thick core, thin edges)
    shapeDensity = shapeDensity * shapeDensity;

    // ── Detail noise erosion ──────────────────────────────────────────────
    /// @ref Schneider & Vos 2015 — detail noise erosion at higher frequency
    /// Subtracts high-frequency 3D detail noise from the base shape density
    /// to add fine cloud edge detail (wisps, tendrils).
    /// @constants DETAIL_FREQ_SCALE = 4.0, DETAIL_EROSION = 0.35
#ifdef UZ_BINDLESS
    if (g_detailNoiseSrv != 0xFFFFFFFF && shapeDensity > 0.0f) {
        Texture3D<float4> detailTex = ResourceDescriptorHeap[g_detailNoiseSrv];
        float3 detailPos = noisePos * (g_fbmLacunarity * 2.0f) + float3(windOffset.x * g_detailWindScale,
                                                      windOffset.y * g_detailWindScale,
                                                      windOffset.z * 0.1f);
        float detailNoise = detailTex.SampleLevel(linearSampler, frac(detailPos * 0.25f), 0).r;
        float erosionStrength = g_detailErosion * (1.0f - saturate(shapeDensity * 2.0f));
        shapeDensity = saturate(shapeDensity - detailNoise * erosionStrength);
    }
#endif

    return shapeDensity * hGrad * g_density;
}

// ─────────────────────────────────────────────────────────────────────────────
// Light March
// ─────────────────────────────────────────────────────────────────────────────

/// @ref Schneider 2015 — secondary light ray march toward sun
float LightMarch(float3 samplePosKm, float3 sunDir, float innerRadius,
                 float outerRadius, float layerHeight) {
    float2 outerHit = RaySphereIntersect(samplePosKm, sunDir,
                                          float3(0, 0, 0), outerRadius);
    float lightDist = max(outerHit.y, 0.0f);
    if (lightDist <= 0.0f) return 1.0f;
    lightDist = min(lightDist, layerHeight * 2.0f);

    uint lightSteps = min(g_lightSteps, 6u);
    float stepLen = lightDist / float(lightSteps);
    float totalDensity = 0.0f;

    for (uint i = 0; i < lightSteps; ++i) {
        float t = (float(i) + 0.5f) * stepLen;
        float3 lightPos = samplePosKm + sunDir * t;
        totalDensity += SampleDensity(lightPos, innerRadius, layerHeight) * stepLen;
    }

    return exp(-totalDensity * g_extinction * DENSITY_SCALE);
}

// ─────────────────────────────────────────────────────────────────────────────
// Main
// ─────────────────────────────────────────────────────────────────────────────

[numthreads(8, 8, 1)]
void main(uint3 DTid : SV_DispatchThreadID) {
    if (DTid.x >= g_halfWidth || DTid.y >= g_halfHeight) return;

#ifdef UZ_BINDLESS
    RWTexture2D<float4> output = ResourceDescriptorHeap[g_outputUavIdx];
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

    // Clamp march distance (50 km max for performance)
    tFar = min(tFar, tNear + 50.0f);

    // Clamp step count to avoid TDR on large march distances.
    // Cap at 32 steps — original 64 causes GPU TDR on first frame with
    // cold shader compilation + heavy FBM 3D texture lookups.
    uint stepCount = min(g_primarySteps, 64u);
    float stepLen = (tFar - tNear) / float(stepCount);
    if (stepLen <= 0.0f) {
        output[DTid.xy] = float4(0.0f, 0.0f, 0.0f, 1.0f);
        return;
    }

    // ── Phase function ──
    float3 sunDir = normalize(g_sunDir);
    float cosTheta = dot(rayDir, sunDir);
    float phase = DualLobeHG(cosTheta, g_phaseG, g_phaseG2, g_phaseBlend);

    // ── Ambient ──
    /// @ref Schneider 2015 — multi-scatter ambient approximation
    /// Fallback to sun fraction when IBL SH is not yet computed.
    float3 ambient = g_ambientColor;
    if (dot(ambient, ambient) < 0.001f) {
        ambient = g_sunColor * 0.25f;
    }
    ambient *= g_ambientMultiplier;

    // ── Ray march (Frostbite analytical integration) ──
    /// @ref Hillaire 2020 — (S - S*T)/max(ext, eps)
    float3 luminance    = float3(0.0f, 0.0f, 0.0f);
    float  transmittance = 1.0f;

    for (uint i = 0; i < stepCount; ++i) {
        if (transmittance < 0.01f) break;

        float t = tNear + (float(i) + 0.5f) * stepLen;
        float3 samplePos = cameraSkyKm + rayDir * t;

        float density = SampleDensity(samplePos, innerRadius, g_layerHeightKm);
        if (density <= 0.0f) continue;

        float ext = density * g_extinction * DENSITY_SCALE;
        float scat = ext * g_scatteringAlbedo;
        float T_step = exp(-ext * stepLen);

        // Light march toward sun for self-shadowing
        float sunT = LightMarch(samplePos, sunDir, innerRadius, outerRadius, g_layerHeightKm);

        // Multi-scattering energy approximation.
        /// @ref Schneider 2015 — multi-scatter energy approximation
        /// @ref Wrenninge 2015 — "Art-Directable Multiple Volumetric Scattering"
        float msPhase = 1.0f;
        float msExt   = 1.0f;
        float3 msLuminance = float3(0.0f, 0.0f, 0.0f);
        float msSunT = sunT;
        for (int ms = 0; ms < 3; ++ms) {
            msPhase *= 0.5f;
            msExt   *= 0.25f;
            msSunT = max(msSunT, pow(sunT, msExt));
            float msP = lerp(phase, 1.0f / (4.0f * PI), 1.0f - msPhase);
            msLuminance += g_sunColor * msSunT * msP * msPhase;
        }

        // Beer-Powder effect — brightens thin cloud edges (silver lining).
        // At cloud boundaries where density is low, multi-scattered photons
        // create a bright rim around the cloud silhouette.
        /// @ref Schneider 2015 — Beer-Powder approximation
        /// @constants powder blend = 0.5 (mix between Beer and powder terms)
        float powder = 1.0f - exp(-density * g_extinction * DENSITY_SCALE * 2.0f);
        float beerPowder = lerp(1.0f, powder, 0.5f);

        // In-scattering: sun (direct + multi-scatter) + ambient
        float3 sunScatter = g_sunColor * sunT * phase * beerPowder + msLuminance;
        float3 ambScatter = ambient;
        float3 scatter = scat * (sunScatter + ambScatter);

        // Frostbite analytical integration
        float3 Sint = (scatter - scatter * T_step) / max(ext, EXT_EPS);

        luminance    += transmittance * Sint;
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
}
