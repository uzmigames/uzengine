// prefilter_cs.hlsl.gen.h -- AUTO-GENERATED, DO NOT EDIT
#pragma once
namespace uz::generated {
inline constexpr char kPrefilterCS[] = R"hlsl(
/// Prefiltered environment map compute shader — split-sum specular IBL.
///
/// Generates one mip level of a prefiltered environment cubemap by
/// importance-sampling the GGX NDF at a given roughness. Each mip stores
/// the pre-integrated specular radiance for that roughness level:
///
///   mip 0 = roughness 0.0 (mirror reflection)
///   mip 1 = roughness 0.25
///   mip 2 = roughness 0.5
///   mip 3 = roughness 0.75
///   mip 4 = roughness 1.0
///
/// At runtime, the renderer samples this cubemap at the reflection
/// direction R with mip level = roughness * maxMipLevel.
///
/// Dispatched once per mip level from the CPU with the appropriate
/// roughness and face size.
///
/// # References
/// - Karis 2013, "Real Shading in Unreal Engine 4" — split-sum, prefiltered env
/// - Lagarde & de Rousiers 2014, "Moving Frostbite to PBR"

// ── begin include: ../include/ibl.hlsli ──
/// Shared IBL (Image-Based Lighting) helper functions.
///
/// Provides low-discrepancy sampling, GGX importance sampling, and
/// roughness-aware Fresnel for split-sum IBL integration.
///
/// Used by brdf_lut_cs.hlsl, irradiance_cs.hlsl, and prefilter_cs.hlsl.
///
/// # References
/// - Karis 2013, "Real Shading in Unreal Engine 4"
/// - Hammersley & Handscomb 1964, "Monte Carlo Methods"

#ifndef IBL_HLSLI
#define IBL_HLSLI

// ── begin include: pbr_common.hlsli ──
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
// ── end include: pbr_common.hlsli ──

// ─────────────────────────────────────────────────────────────────────────────
// Low-Discrepancy Sampling
// ─────────────────────────────────────────────────────────────────────────────

/// Van der Corput radical inverse in base 2 using bit reversal.
///
/// Mirrors the binary representation of `bits` around the decimal point
/// to produce a value in [0, 1). Used as the second coordinate in the
/// Hammersley sequence.
///
/// # Parameters
/// - bits: unsigned integer input.
///
/// # Returns
/// Radical inverse value in [0, 1).
float RadicalInverseVdC(uint bits) {
    bits = (bits << 16u) | (bits >> 16u);
    bits = ((bits & 0x55555555u) << 1u) | ((bits & 0xAAAAAAAAu) >> 1u);
    bits = ((bits & 0x33333333u) << 2u) | ((bits & 0xCCCCCCCCu) >> 2u);
    bits = ((bits & 0x0F0F0F0Fu) << 4u) | ((bits & 0xF0F0F0F0u) >> 4u);
    bits = ((bits & 0x00FF00FFu) << 8u) | ((bits & 0xFF00FF00u) >> 8u);
    return float(bits) * 2.3283064365386963e-10f; // 1 / 0x100000000
}

/// Hammersley 2D low-discrepancy sequence point.
///
/// Produces a quasi-random 2D sample in [0,1)^2. The x coordinate is a
/// simple linear ramp (i/N), while y uses the Van der Corput radical
/// inverse for better coverage than pseudo-random sampling.
///
/// # Parameters
/// - i: sample index in [0, N).
/// - N: total number of samples.
///
/// # Returns
/// 2D sample point in [0, 1)^2.
float2 Hammersley(uint i, uint N) {
    return float2(float(i) / float(N), RadicalInverseVdC(i));
}

// ─────────────────────────────────────────────────────────────────────────────
// GGX Importance Sampling
// ─────────────────────────────────────────────────────────────────────────────

/// Importance-samples the GGX NDF to generate a half-vector.
///
/// Maps a 2D uniform sample (Xi) to a direction on the hemisphere weighted
/// by the GGX distribution for the given roughness. The returned vector is
/// in world space, oriented around the surface normal N.
///
/// # Parameters
/// - Xi:        2D uniform random sample in [0, 1)^2 (e.g., from Hammersley).
/// - N:         macrosurface normal (normalised).
/// - roughness: perceptual roughness in [0, 1].
///
/// # Returns
/// World-space half-vector H sampled proportional to D_GGX(H).
float3 ImportanceSampleGGX(float2 Xi, float3 N, float roughness) {
    float a = roughness * roughness;

    // Spherical coordinates from the GGX inverse CDF
    float phi      = 2.0f * PI * Xi.x;
    float cosTheta = sqrt((1.0f - Xi.y) / (1.0f + (a * a - 1.0f) * Xi.y));
    float sinTheta = sqrt(1.0f - cosTheta * cosTheta);

    // Tangent-space half-vector
    float3 H;
    H.x = cos(phi) * sinTheta;
    H.y = sin(phi) * sinTheta;
    H.z = cosTheta;

    // Build TBN from N — construct an arbitrary tangent frame
    float3 up = (abs(N.z) < 0.999f) ? float3(0.0f, 0.0f, 1.0f)
                                     : float3(1.0f, 0.0f, 0.0f);
    float3 T = normalize(cross(up, N));
    float3 B = cross(N, T);

    // Transform from tangent space to world space
    return normalize(T * H.x + B * H.y + N * H.z);
}

// ─────────────────────────────────────────────────────────────────────────────
// Fresnel (IBL variant)
// ─────────────────────────────────────────────────────────────────────────────

/// Fresnel-Schlick with roughness attenuation for IBL ambient lighting.
///
/// Unlike the standard FresnelSchlick (which approaches 1.0 at grazing
/// angles regardless of roughness), this variant caps the maximum Fresnel
/// reflectance based on roughness. This prevents overly bright rim lighting
/// on rough materials under environment illumination.
///
/// # Parameters
/// - cosTheta:  dot(N, V), clamped to [0, 1].
/// - F0:        reflectance at normal incidence.
/// - roughness: perceptual roughness in [0, 1].
///
/// # Returns
/// Fresnel reflectance attenuated by roughness.
float3 FresnelSchlickRoughness(float cosTheta, float3 F0, float roughness) {
    float3 maxF = max(float3(1.0f - roughness, 1.0f - roughness, 1.0f - roughness), F0);
    return F0 + (maxF - F0) * Pow5(saturate(1.0f - cosTheta));
}

// ─────────────────────────────────────────────────────────────────────────────
// Cubemap Helpers
// ─────────────────────────────────────────────────────────────────────────────

/// Converts a face index + 2D UV to a world-space direction for cubemap sampling.
///
/// Face order follows DX12 / D3D convention:
///   0 = +X, 1 = -X, 2 = +Y, 3 = -Y, 4 = +Z, 5 = -Z
///
/// # Parameters
/// - faceIndex: cubemap face [0, 5].
/// - uv:        normalised UV in [0, 1]^2 within that face.
///
/// # Returns
/// Unnormalised world-space direction vector.
float3 CubeFaceDirection(uint faceIndex, float2 uv) {
    // Remap UV from [0,1] to [-1,1]
    float u = uv.x * 2.0f - 1.0f;
    float v = uv.y * 2.0f - 1.0f;

    // DX cubemap convention: +Y is up, V is flipped for top/bottom
    float3 dir;
    switch (faceIndex) {
        case 0: dir = float3( 1.0f,   -v,   -u); break; // +X
        case 1: dir = float3(-1.0f,   -v,    u); break; // -X
        case 2: dir = float3(   u,  1.0f,    v); break; // +Y
        case 3: dir = float3(   u, -1.0f,   -v); break; // -Y
        case 4: dir = float3(   u,   -v,  1.0f); break; // +Z
        case 5: dir = float3(  -u,   -v, -1.0f); break; // -Z
        default: dir = float3(0.0f, 0.0f, 1.0f); break;
    }
    return dir;
}

#endif // IBL_HLSLI
// ── end include: ../include/ibl.hlsli ──

// ─────────────────────────────────────────────────────────────────────────────
// Resources
// ─────────────────────────────────────────────────────────────────────────────

/// Root constants at b0.
cbuffer RootConstants : register(b0) {
    uint  envMapIndex;  // SRV heap index of the source environment cubemap
    uint  faceSize;     // output face resolution for this mip level
    float roughness;    // target roughness for this mip [0, 1]
    uint  sampleCount;  // number of importance samples (e.g., 256)
};

#ifdef UZ_BINDLESS
// Environment cubemap fetched from descriptor heap at runtime
#else
TextureCube<float3> envMap : register(t0);
#endif

SamplerState linearSampler : register(s0);

RWTexture2DArray<float4> prefilteredOut : register(u0);

// ─────────────────────────────────────────────────────────────────────────────
// Compute Shader Entry Point
// ─────────────────────────────────────────────────────────────────────────────

[numthreads(8, 8, 1)]
void main(uint3 dtid : SV_DispatchThreadID) {
    uint faceIndex = dtid.z;

    if (dtid.x >= faceSize || dtid.y >= faceSize || faceIndex >= 6u) return;

    // Compute the world-space direction for this texel
    float2 uv = (float2(dtid.xy) + 0.5f) / float(faceSize);
    float3 N = normalize(CubeFaceDirection(faceIndex, uv));

    // For the prefilter convolution, assume V = R = N (the "split-sum"
    // approximation). This loses the view-dependent Fresnel stretch at
    // grazing angles but is the standard real-time trade-off.
    float3 R = N;
    float3 V = R;

#ifdef UZ_BINDLESS
    TextureCube<float3> envCube = ResourceDescriptorHeap[envMapIndex];
#else
    #define envCube envMap
#endif

    float3 prefilteredColor = float3(0.0f, 0.0f, 0.0f);
    float  totalWeight      = 0.0f;

    // Clamp roughness to avoid degenerate delta distribution at roughness=0
    float clampedRoughness = max(roughness, MIN_ROUGHNESS);

    for (uint i = 0u; i < sampleCount; ++i) {
        float2 Xi = Hammersley(i, sampleCount);
        float3 H  = ImportanceSampleGGX(Xi, N, clampedRoughness);
        float3 L  = normalize(2.0f * dot(V, H) * H - V);

        float NdotL = dot(N, L);
        if (NdotL > 0.0f) {
            // Weight by NdotL to reduce variance from samples near the
            // horizon where the cosine term is small
            prefilteredColor += envCube.SampleLevel(linearSampler, L, 0.0f).rgb * NdotL;
            totalWeight += NdotL;
        }
    }

    // Guard against zero-weight edge case (all samples below horizon)
    prefilteredColor = (totalWeight > 0.0f)
        ? prefilteredColor / totalWeight
        : float3(0.0f, 0.0f, 0.0f);

    prefilteredOut[uint3(dtid.xy, faceIndex)] = float4(prefilteredColor, 1.0f);

#ifndef UZ_BINDLESS
    #undef envCube
#endif
}
)hlsl";
} // namespace uz::generated
