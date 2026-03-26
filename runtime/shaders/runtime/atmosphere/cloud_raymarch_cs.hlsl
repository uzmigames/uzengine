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

#include "../include/units.hlsli"

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
// Cloud Constant Buffer (b2)
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
};

SamplerState linearSampler : register(s0);

#ifndef UZ_BINDLESS
Texture3D<float>    g_ShapeNoiseTex : register(t0);
RWTexture2D<float4> g_OutputTex     : register(u0);
#endif

#ifndef UZ_PI_DEFINED
#define UZ_PI_DEFINED
static const float PI = 3.14159265359f;
#endif

/// @ref Bruneton 2017 — Earth radius = 6360 km
static const float PLANET_RADIUS_KM = 6360.0f;
static const float EXT_EPS = 1e-7f;

/// Density-to-extinction scale factor.
/// SampleDensity() returns normalized values in [0,1]. Real cloud extinction
/// coefficients are typically 10-40 km^-1 (Schneider 2015). The component's
/// base extinction (0.04 km^-1) is scaled by this factor to produce physically
/// correct optical depth: 0.04 * 25 = 1.0 km^-1 effective extinction, which
/// gives realistic opacity over the 10 km cloud layer height.
/// @ref Schneider 2015 — cloud density normalization
static const float DENSITY_SCALE = 25.0f;

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
        // The ray must exit the inner sphere (going outward) then traverse the shell
        if (innerHit.y < 0.0f) return false; // Shouldn't happen but guard
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

/// Height gradient for cloud density distribution within the layer.
/// Uses a rounded-bottom profile that peaks around 30% height then
/// fades at the top — typical of cumulus clouds.
/// @ref Schneider 2015 — height-based density gradient for cumulus
float HeightGradient(float sampleHeight, float innerRadius, float layerHeight) {
    float normalizedH = saturate((sampleHeight - innerRadius) / layerHeight);
    // Smooth ramp up from bottom, gradual fade at top
    float bottomFade = smoothstep(0.0f, 0.15f, normalizedH);
    float topFade    = smoothstep(1.0f, 0.6f, normalizedH);
    return bottomFade * topFade;
}

// ─────────────────────────────────────────────────────────────────────────────
// Density Sampling
// ─────────────────────────────────────────────────────────────────────────────

// ─── Procedural Value Noise ──────────────────────────────────────────────────
// Simple GPU hash-based noise for cloud shapes until 3D texture upload is fixed.
// @ref Inigo Quilez — "Hash without Sine" procedural noise

float Hash3D(float3 p) {
    p = frac(p * float3(0.1031f, 0.1030f, 0.0973f));
    p += dot(p, p.yxz + 33.33f);
    return frac((p.x + p.y) * p.z);
}

float ValueNoise3D(float3 p) {
    float3 i = floor(p);
    float3 f = frac(p);
    f = f * f * (3.0f - 2.0f * f); // smoothstep

    return lerp(
        lerp(lerp(Hash3D(i + float3(0,0,0)), Hash3D(i + float3(1,0,0)), f.x),
             lerp(Hash3D(i + float3(0,1,0)), Hash3D(i + float3(1,1,0)), f.x), f.y),
        lerp(lerp(Hash3D(i + float3(0,0,1)), Hash3D(i + float3(1,0,1)), f.x),
             lerp(Hash3D(i + float3(0,1,1)), Hash3D(i + float3(1,1,1)), f.x), f.y),
        f.z);
}

/// Fractal Brownian Motion — 4 octaves of value noise.
/// @ref Schneider 2015 — multi-octave noise for cloud shapes
float FBM4(float3 p) {
    float value = 0.0f;
    float amplitude = 0.5f;
    float frequency = 1.0f;
    for (int oct = 0; oct < 4; ++oct) {
        value += amplitude * ValueNoise3D(p * frequency);
        amplitude *= 0.5f;
        frequency *= 2.0f;
    }
    return value;
}

// ─── Density Sampling ────────────────────────────────────────────────────────

float SampleDensity(float3 samplePosKm, float innerRadius, float layerHeight) {
    float sampleHeight = length(samplePosKm);
    float hGrad = HeightGradient(sampleHeight, innerRadius, layerHeight);
    if (hGrad <= 0.0f) return 0.0f;

    float3 windOffset = g_windVelocity * g_time;
    float normalizedH = saturate((sampleHeight - innerRadius) / layerHeight);

    // Sample 3D shape noise texture (128^3 Perlin-Worley).
    // UV: world XY position tiled at ~8km period, Z = normalized height in layer.
    // The wrap sampler handles tiling automatically.
    /// @ref Schneider 2015 — Perlin-Worley noise for cloud shapes
    /// @ref three-geospatial shapeRepeat — noise tiling scale
    // Procedural FBM noise for cloud shapes
    float3 noiseCoord = float3(
        (samplePosKm.x + windOffset.x) * 0.12f,
        (samplePosKm.y + windOffset.y) * 0.12f,
        normalizedH * 3.0f + windOffset.z * 0.05f
    );
    float noise = FBM4(noiseCoord);

    // Coverage remap: remap(noise, 1-coverage, 1, 0, 1)
    /// @ref Schneider 2015 slide 70 — density = remap(shape, 1-coverage, 1, 0, 1)
    float coverageThreshold = 1.0f - g_coverage;
    float shapeDensity = saturate((noise - coverageThreshold) / max(g_coverage, 0.001f));
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
    float minSafe = PLANET_RADIUS_KM + UZ_PLANET_RADIUS_OFFSET_KM;
    if (distToCenter < minSafe) {
        cameraSkyKm = cameraSkyKm * (minSafe / distToCenter);
    }

    float innerRadius = PLANET_RADIUS_KM + g_layerBottomKm;
    float outerRadius = innerRadius + g_layerHeightKm;

    // NOTE: Depth check is NOT done here because the cloud compute runs on a
    // separate command list BEFORE the render graph, so the depth buffer may be
    // in DepthStencilWrite state (not readable). Instead, the depth check is
    // performed in the composite pixel shader where the render graph manages
    // barrier state correctly.

    // ── Cloud shell intersection ──
    float tNear, tFar;
    bool hit = GetRayNearFar(cameraSkyKm, rayDir, innerRadius, outerRadius, tNear, tFar);

    if (!hit) {
        output[DTid.xy] = float4(0.0f, 0.0f, 0.0f, 1.0f);
        return;
    }

    // Cloud shell intersection confirmed — proceed to ray march.

    // Clamp march distance (50 km max for performance)
    tFar = min(tFar, tNear + 50.0f);

    // Clamp step count to avoid TDR on large march distances
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
    // Cloud interiors receive multi-scattered light approximated as ambient.
    // If the scene ambient is zero (IBL not ready), use a fraction of sun color.
    /// @ref Schneider 2015 — multi-scatter ambient approximation
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
        // After the first scatter event, photons continue bouncing inside the cloud,
        // brightening the interior. This is approximated by boosting the sun
        // transmittance: each scatter order loses less energy.
        /// @ref Schneider 2015 — multi-scatter energy approximation
        /// @ref Wrenninge 2015 — "Art-Directable Multiple Volumetric Scattering"
        float msPhase = 1.0f;
        float msExt   = 1.0f;
        float3 msLuminance = float3(0.0f, 0.0f, 0.0f);
        float msSunT = sunT;
        for (int ms = 0; ms < 3; ++ms) {
            msPhase *= 0.5f;   // Phase becomes more isotropic
            msExt   *= 0.25f;  // Lower extinction (photons penetrate deeper)
            msSunT = max(msSunT, pow(sunT, msExt)); // Higher transmittance
            float msP = lerp(phase, 1.0f / (4.0f * PI), 1.0f - msPhase);
            msLuminance += g_sunColor * msSunT * msP * msPhase;
        }

        // In-scattering: sun (direct + multi-scatter) + ambient
        float3 sunScatter = g_sunColor * sunT * phase + msLuminance;
        float3 ambScatter = ambient;
        float3 scatter = scat * (sunScatter + ambScatter);

        // Frostbite analytical integration
        float3 Sint = (scatter - scatter * T_step) / max(ext, EXT_EPS);

        luminance    += transmittance * Sint;
        transmittance *= T_step;
    }

    output[DTid.xy] = float4(luminance, transmittance);
}
