/// Cloud Temporal Reprojection Compute Shader — stabilizes half-res cloud rendering.
///
/// Reprojects the current frame's half-resolution cloud output using the previous
/// frame's view-projection matrix, then blends with the history buffer using an
/// exponential moving average. Handles disocclusion (off-screen reprojection,
/// depth discontinuities) by increasing the blend factor toward the current frame.
///
/// uses a more sophisticated checkerboard reconstruction approach
/// (VolumetricRenderTarget.usf) with per-pixel new-sample vs history decisions.
/// Our temporal filter is a simplified EMA approach suitable for the engine's
/// current half-res cloud pipeline.
///
/// @ref ReconstructVolumetricRenderTargetPS
/// @ref ClipToPrevClip reprojection
/// @ref bValidPreviousUVs check
/// @ref MaxDepthKm = 60000 threshold
/// @ref ThresholdToNewSampleKm = 2.0
/// @ref temporal blend: Prev + Factor * (Curr - Prev)
///
/// @constants
///   Default blend factor: 0.05 (accumulate ~20 frames for stable result)
///   Disocclusion blend (depth discontinuity): 0.3
///   Off-screen reprojection: 1.0 (use current frame only)
///   Depth discontinuity threshold: 2.0 km (reference VolumetricRenderTarget.usf:318)
///   MaxDepthKm safe threshold: 60000.0 (reference VolumetricRenderTarget.usf:167)
///
/// @deviations
///   - the reference uses checkerboard pattern (DownSampleFactor x DownSampleFactor tiles,
///     rotating CurrentTracingPixelOffset each frame) with per-pixel new/history
///     decisions. We use a simpler full-frame EMA blend.
///   - reference samples cloud front depth for reprojection quality; we reproject using
///     the depth buffer as a proxy.
///   - the reference applies HistoryPreExposureCorrection; we assume stable exposure.
///   - Neighborhood clamping (AABB clamp in YCoCg) is not implemented; the reference
///     VolumetricRenderTarget.usf has USE_YCOCG=0 by default (line 130).


// ─── Samplers ───────────────────────────────────────────────────────────────
SamplerState LinearClampSampler : register(s0);


// ─── Constant Buffer ────────────────────────────────────────────────────────

/// @ref previous frame textures + matrices
cbuffer CloudTemporalCB : register(b2)
{
    float4x4 prevViewProj;      ///< Previous frame's view-projection matrix
    float4x4 invViewProj;       ///< Current frame's inverse view-projection matrix
    uint     currentSrvIndex;   ///< Bindless SRV: current frame cloud texture (RGBA16F)
    uint     historySrvIndex;   ///< Bindless SRV: previous frame cloud texture (RGBA16F)
    uint     depthSrvIndex;     ///< Bindless SRV: current depth buffer
    float    blendFactor;       ///< Base temporal blend factor (default 0.05)
    float    outputWidth;       ///< Output texture width
    float    outputHeight;      ///< Output texture height
    float2   _pad;
};


// ─── Output ─────────────────────────────────────────────────────────────────
RWTexture2D<float4> temporalOutput : register(u0); ///< RGBA16F cloud color + alpha


// ═══════════════════════════════════════════════════════════════════════════
// Main Compute Shader
// ═══════════════════════════════════════════════════════════════════════════

[numthreads(8, 8, 1)]
void main(uint2 DTid : SV_DispatchThreadID)
{
    /// Bounds check
    if (DTid.x >= (uint)outputWidth || DTid.y >= (uint)outputHeight)
        return;

    /// ─── Step 1: Load current frame data ────────────────────────────────
    Texture2D<float4> currentTex = ResourceDescriptorHeap[currentSrvIndex];
    Texture2D<float4> historyTex = ResourceDescriptorHeap[historySrvIndex];
    Texture2D<float>  depthTex   = ResourceDescriptorHeap[depthSrvIndex];

    float2 texelSize = 1.0 / float2(outputWidth, outputHeight);
    float2 uv = (float2(DTid) + 0.5) * texelSize;

    /// Sample current frame cloud result
    float4 currentColor = currentTex.SampleLevel(LinearClampSampler, uv, 0);

    /// ─── Step 2: Reproject current pixel to previous frame UV ───────────
    /// Reconstruct world position from current screen UV + depth
    float2 ndc = uv * 2.0 - 1.0;
    ndc.y = -ndc.y;

    /// Sample depth buffer for reprojection
    float deviceZ = depthTex.SampleLevel(LinearClampSampler, uv, 0);

    /// Unproject to world space using current frame's inverse view-projection
    float4 worldPos = mul(float4(ndc, deviceZ, 1.0), invViewProj);
    worldPos.xyz /= worldPos.w;

    /// Project world position into previous frame's clip space
    /// @ref CurrClip -> PrevClip via ClipToPrevClip
    float4 prevClip = mul(float4(worldPos.xyz, 1.0), prevViewProj);
    float2 prevNdc = prevClip.xy / prevClip.w;

    /// Convert previous NDC to UV [0,1]
    float2 prevUV;
    prevUV.x =  prevNdc.x * 0.5 + 0.5;
    prevUV.y = -prevNdc.y * 0.5 + 0.5;

    /// ─── Step 3: Determine blend factor ─────────────────────────────────
    float adaptiveBlend = blendFactor;

    /// Check if reprojected UV is on-screen
    /// @ref bValidPreviousUVs
    bool validHistory = all(prevUV > 0.0) && all(prevUV < 1.0);

    if (!validHistory)
    {
        /// Off-screen: use only current frame, no history available
        adaptiveBlend = 1.0;
    }
    else
    {
        /// Check for depth discontinuity (disocclusion detection)
        /// When a previously-occluded area becomes visible, history is invalid.
        ///
        /// Sample depth at reprojected location and compare with current depth.
        /// @ref ThresholdToNewSampleKm = 2.0
        /// We convert the km threshold to a device-Z comparison for simplicity.

        /// Reproject the previous UV's depth to check for discontinuity
        float prevDeviceZ = depthTex.SampleLevel(LinearClampSampler, prevUV, 0);

        /// Compute world-space distances for both depths
        float4 currWorld = mul(float4(ndc, deviceZ, 1.0), invViewProj);
        currWorld.xyz /= currWorld.w;
        float4 prevWorld = mul(float4(prevNdc, prevDeviceZ, 1.0), invViewProj);
        prevWorld.xyz /= prevWorld.w;

        float depthDiffKm = abs(length(currWorld.xyz) - length(prevWorld.xyz));

        /// Depth discontinuity threshold: 2.0 km
        /// @ref implementation detail
        static const float kDepthDiscontinuityThresholdKm = 2.0;

        if (depthDiffKm > kDepthDiscontinuityThresholdKm)
        {
            /// Depth discontinuity detected: bias toward current frame
            /// Use 0.3 blend (faster convergence than default 0.05, but not instant)
            adaptiveBlend = 0.3;
        }
    }

    /// ─── Step 4: Sample history and blend ───────────────────────────────
    float4 historyColor;
    if (validHistory)
    {
        /// Sample previous frame at reprojected position with bilinear filtering
        /// @ref SafeSamplePreviousFrameVolumetricTexture
        historyColor = historyTex.SampleLevel(LinearClampSampler, prevUV, 0);

        /// Validate history: reject NaN/Inf values
        /// @ref IsFinite check
        if (!all(isfinite(historyColor)))
        {
            historyColor = float4(0.0, 0.0, 0.0, 0.0);
            adaptiveBlend = 1.0;
        }
    }
    else
    {
        /// No valid history: use current frame
        historyColor = currentColor;
    }

    /// Temporal blend: exponential moving average
    /// @ref implementation detail
    /// FilteredData = PrevData + TemporalFactor * (CurrData - PrevData)
    /// This is equivalent to lerp(history, current, factor)
    float4 result = historyColor + adaptiveBlend * (currentColor - historyColor);

    /// Clamp result to valid range: luminance >= 0, alpha in [0,1]
    result.rgb = max(result.rgb, 0.0);
    result.a   = saturate(result.a);

    temporalOutput[DTid] = result;
}
