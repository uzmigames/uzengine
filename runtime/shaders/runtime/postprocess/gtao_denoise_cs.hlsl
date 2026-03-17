/// GTAO Spatial Denoise — edge-preserving bilateral blur.
///
/// Applies a depth-aware spatial filter to the raw GTAO output to reduce
/// the high-frequency noise from the per-pixel jittered sampling pattern.
/// Uses a 4x4 box filter kernel (matching UE5's 4x4 random texture tiling)
/// with bilateral depth weights to preserve edges at silhouettes.
///
/// # Resources
/// - t0: Raw AO texture from GTAO pass (R8_UNORM) [SM5]
/// - t1: Depth buffer (R32_FLOAT) for bilateral weighting [SM5]
/// - u0: Filtered AO output (same format as input)
/// - b0: Root constants (8 DWORDs — texel size, near/far, SRV indices)
///
/// @source UE5 PostProcessAmbientOcclusion.usf:510-535 — MainSSAOSmoothCS
/// @source UE5 PostProcessAmbientOcclusionCommon.ush:60-63 — ComputeDepthSimilarity
/// @deviations Extended from UE5's simple box filter to include bilateral depth
///             weighting for better edge preservation.

#include "../include/math_util.hlsli"

// ─────────────────────────────────────────────────────────────────────────────
// Root Constants (b0, 8 DWORDs = 32 bytes)
// ─────────────────────────────────────────────────────────────────────────────

cbuffer DenoiseParams : register(b0) {
    float texelSizeX;        ///< 1.0 / width.
    float texelSizeY;        ///< 1.0 / height.
    float nearPlane;         ///< Camera near plane for depth linearization.
    float farPlane;          ///< Camera far plane for depth linearization.
    uint  rawAOSrvIndex;     ///< SRV index for raw GTAO texture (bindless).
    uint  depthSrvIndex;     ///< SRV index for depth buffer (bindless).
    uint  _pad0;
    uint  _pad1;
};

// ─────────────────────────────────────────────────────────────────────────────
// Resources
// ─────────────────────────────────────────────────────────────────────────────

#ifndef UZ_BINDLESS
Texture2D<float>   rawAO       : register(t0); ///< Raw GTAO output.
Texture2D<float>   depthBuffer : register(t1); ///< Scene depth (R32_FLOAT).
#endif

RWTexture2D<float> filteredAO  : register(u0); ///< Filtered AO output.

SamplerState pointSampler : register(s0);

// ─────────────────────────────────────────────────────────────────────────────
// Helper Functions
// ─────────────────────────────────────────────────────────────────────────────

/// Sample raw AO texture.
float SampleRawAO(float2 uv) {
#ifdef UZ_BINDLESS
    Texture2D<float> aoTex = ResourceDescriptorHeap[rawAOSrvIndex];
    return aoTex.SampleLevel(pointSampler, uv, 0).r;
#else
    return rawAO.SampleLevel(pointSampler, uv, 0).r;
#endif
}

/// Sample depth texture.
float SampleDepth(float2 uv) {
#ifdef UZ_BINDLESS
    Texture2D<float> depthTex = ResourceDescriptorHeap[depthSrvIndex];
    return depthTex.SampleLevel(pointSampler, uv, 0).r;
#else
    return depthBuffer.SampleLevel(pointSampler, uv, 0).r;
#endif
}

/// Linearize hardware depth to view-space Z.
float LinearizeDepth(float deviceZ) {
    return nearPlane * farPlane / (farPlane - deviceZ * (farPlane - nearPlane));
}

/// Depth similarity weight — 1.0 for same depth, 0.0 for very different.
/// @source UE5 PostProcessAmbientOcclusionCommon.ush:60-63 — ComputeDepthSimilarity
/// @constants 0.003 (UE5 PostProcessAmbientOcclusionCommon.ush:183)
/// @deviations NONE
float ComputeDepthSimilarity(float depthA, float depthB) {
    return saturate(1.0f - abs(depthA - depthB) * 0.003f);
}

// ─────────────────────────────────────────────────────────────────────────────
// Main Compute Shader
// ─────────────────────────────────────────────────────────────────────────────

/// 4x4 bilateral blur kernel.
/// @source UE5 PostProcessAmbientOcclusion.usf:528-534 — 4 bilinear samples
/// @deviations Added bilateral depth weighting

[numthreads(8, 8, 1)]
void main(
    uint2 groupId        : SV_GroupID,
    uint2 dispatchId     : SV_DispatchThreadID,
    uint2 groupThreadId  : SV_GroupThreadID)
{
    float2 texelSize = float2(texelSizeX, texelSizeY);
    float2 screenSize = 1.0f / texelSize;

    if (dispatchId.x >= (uint)screenSize.x || dispatchId.y >= (uint)screenSize.y) {
        return;
    }

    float2 uv = ((float2)dispatchId + 0.5f) * texelSize;

    float centerDepth = LinearizeDepth(SampleDepth(uv));

    // ── 4x4 bilateral blur via 4 bilinear taps ──
    /// @source UE5 PostProcessAmbientOcclusion.usf:529-533
    float weightSum = 0.0f;
    float aoSum = 0.0f;

    static const float2 offsets[4] = {
        float2( 0.0f, 0.0f),
        float2( 2.0f, 0.0f),
        float2( 0.0f, 2.0f),
        float2( 2.0f, 2.0f)
    };

    [unroll]
    for (uint i = 0; i < 4; i++) {
        float2 sampleUV = uv + offsets[i] * texelSize;
        sampleUV = clamp(sampleUV, texelSize * 0.5f, 1.0f - texelSize * 0.5f);

        float sampleAO = SampleRawAO(sampleUV);
        float sampleDepth = LinearizeDepth(SampleDepth(sampleUV));

        float depthWeight = ComputeDepthSimilarity(centerDepth, sampleDepth);
        aoSum += sampleAO * depthWeight;
        weightSum += depthWeight;
    }

    filteredAO[dispatchId] = aoSum / max(weightSum, 0.0001f);
}
