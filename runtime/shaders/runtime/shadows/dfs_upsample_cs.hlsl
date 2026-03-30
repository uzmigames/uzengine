/// DFS Bilateral Upsample — depth-aware upsampling of half-res DFS shadow output.
///
/// Reads 4 nearest half-res shadow samples (2x2 bilinear neighborhood), weights
/// them by spatial bilinear position AND depth similarity to the full-res depth
/// at the output pixel. This preserves sharp shadow edges at geometry boundaries
/// while smoothly interpolating in flat regions.
///
/// The reference technique implements this as a pixel shader (bilateral upsample pass).
/// We implement the same algorithm as a compute shader since our DFS pipeline is
/// entirely compute-based.
///
/// # Pipeline
/// 1. Compute full-res screen UV from dispatch thread ID
/// 2. Sample full-res depth buffer, linearize to view-space distance
/// 3. Compute the 4 low-res texel centers covering this full-res pixel
/// 4. For each: sample shadow factor + stored depth
/// 5. Compute bilinear spatial weight * depth proximity weight
/// 6. Reject samples outside the valid DFS depth range (unless shadowed)
/// 7. Normalize and output the weighted shadow factor
///
/// # Inputs
/// - Half-res DFS output (t0): RG16F — (shadow_factor, scene_depth)
/// - Full-res depth buffer (t1): R32_FLOAT (reversed-Z device depth)
///
/// # Output
/// - RWTexture2D<float> (u0): R32_FLOAT — upsampled shadow factor
///
/// # Thread Group
/// [8, 8, 1] — one thread per full-res output pixel
///
/// @ref Bilateral upsampling for shadow factors — depth-aware 2x2 interpolation
/// @ref Wright 2015 "Dynamic Occlusion with SDFs" — DFS upsample pass
/// @ref Wright 2015 "Dynamic Occlusion with Signed Distance Fields" — bilateral upsample
/// @constants Epsilon=0.0001, BILATERAL_UPSAMPLE=1
/// @deviations Compute shader instead of pixel shader. UV computation uses
///             dispatch thread ID instead of SV_POSITION. Same bilateral math.

#include "../include/depth_common.hlsli"

// ─────────────────────────────────────────────────────────────────────────────
// Root Constants (b0, compute root signature)
// ─────────────────────────────────────────────────────────────────────────────

/// @ref Wright 2015 "DFS" — upsample:815-821 — UpsamplePS parameters
/// @constants OneOverDownsampleFactor=0.5 for 2x downsample
/// @deviations Combined into root constants cbuffer for compute dispatch
cbuffer UpsampleParams : register(b0) {
    uint   g_OutputWidth;            // Full-res output width
    uint   g_OutputHeight;           // Full-res output height
    float  g_OneOverDownsampleFactor; // 1.0 / downsample_factor (0.5 for 2x)
    float  g_NearPlane;              // Camera near plane for depth linearization
    float  g_FarPlane;               // Camera far plane for depth linearization
    float  g_MinDepth;               // Min depth for valid DFS shadows
    float  g_MaxDepth;               // Max depth for valid DFS shadows
    uint   _pad0;
};

// ─────────────────────────────────────────────────────────────────────────────
// Resources
// ─────────────────────────────────────────────────────────────────────────────

Texture2D<float2>  g_HalfResShadow : register(t0);  // Half-res DFS output (shadow, depth)
Texture2D<float>   g_DepthBuffer   : register(t1);  // Full-res depth buffer (reversed-Z)

RWTexture2D<float> g_OutputShadow  : register(u0);  // Full-res upsampled shadow

SamplerState       g_PointSampler  : register(s0);  // Point sampler for explicit texel fetch

// ─────────────────────────────────────────────────────────────────────────────
// Bilateral Upsample
// ─────────────────────────────────────────────────────────────────────────────

/// Implements the bilateral upsample from Wright 2015 "Dynamic Occlusion with Signed Distance Fields".
///
/// The algorithm:
/// 1. Find the 4 low-res texels covering this full-res pixel (2x2 neighborhood)
/// 2. Compute bilinear interpolation weights from the fractional position
/// 3. Compute depth-based weights: 1/(|cornerDepth - sceneDepth| + epsilon)
/// 4. Reject corners outside the DFS depth range (unless they are actually shadowed)
/// 5. Combine: finalWeight = bilinearWeight * depthWeight * validityMask
/// 6. Normalize: result = dot(shadowFactors, finalWeights) / sum(finalWeights)
///
/// The depth rejection (step 4) matches reference's logic: a sample is valid if either
/// (a) its depth is within [MinDepth, MaxDepth], OR (b) it is actually shadowed
/// (shadow < 1.0). This prevents unshadowed sky/distant pixels from bleeding
/// white into valid shadow regions, while allowing shadowed pixels from heightfield
/// traces that may be outside the standard range.
///
/// @ref Wright 2015 "Dynamic Occlusion with Signed Distance Fields" — bilateral upsample path
/// @constants Epsilon=0.0001
/// @deviations NONE — exact same bilateral math, just in compute form

[numthreads(8, 8, 1)]
void main(uint3 DTid : SV_DispatchThreadID) {
    // Skip out-of-bounds threads
    if (DTid.x >= g_OutputWidth || DTid.y >= g_OutputHeight) {
        return;
    }

    // Compute full-res buffer UV (center of pixel)
    /// @ref Bilateral upsample —27 — BufferUV = SvPositionToBufferUV(SVPos)
    float2 fullResSize = float2(g_OutputWidth, g_OutputHeight);
    float2 bufferUV = (float2(DTid.xy) + 0.5f) / fullResSize;

    // Sample full-res scene depth and linearize
    /// @ref Bilateral upsample —34 — SceneDepth = CalcSceneDepth(BufferUV)
    float rawDepth = g_DepthBuffer.SampleLevel(g_PointSampler, bufferUV, 0).r;
    float sceneDepth = LinearizeDepthReversedZ(rawDepth, g_NearPlane, g_FarPlane);

    // Compute low-res buffer dimensions
    /// @ref Bilateral upsample —39 — LowResBufferSize = floor(ViewSize * OneOverDownsampleFactor)
    float2 lowResBufferSize = floor(fullResSize * g_OneOverDownsampleFactor);
    float2 lowResTexelSize = 1.0f / lowResBufferSize;

    // The shadow UV is the same as buffer UV (no scissor rect offset in our case)
    float2 shadowFactorsUV = bufferUV;

    // Find the top-left corner of the 2x2 low-res neighborhood
    /// @ref Bilateral upsample —45 — Corner00UVBase = floor(UV * LowResSize) / LowResSize
    float2 corner00UVBase = floor(shadowFactorsUV * lowResBufferSize) / lowResBufferSize;

    // Compute bilinear weights: fractional position within the low-res texel
    // offsets by half a full-res texel because rays are traced from the
    // full-res texel at the top-left of each low-res tile.
    /// @ref Bilateral upsample —48-49 — BilinearWeights calculation
    float2 fullResTexelSize = 1.0f / fullResSize;
    float2 corner00UV = corner00UVBase + 0.5f * fullResTexelSize;
    float2 bilinearWeights = saturate((shadowFactorsUV - corner00UV) * lowResBufferSize);

    // Sample the 4 low-res texels at their centers
    /// @ref Bilateral upsample —52-57 — 4 texture samples
    float2 corner00SampleUV = corner00UVBase + 0.5f * lowResTexelSize;

    // Clamp to maximum valid UV to prevent sampling beyond the shadow texture
    // uses ShadowFactorsUVBilinearMax; we clamp to the last texel center
    float2 uvMax = 1.0f - 0.5f * lowResTexelSize;

    float2 textureValues00 = g_HalfResShadow.SampleLevel(g_PointSampler, min(corner00SampleUV, uvMax), 0);
    float2 textureValues10 = g_HalfResShadow.SampleLevel(g_PointSampler, min(corner00SampleUV + float2(lowResTexelSize.x, 0), uvMax), 0);
    float2 textureValues01 = g_HalfResShadow.SampleLevel(g_PointSampler, min(corner00SampleUV + float2(0, lowResTexelSize.y), uvMax), 0);
    float2 textureValues11 = g_HalfResShadow.SampleLevel(g_PointSampler, min(corner00SampleUV + lowResTexelSize, uvMax), 0);

    // Extract shadow factors and depths from the 4 samples
    /// @ref Bilateral upsample —67-68 — ShadowFactors and CornerDepths
    float4 shadowFactors = float4(textureValues00.x, textureValues10.x, textureValues01.x, textureValues11.x);
    float4 cornerDepths  = abs(float4(textureValues00.y, textureValues10.y, textureValues01.y, textureValues11.y));

    // Compute bilinear spatial weights (standard 2x2 bilinear interpolation)
    /// @ref Bilateral upsample —59-63 — CornerWeights
    float4 cornerWeights = float4(
        (1.0f - bilinearWeights.y) * (1.0f - bilinearWeights.x),
        (1.0f - bilinearWeights.y) * bilinearWeights.x,
        bilinearWeights.y * (1.0f - bilinearWeights.x),
        bilinearWeights.y * bilinearWeights.x);

    /// @ref Bilateral upsample —65 — Epsilon = .0001f
    float epsilon = 0.0001f;

    // Depth validity: a corner is valid if its depth is within the DFS range
    // OR if it is actually shadowed (shadow < 1.0). This prevents unshadowed
    // sky pixels from contaminating shadow regions while allowing shadowed
    // pixels from heightfield traces outside the normal range.
    /// @ref Bilateral upsample —72-76 — CornerDepthsValid
    float4 minDepthVec = g_MinDepth.xxxx;
    float4 maxDepthVec = g_MaxDepth.xxxx;
    float4 cornerDepthsValid =
        float4(
            (minDepthVec.x < cornerDepths.x && cornerDepths.x < maxDepthVec.x) || (shadowFactors.x < 1.0f) ? 1.0f : 0.0f,
            (minDepthVec.y < cornerDepths.y && cornerDepths.y < maxDepthVec.y) || (shadowFactors.y < 1.0f) ? 1.0f : 0.0f,
            (minDepthVec.z < cornerDepths.z && cornerDepths.z < maxDepthVec.z) || (shadowFactors.z < 1.0f) ? 1.0f : 0.0f,
            (minDepthVec.w < cornerDepths.w && cornerDepths.w < maxDepthVec.w) || (shadowFactors.w < 1.0f) ? 1.0f : 0.0f);

    // Depth proximity weights: inverse of absolute depth difference
    // Closer depths get higher weight — this is the bilateral filter kernel
    /// @ref Bilateral upsample —77 — DepthWeights = 1/(|CornerDepths - SceneDepth| + Epsilon)
    float4 depthWeights = 1.0f / (abs(cornerDepths - sceneDepth.xxxx) + epsilon);

    // Final weight = spatial bilinear * depth validity * depth proximity
    /// @ref Bilateral upsample —78 — FinalWeights = CornerWeights * CornerDepthsValid * DepthWeights
    float4 finalWeights = cornerWeights * cornerDepthsValid * depthWeights;

    // Normalize and compute weighted shadow factor
    /// @ref Bilateral upsample —79-87 — TotalWeight, InterpolatedResult
    float totalWeight = dot(finalWeights, float4(1, 1, 1, 1));

    float interpolatedResult = 1.0f;
    if (totalWeight != 0.0f) {
        interpolatedResult = dot(shadowFactors, finalWeights) / totalWeight;
    }

    g_OutputShadow[DTid.xy] = interpolatedResult;
}
