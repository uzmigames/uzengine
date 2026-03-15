// sky_atmosphere_ps.hlsl.gen.h -- AUTO-GENERATED, DO NOT EDIT
#pragma once
namespace uz::generated {
inline constexpr char kSkyAtmospherePS[] = R"hlsl(
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

// ── begin include: ../include/pbr_common.hlsli ──
/// Shared PBR functions for Cook-Torrance BRDF evaluation.
///
/// Used by both the forward pass (mesh_ps.hlsl) and deferred lighting pass
/// (deferred_lighting_ps.hlsl). Implements GGX NDF, Smith geometry, and
/// Schlick-Fresnel from the metallic-roughness workflow.
///
/// # References
/// - Karis 2013, "Real Shading in Unreal Engine 4"
/// - Walter et al. 2007, "Microfacet Models for Refraction"

#ifndef PBR_COMMON_HLSLI
#define PBR_COMMON_HLSLI

// ── begin include: math_util.hlsli ──
/// Compiler-agnostic math utilities for UzEngine shaders.
///
/// FXC (SM5.0/DXBC) and DXC (SM6.x/DXIL) compile pow() with different
/// instruction patterns — FXC uses multiply chains for small integer
/// exponents while DXC always uses log-mul-exp. This causes visible
/// brightness divergence between DX11 and DX12 on the same GPU.
///
/// This header provides deterministic replacements that produce identical
/// results on both compilers, following the same pattern as UE5's
/// MathUtil.ush (Pow2/Pow4/Pow5) and Common.ush (LinearToSrgb).
///
/// # Usage
/// Include this header in any shader that needs pow-of-N or sRGB conversion:
/// ```hlsl
/// #include "include/math_util.hlsli"
/// float3 F = F0 + (1 - F0) * Pow5(1 - cosTheta);
/// color = LinearToSrgb(color);
/// ```

#ifndef MATH_UTIL_HLSLI
#define MATH_UTIL_HLSLI

// ─────────────────────────────────────────────────────────────────────────────
// Deterministic Power Functions
// ─────────────────────────────────────────────────────────────────────────────
// Explicit multiply chains — identical codegen on FXC and DXC.

/// x^2 via multiply (avoids pow).
float  Pow2(float  x) { return x * x; }
float2 Pow2(float2 x) { return x * x; }
float3 Pow2(float3 x) { return x * x; }
float4 Pow2(float4 x) { return x * x; }

/// x^4 via two squarings (avoids pow).
float  Pow4(float  x) { float x2 = x * x; return x2 * x2; }
float2 Pow4(float2 x) { float2 x2 = x * x; return x2 * x2; }
float3 Pow4(float3 x) { float3 x2 = x * x; return x2 * x2; }
float4 Pow4(float4 x) { float4 x2 = x * x; return x2 * x2; }

/// x^5 via multiply chain (avoids pow).
/// Primary use: Schlick Fresnel approximation pow(1 - cosTheta, 5).
float  Pow5(float  x) { float x2 = x * x; return x2 * x2 * x; }
float2 Pow5(float2 x) { float2 x2 = x * x; return x2 * x2 * x; }
float3 Pow5(float3 x) { float3 x2 = x * x; return x2 * x2 * x; }
float4 Pow5(float4 x) { float4 x2 = x * x; return x2 * x2 * x; }

// ─────────────────────────────────────────────────────────────────────────────
// sRGB Conversion — IEC 61966-2-1 exact transfer function
// ─────────────────────────────────────────────────────────────────────────────
// Uses pow(x, 1/2.4) exponent from the sRGB spec instead of the approximate
// pow(x, 1/2.2). The 1/2.4 exponent is exact in IEEE754 (0.41666...repeating
// but the spec mandates it), and critically, the two-branch structure with
// step()+lerp() eliminates the divergence-prone pow(x, 1/2.2) entirely for
// the linear segment. This matches UE5 Common.ush LinearToSrgbBranchless().

/// Converts linear-space color to sRGB using a pow-free approximation.
///
/// Avoids native pow() which compiles differently on FXC vs DXC,
/// causing brightness divergence between DX11 and DX12. Uses the
/// UE5 LinearToSrgbBranchless pattern: sqrt + rsqrt chain that
/// closely approximates pow(x, 1/2.4) with identical codegen on
/// both compilers.
///
/// Max error vs exact sRGB: < 0.6/255 — imperceptible.
///
/// # Parameters
/// - lin: linear-space RGB color (pre-tonemapped, in [0,1] range).
///
/// # Returns
/// sRGB-encoded color suitable for UNORM backbuffer output.
float3 LinearToSrgb(float3 lin) {
    // Attempt pow-free approximation of pow(x, 1/2.4) via sqrt chain:
    // pow(x, 1/2.4) ≈ sqrt(x) * rsqrt(sqrt(x))^(1/3) — but there's no
    // clean decomposition. Instead use two sqrt + lerp to match the curve:
    //   s = sqrt(lin), ss = sqrt(s)  → ss = lin^0.25
    //   Blend: 0.585122 * ss + 0.783677 * s - 0.368800 ≈ lin^(1/2.4)
    // This is the UE5 Common.ush approach (LinearToSrgbBranchingless).
    float3 s  = sqrt(max(lin, 0.0f));
    float3 ss = sqrt(s);
    float3 hi = 0.585122f * ss + 0.783677f * s - 0.368799f;
    float3 lo = lin * 12.92f;
    float3 t  = step(0.0031308f, lin);
    return lerp(lo, hi, t);
}

/// Converts sRGB-encoded color back to linear space.
///
/// Uses Pow2 + multiply to approximate pow(x, 2.4) without native pow(),
/// avoiding FXC/DXC divergence. pow(x, 2.4) = pow(x, 2) * pow(x, 0.4).
/// pow(x, 0.4) = pow(x, 2/5) = sqrt(pow(x, 4/5)) ≈ sqrt(x * sqrt(sqrt(x))).
///
/// # Parameters
/// - srgb: sRGB-encoded RGB color (e.g. from texture with no hardware decode).
///
/// # Returns
/// Linear-space color.
float3 SrgbToLinear(float3 srgb) {
    float3 lo = srgb / 12.92f;
    float3 v  = max((srgb + 0.055f) / 1.055f, 0.0f);
    // pow(v, 2.4) ≈ v^2 * v^0.4, where v^0.4 ≈ sqrt(v * sqrt(sqrt(v)))
    float3 v2 = v * v;
    float3 v04 = sqrt(v * sqrt(sqrt(max(v, 0.0f))));
    float3 hi = v2 * v04;
    float3 t  = step(0.04045f, srgb);
    return lerp(lo, hi, t);
}

#endif // MATH_UTIL_HLSLI
// ── end include: math_util.hlsli ──

static const float PI = 3.14159265359f;

/// Minimum roughness clamped before squaring to alpha, preventing a perfectly
/// smooth (singular) specular lobe that collapses to a delta function on GPU.
static const float MIN_ROUGHNESS = 0.04f;

// ─────────────────────────────────────────────────────────────────────────────
// ACES Filmic Tonemapping
// ─────────────────────────────────────────────────────────────────────────────

/// ACES filmic tonemapping curve (Narkowicz 2015 fit).
///
/// Maps HDR linear radiance to [0,1] LDR with a filmic S-curve that provides
/// better highlight rolloff and color preservation than per-channel Reinhard.
///
/// # Parameters
/// - x: linear HDR color (unbounded positive).
///
/// # Returns
/// Tonemapped LDR color in [0, 1].
float3 ACESFilm(float3 x) {
    float a = 2.51f;
    float b = 0.03f;
    float c = 2.43f;
    float d = 0.59f;
    float e = 0.14f;
    return saturate((x * (a * x + b)) / (x * (c * x + d) + e));
}

// ─────────────────────────────────────────────────────────────────────────────
// PBR Helper Functions
// ─────────────────────────────────────────────────────────────────────────────

/// GGX / Trowbridge-Reitz Normal Distribution Function.
///
/// Describes the statistical distribution of microfacet normals relative to
/// the macrosurface normal. A higher alpha (rougher surface) produces a wider,
/// dimmer highlight; a lower alpha produces a tight, bright specular peak.
///
/// D(h) = alpha^2 / (PI * ((N.H)^2 * (alpha^2 - 1) + 1)^2)
///
/// # Parameters
/// - NdotH: dot product of macrosurface normal N and half-vector H, in [0,1].
/// - alpha: perceptual roughness squared (r^2), in [MIN_ROUGHNESS^2, 1].
///
/// # Returns
/// Specular lobe density in [0, +inf). Integrates to 1 over the hemisphere.
float DistributionGGX(float NdotH, float alpha) {
    float a2    = alpha * alpha;
    float denom = NdotH * NdotH * (a2 - 1.0f) + 1.0f;
    return a2 / (PI * denom * denom);
}

/// Schlick-GGX single-direction geometry masking/shadowing term.
///
/// Approximates the fraction of microfacets visible from direction v that
/// are not shadowed or masked by other microfacets.
///
/// G1(v) = N.V / (N.V * (1 - k) + k)   where k = alpha / 2 for direct lighting
///
/// # Parameters
/// - NdotV: saturated dot product of N and the view or light direction.
/// - alpha: perceptual roughness squared (r^2).
///
/// # Returns
/// Scalar in (0, 1].
float GeometrySchlickGGX(float NdotV, float alpha) {
    // k remapping for direct (analytic) lights per Karis 2013 Real Shading in UE4
    float k = alpha / 2.0f;
    return NdotV / (NdotV * (1.0f - k) + k);
}

/// Smith combined geometry function for both view and light directions.
///
/// Multiplies two independent Schlick-GGX terms: one for the view direction
/// (masking) and one for the light direction (shadowing).
///
/// # Parameters
/// - NdotV: saturated dot(N, V).
/// - NdotL: saturated dot(N, L).
/// - alpha: perceptual roughness squared (r^2).
///
/// # Returns
/// Scalar geometry attenuation in (0, 1].
float GeometrySmith(float NdotV, float NdotL, float alpha) {
    return GeometrySchlickGGX(NdotV, alpha) * GeometrySchlickGGX(NdotL, alpha);
}

/// Schlick Fresnel approximation.
///
/// Computes the fraction of light reflected (vs. refracted) at a microfacet
/// surface. At grazing angles (cosTheta -> 0) the reflectance approaches 1
/// regardless of material.
///
/// F(v, h) = F0 + (1 - F0) * (1 - V.H)^5
///
/// # Parameters
/// - cosTheta: dot(V, H) — cosine of angle between view and half-vector.
/// - F0:       reflectance at normal incidence (0 deg angle of incidence).
///             Dielectric typical value: (0.04, 0.04, 0.04).
///             Metals: use the albedo color as F0.
///
/// # Returns
/// float3 Fresnel reflectance in [F0, 1].
float3 FresnelSchlick(float cosTheta, float3 F0) {
    return F0 + (1.0f - F0) * Pow5(saturate(1.0f - cosTheta));
}

/// Evaluates Cook-Torrance BRDF for a single light source.
///
/// Computes the full outgoing radiance from a single analytic light, including
/// both diffuse (Lambertian) and specular (GGX) lobes with energy conservation.
///
/// # Parameters
/// - N:         world-space surface normal (normalised).
/// - V:         world-space view direction (normalised, toward camera).
/// - L:         world-space light direction (normalised, toward light).
/// - radiance:  incoming light radiance (color * intensity * attenuation).
/// - albedo:    surface base color (linear).
/// - metallic:  metallic factor [0,1].
/// - alpha:     roughness^2 (PBR alpha parameter).
/// - F0:        reflectance at normal incidence.
///
/// # Returns
/// Outgoing radiance contribution from this light.
float3 EvaluateBRDF(float3 N, float3 V, float3 L, float3 radiance,
                    float3 albedo, float metallic, float alpha, float3 F0) {
    float3 H = normalize(V + L);

    float NdotL = max(dot(N, L), 0.001f);
    float NdotV = max(dot(N, V), 0.001f);
    float NdotH = saturate(dot(N, H));
    float VdotH = saturate(dot(V, H));

    // Cook-Torrance specular BRDF
    float  D = DistributionGGX(NdotH, alpha);
    float  G = GeometrySmith(NdotV, NdotL, alpha);
    float3 F = FresnelSchlick(VdotH, F0);

    // D*G*F / (4 * N.V * N.L) — the 4 is the half-vector Jacobian
    float3 specular = (D * G * F) / max(4.0f * NdotV * NdotL, 0.001f);

    // Energy conservation: kD zeroed for metals (no diffuse lobe)
    float3 kS = F;
    float3 kD = (1.0f - kS) * (1.0f - metallic);

    // Lambertian diffuse normalised by PI
    float3 diffuse = kD * albedo / PI;

    return (diffuse + specular) * radiance * NdotL;
}

#endif // PBR_COMMON_HLSLI
// ── end include: ../include/pbr_common.hlsli ──

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

static const float PI = 3.14159265359f;
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

    // ── Tonemap + gamma ──
    sky = ACESFilm(sky);
    sky = LinearToSrgb(sky);

    return float4(sky, 1.0f);
}
)hlsl";
} // namespace uz::generated
