/// Sky atmosphere pixel shader — Hillaire 2020 single-scatter approximation.
///
/// Renders physical sky color for pixels with no geometry (depth == 1.0).
/// Uses 16-step ray marching through the atmosphere with Rayleigh + Mie
/// scattering and a Henyey-Greenstein phase function. Adds a sun disk.
///
/// # Inputs
/// - PerFrameCB at b1: camera, invViewProj, lightDir, lightColor
/// - SkyAtmosphereCB at b2: atmosphere parameters
/// - Depth texture: to skip pixels with geometry
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
    uint _pad0;
    uint _pad1;
    uint _pad2;
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
    float    _framePad[10];
};

cbuffer SkyAtmosphereCB : register(b2) {
    float4 rayleighScattering;   // xyz = coefficients, w = scale height
    float4 mieScattering;        // x = coefficient, y = anisotropy, z = scale height, w = unused
    float4 groundAlbedo;         // xyz = ground color, w = unused
    float4 sunIlluminance;       // xyz = sun color, w = angular radius
    float  planetRadius;
    float  atmosphereHeight;
    float  sunDiskIntensity;
};

#ifndef UZ_BINDLESS
Texture2D<float> sceneDepth : register(t0);
#endif

SamplerState pointSampler : register(s0);

// ─────────────────────────────────────────────────────────────────────────────
// Constants
// ─────────────────────────────────────────────────────────────────────────────

static const int   NUM_STEPS = 16;

// ─────────────────────────────────────────────────────────────────────────────
// Atmosphere Helpers
// ─────────────────────────────────────────────────────────────────────────────

/// Rayleigh phase function: (3/16pi) * (1 + cos^2(theta))
float RayleighPhase(float cosTheta) {
    return (3.0f / (16.0f * PI)) * (1.0f + cosTheta * cosTheta);
}

/// Henyey-Greenstein phase function for Mie scattering.
float MiePhase(float cosTheta, float g) {
    float g2 = g * g;
    float denom = 1.0f + g2 - 2.0f * g * cosTheta;
    return (1.0f / (4.0f * PI)) * (1.0f - g2) / (denom * sqrt(denom));
}

/// Computes the density ratio at a given altitude above sea level.
/// density = exp(-altitude / scaleHeight)
float DensityAtAltitude(float altitude, float scaleHeight) {
    return exp(-altitude / scaleHeight);
}

/// Ray-sphere intersection. Returns distance to the nearest intersection,
/// or -1 if the ray misses the sphere.
/// origin = ray origin, dir = normalized ray direction, center = sphere center, radius.
/// Returns (tNear, tFar). If tNear < 0, ray starts inside sphere.
float2 RaySphereIntersect(float3 origin, float3 dir, float3 center, float radius) {
    float3 oc = origin - center;
    float b = dot(oc, dir);
    float c = dot(oc, oc) - radius * radius;
    float discriminant = b * b - c;

    if (discriminant < 0.0f)
        return float2(-1.0f, -1.0f);

    float sqrtDisc = sqrt(discriminant);
    return float2(-b - sqrtDisc, -b + sqrtDisc);
}

// ─────────────────────────────────────────────────────────────────────────────
// Pixel Shader Entry Point
// ─────────────────────────────────────────────────────────────────────────────

float4 main(FSInput input) : SV_Target {
    float2 uv = input.texcoord;

    // ── Read depth — skip pixels with geometry ──
#ifdef UZ_BINDLESS
    Texture2D<float> depthTex = ResourceDescriptorHeap[skyIndices.depthIdx];
    float depth = depthTex.Sample(pointSampler, uv).r;
#else
    float depth = sceneDepth.Sample(pointSampler, uv).r;
#endif

    // Only render sky where there is no geometry (depth == 1.0 = far plane)
    if (depth < 0.9999f)
        discard;

    // ── Reconstruct world-space ray direction from screen UV ──
    float2 ndc = uv * 2.0f - 1.0f;
    ndc.y = -ndc.y;
    float4 farPoint = mul(float4(ndc, 1.0f, 1.0f), invViewProj);
    float3 worldFar = farPoint.xyz / farPoint.w;
    float3 rayDir = normalize(worldFar - cameraPos.xyz);

    // ── Setup atmosphere geometry ──
    // Planet center is below the camera at (0, -planetRadius, 0)
    // Camera altitude = cameraPos.y (world Y = up)
    float3 planetCenter = float3(0.0f, -planetRadius, 0.0f);
    float atmosphereRadius = planetRadius + atmosphereHeight;

    // Camera position in atmosphere-relative coordinates
    float3 rayOrigin = float3(0.0f, cameraPos.y + planetRadius, 0.0f);

    // ── Intersect ray with atmosphere ──
    float2 atmoHit = RaySphereIntersect(rayOrigin, rayDir, float3(0, 0, 0), atmosphereRadius);

    // No intersection with atmosphere
    if (atmoHit.y < 0.0f)
        return float4(0, 0, 0, 1);

    // Clamp start to camera position (ray may start inside atmosphere)
    float tStart = max(atmoHit.x, 0.0f);
    float tEnd   = atmoHit.y;

    // Check if ray hits the planet surface
    float2 planetHit = RaySphereIntersect(rayOrigin, rayDir, float3(0, 0, 0), planetRadius);
    if (planetHit.x > 0.0f) {
        tEnd = min(tEnd, planetHit.x);
    }

    float stepLength = (tEnd - tStart) / float(NUM_STEPS);

    // ── Sun direction (toward the light) ──
    float3 sunDir = normalize(lightDir.xyz);
    float cosTheta = dot(rayDir, sunDir);

    // Phase functions
    float rayleighPhase = RayleighPhase(cosTheta);
    float miePhase      = MiePhase(cosTheta, mieScattering.y);

    // ── Ray march through the atmosphere ──
    float3 totalRayleigh = float3(0, 0, 0);
    float3 totalMie      = float3(0, 0, 0);
    float  opticalDepthR = 0.0f;
    float  opticalDepthM = 0.0f;

    float3 betaR = rayleighScattering.xyz;
    float  betaM = mieScattering.x;
    float  scaleHeightR = rayleighScattering.w;
    float  scaleHeightM = mieScattering.z;

    for (int i = 0; i < NUM_STEPS; ++i) {
        float t = tStart + (float(i) + 0.5f) * stepLength;
        float3 samplePos = rayOrigin + rayDir * t;
        float altitude = length(samplePos) - planetRadius;

        // Density at this sample point
        float densityR = DensityAtAltitude(altitude, scaleHeightR);
        float densityM = DensityAtAltitude(altitude, scaleHeightM);

        opticalDepthR += densityR * stepLength;
        opticalDepthM += densityM * stepLength;

        // Light ray from sample point to sun — estimate optical depth
        // Use a simple 4-step secondary march for light transmittance
        float2 lightHit = RaySphereIntersect(samplePos, sunDir, float3(0, 0, 0), atmosphereRadius);
        float lightStepLen = lightHit.y / 4.0f;
        float lightOptR = 0.0f;
        float lightOptM = 0.0f;

        for (int j = 0; j < 4; ++j) {
            float tLight = (float(j) + 0.5f) * lightStepLen;
            float3 lightSample = samplePos + sunDir * tLight;
            float lightAlt = length(lightSample) - planetRadius;
            lightOptR += DensityAtAltitude(lightAlt, scaleHeightR) * lightStepLen;
            lightOptM += DensityAtAltitude(lightAlt, scaleHeightM) * lightStepLen;
        }

        // Combined transmittance from sun to sample to camera
        float3 tau = betaR * (opticalDepthR + lightOptR) +
                     betaM * 1.1f * (opticalDepthM + lightOptM);
        float3 transmittance = exp(-tau);

        totalRayleigh += densityR * transmittance * stepLength;
        totalMie      += densityM * transmittance * stepLength;
    }

    // ── Combine Rayleigh and Mie ──
    float3 sky = sunIlluminance.xyz * (
        totalRayleigh * betaR * rayleighPhase +
        totalMie      * betaM * miePhase
    );

    // ── Sun disk ──
    float sunCosAngle = cos(sunIlluminance.w);
    if (cosTheta > sunCosAngle) {
        // Smooth edge for the sun disk
        float sunEdge = smoothstep(sunCosAngle, sunCosAngle + 0.0002f, cosTheta);
        sky += sunIlluminance.xyz * sunDiskIntensity * sunEdge;
    }

    // DEBUG: dump offsets
    // rayleighScattering.x = ~5.8e-6 (offset 0) — tiny value, multiply big
    // sunIlluminance.x = 20.0 (offset 48)
    // planetRadius = 6360000 (offset 64)
    // Show: R=rayleigh*1e5, G=sunIllum*0.05, B=planet/1e7
    return float4(
        saturate(rayleighScattering.x * 1e5f),  // expect ~0.58
        saturate(sunIlluminance.x * 0.05f),      // expect 1.0
        saturate(planetRadius / 1e7f),            // expect 0.636
        1.0f
    );
}
