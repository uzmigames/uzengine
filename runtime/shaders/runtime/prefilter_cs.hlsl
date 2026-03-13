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

#include "include/ibl.hlsli"

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
