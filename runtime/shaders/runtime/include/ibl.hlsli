/// IBL utilities — importance sampling for cubemap baking.
/// @ref Karis 2013, Hammersley sequences
#ifndef IBL_HLSLI
#define IBL_HLSLI
#include "math_util.hlsli"

static const float IBL_MIN_ROUGHNESS = 0.045;

float RadicalInverseVdC(uint bits) {
    bits = (bits << 16u) | (bits >> 16u);
    bits = ((bits & 0x55555555u) << 1u) | ((bits & 0xAAAAAAAAu) >> 1u);
    bits = ((bits & 0x33333333u) << 2u) | ((bits & 0xCCCCCCCCu) >> 2u);
    bits = ((bits & 0x0F0F0F0Fu) << 4u) | ((bits & 0xF0F0F0F0u) >> 4u);
    bits = ((bits & 0x00FF00FFu) << 8u) | ((bits & 0xFF00FF00u) >> 8u);
    return float(bits) * 2.3283064365386963e-10;
}

float2 Hammersley(uint i, uint N) {
    return float2(float(i) / float(N), RadicalInverseVdC(i));
}

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

// Legacy alias
#define MIN_ROUGHNESS IBL_MIN_ROUGHNESS

#endif
