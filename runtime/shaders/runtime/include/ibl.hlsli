/// IBL utilities — importance sampling for cubemap baking.
/// @ref Karis 2013, Hammersley sequences
#ifndef IBL_HLSLI
#define IBL_HLSLI
#include "math_util.hlsli"
#include "sampling.hlsli"

float3 ImportanceSampleGGX(float2 Xi, float3 N, float roughness) {
    float a = roughness * roughness;
    float phi = 2.0 * PI * Xi.x;
    float cosT = sqrt((1.0 - Xi.y) / (1.0 + (a * a - 1.0) * Xi.y));
    float sinT = sqrt(1.0 - cosT * cosT);

    float3 H = float3(cos(phi) * sinT, sin(phi) * sinT, cosT);

    float3 up = abs(N.z) < 0.999 ? float3(0, 0, 1) : float3(1, 0, 0);
    float3 T = normalize(cross(up, N));
    float3 B = cross(N, T);

    return normalize(T * H.x + B * H.y + N * H.z);
}

float3 CubeFaceDirection(uint face, float2 uv) {
    float u = uv.x * 2.0 - 1.0, v = uv.y * 2.0 - 1.0;
    switch (face) {
        case 0: return float3( 1, -v, -u);
        case 1: return float3(-1, -v,  u);
        case 2: return float3( u,  1,  v);
        case 3: return float3( u, -1, -v);
        case 4: return float3( u, -v,  1);
        case 5: return float3(-u, -v, -1);
        default: return float3(0, 0, 1);
    }
}

/// Converts a world-space direction (Z-up) to DX cubemap convention (Y-up).
/// UzEngine uses Z-up world space, but DX12 TextureCube hardware sampling
/// expects Y-up. Apply this before any TextureCube.SampleLevel() call.
/// Mapping: world (X,Y,Z) → cubemap (X,-Z,Y)
float3 WorldToCubemap(float3 dir) {
    return float3(dir.x, -dir.z, dir.y);
}

// ─────────────────────────────────────────────────────────────────────────────
// LOD / Solid Angle Helpers
// ─────────────────────────────────────────────────────────────────────────────

/// Solid angle subtended by a single importance sample.
/// @ref Pharr, Jakob, Humphreys 2016 "PBR" — Monte Carlo integration
/// @constants omega_s = 1 / (N * pdf)
float SolidAngleSample(uint numSamples, float pdf) {
    return 1.0f / (float(numSamples) * max(pdf, 1e-8f));
}

/// Compute cubemap mip level for importance-sampled environment lookup.
/// Uses the ratio of sample solid angle to texel solid angle for proper
/// pre-filtering that avoids undersampling at low sample counts.
/// @ref Pharr, Jakob, Humphreys 2016 — Section 13.10, env map filtering
/// @ref Karis 2013 — split-sum IBL with roughness-dependent mip selection
/// @constants omega_p = 4*PI / (6 * texWidth^2) — solid angle of one cubemap texel
float ComputeEnvLOD(float pdf, uint texWidth, uint numSamples) {
    float omegaS = SolidAngleSample(numSamples, pdf);
    float omegaP = 4.0f * PI / (6.0f * float(texWidth) * float(texWidth));
    return max(0.5f * log2(omegaS / omegaP), 0.0f);
}

#endif
