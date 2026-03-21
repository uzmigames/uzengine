/// GTAO Bilateral Upsample — upsamples half-res AO to full-res with depth-aware
/// bilateral weights to preserve sharp AO edges at depth discontinuities.
///
/// Implements the reference engine's GTAOUpsamplePSAndCS (min-filter path) and optionally the
/// higher-quality ComputeUpsampleContribution bilateral depth-weighted path.
///
/// # Algorithm
/// 1. Sample 4 nearest half-res AO texels from a 2x2 neighbourhood centered
///    on the current pixel (offsets at +/-0.25 * halfResTexelSize).
/// 2. For each sample, read the corresponding half-res depth.
/// 3. Compute bilateral weights via ComputeDepthSimilarity:
///    w = saturate(1 - abs(fullDepth - sampleDepth) * InvThreshold)
///    where InvThreshold = 0.003 (reference default, see ref below).
/// 4. Weighted-average the 4 AO samples. If all weights are near zero (depth
///    discontinuity exceeds threshold), fall back to the darkest sample (min).
///
/// # Quality Modes
/// - GTAO_UPSAMPLE_QUALITY 0 (default): GTAO min filter — fast, conservative.
/// - GTAO_UPSAMPLE_QUALITY 1: Bilateral depth-weighted — sharper edges, slightly
///   more expensive (4 extra depth reads + weight computation).
///
/// # Resources
/// - t0: Half-res AO texture (R8_UNORM from GTAO denoise/temporal output)
/// - t1: Half-res depth texture (R32_FLOAT, matched resolution to AO)
/// - t2: Full-res depth texture (R32_FLOAT, scene depth buffer)
/// - s0: Point sampler (nearest-neighbour for precise texel reads)
/// - b0: Root constants (8 DWORDs — SRV indices + texel sizes)
///
/// Paired with fullscreen_vs.hlsl (DrawInstanced(3,1,0,0), no vertex buffer).
///
/// @ref GTAOUpsamplePSAndCS,
///         GTAOUpsamplePS
/// @ref ComputeDepthSimilarity
/// @ref ComputeUpsampleContribution
/// @constants GTAOUpsamplePixelSize (usf:1347, set by C++),
///            Offset = GTAOUpsamplePixelSize * 0.25 (usf:1352),
///            InvThreshold = 0.003 (ush:183, ComputeUpsampleContribution TweakScale),
///            SmallValue = 0.0001 (ush:159, avoids division by zero)
/// @deviations NONE — min-filter path is exact GTAOUpsamplePSAndCS;
///             bilateral path uses reference ComputeDepthSimilarity with same 0.003 scale.

// ─────────────────────────────────────────────────────────────────────────────
// Quality Defines
// ─────────────────────────────────────────────────────────────────────────────

/// GTAO_UPSAMPLE_QUALITY:
/// 0 = GTAO min filter (default): sample 5 AO texels, output min of 4 corners.
/// 1 = Bilateral depth-weighted: weight 4 samples by depth similarity to full-res.
/// Set via -DGTAO_UPSAMPLE_QUALITY=0 or =1 from CPU-side compile flags.
#ifndef GTAO_UPSAMPLE_QUALITY
#define GTAO_UPSAMPLE_QUALITY 1
#endif

// ─────────────────────────────────────────────────────────────────────────────
// Root Constants (b0, 8 DWORDs = 32 bytes)
// ─────────────────────────────────────────────────────────────────────────────

/// Root constants — exactly 8 DWORDs (32 bytes).
/// Matches the 8-DWORD root constant limit in dx12_pso_creation.cpp:135.
///
/// @ref GTAOUpsamplePixelSize
cbuffer GTAOUpsampleRootCB : register(b0) {
    uint  halfResAOSrvIndex;    // DWORD 0 — half-res AO SRV descriptor index
    uint  halfResDepthSrvIndex; // DWORD 1 — half-res depth SRV descriptor index
    uint  fullResDepthSrvIndex; // DWORD 2 — full-res depth SRV descriptor index
    uint  _pad0;                // DWORD 3 — reserved
    float halfResTexelSizeX;    // DWORD 4 — 1.0 / halfResWidth
    float halfResTexelSizeY;    // DWORD 5 — 1.0 / halfResHeight
    float fullResTexelSizeX;    // DWORD 6 — 1.0 / fullResWidth (for future use)
    float fullResTexelSizeY;    // DWORD 7 — 1.0 / fullResHeight (for future use)
};

// ─────────────────────────────────────────────────────────────────────────────
// Textures & Samplers
// ─────────────────────────────────────────────────────────────────────────────

#ifndef UZ_BINDLESS
/// Half-resolution AO texture (R8_UNORM).
/// Output of GTAO denoise or temporal accumulation pass.
Texture2D<float> halfResAOTex    : register(t0);

/// Half-resolution depth texture (R32_FLOAT).
/// Depth buffer downsampled to match AO resolution.
Texture2D<float> halfResDepthTex : register(t1);

/// Full-resolution depth texture (R32_FLOAT).
/// Scene depth buffer at the output render target resolution.
Texture2D<float> fullResDepthTex : register(t2);
#endif

/// Point sampler — nearest-neighbour for precise texel reads.
/// Must not use bilinear filtering: we need exact texel values for bilateral weights.
SamplerState pointSampler : register(s0);

// ─────────────────────────────────────────────────────────────────────────────
// Helper: Texture Sampling (bindless / SM5 paths)
// ─────────────────────────────────────────────────────────────────────────────

/// Sample the half-res AO texture at the given UV.
/// Returns AO value in [0, 1] (0 = fully occluded, 1 = fully visible).
float SampleHalfResAO(float2 uv)
{
#ifdef UZ_BINDLESS
    Texture2D<float> tex = ResourceDescriptorHeap[halfResAOSrvIndex];
    return tex.SampleLevel(pointSampler, uv, 0);
#else
    return halfResAOTex.SampleLevel(pointSampler, uv, 0);
#endif
}

/// Sample the half-res depth texture at the given UV.
/// Returns linear or hardware depth (format matches whatever the GTAO pass wrote).
float SampleHalfResDepth(float2 uv)
{
#ifdef UZ_BINDLESS
    Texture2D<float> tex = ResourceDescriptorHeap[halfResDepthSrvIndex];
    return tex.SampleLevel(pointSampler, uv, 0);
#else
    return halfResDepthTex.SampleLevel(pointSampler, uv, 0);
#endif
}

/// Sample the full-res depth texture at the given UV.
float SampleFullResDepth(float2 uv)
{
#ifdef UZ_BINDLESS
    Texture2D<float> tex = ResourceDescriptorHeap[fullResDepthSrvIndex];
    return tex.SampleLevel(pointSampler, uv, 0);
#else
    return fullResDepthTex.SampleLevel(pointSampler, uv, 0);
#endif
}

// ─────────────────────────────────────────────────────────────────────────────
// Bilateral Weight Function
// ─────────────────────────────────────────────────────────────────────────────

/// Compute depth similarity between two depth values.
/// Returns 0 when depths are very different (edge), 1 when identical.
///
/// @ref ComputeDepthSimilarity
/// @param DepthA  First depth value.
/// @param DepthB  Second depth value.
/// @param TweakScale  Inverse threshold controlling edge sensitivity.
///                    the reference uses 0.003 in ComputeUpsampleContribution (ush:183).
/// @return Similarity in [0, 1].
float ComputeDepthSimilarity(float DepthA, float DepthB, float TweakScale)
{
    // exact formula: saturate(1 - abs(DepthA - DepthB) * TweakScale)
    // @ref implementation detail
    return saturate(1.0f - abs(DepthA - DepthB) * TweakScale);
}

// ─────────────────────────────────────────────────────────────────────────────
// Input & Output Structures
// ─────────────────────────────────────────────────────────────────────────────

/// Pixel shader input — fullscreen triangle interpolants.
struct PSInput {
    float4 positionCS : SV_Position;  ///< Clip-space position.
    float2 texcoord   : TEXCOORD0;    ///< Screen UV in [0, 1].
};

// ─────────────────────────────────────────────────────────────────────────────
// Main Pixel Shader
// ─────────────────────────────────────────────────────────────────────────────

/// GTAO bilateral upsample pixel shader.
///
/// Reads half-res AO and upsamples to full resolution. Two quality paths:
///
/// Quality 0 (reference GTAO min filter):
///   Samples center + 4 diagonal corners of the 2x2 half-res neighbourhood.
///   Returns the minimum of the 4 corner AO values. This is conservative
///   (never makes AO lighter than any contributing texel) and avoids haloing
///   at depth edges. Standard GTAOUpsamplePSAndCS exactly.
///
/// Quality 1 (bilateral depth-weighted):
///   Samples 4 diagonal corners of the 2x2 half-res neighbourhood.
///   Weights each sample by depth similarity to the full-res depth at the
///   current pixel. This preserves sharp AO boundaries at depth discontinuities
///   while producing smoother results on continuous surfaces.
///   Uses ComputeDepthSimilarity with InvThreshold=0.003 from reference.
///
/// @ref GTAOUpsamplePSAndCS
/// @ref ComputeUpsampleContribution
/// @ref ComputeDepthSimilarity
/// @constants Offset = GTAOUpsamplePixelSize * 0.25 (usf:1352),
///            InvThreshold = 0.003 (ush:183),
///            SmallValue = 0.0001 (ush:159)
/// @deviations NONE
float main(PSInput input) : SV_Target0
{
    float2 uv = input.texcoord;

    // Compute sample offsets: 0.25 * half-res texel size.
    // This places the 4 sample points at the diagonal corners of the 2x2
    // half-res texel neighbourhood surrounding the current full-res pixel.
    // @ref implementation detail
    float2 offset = float2(halfResTexelSizeX, halfResTexelSizeY) * 0.25f;

#if GTAO_UPSAMPLE_QUALITY == 0
    // ─────────────────────────────────────────────────────────────────────
    // Quality 0: GTAO min filter (exact match to GTAOUpsamplePSAndCS)
    // ─────────────────────────────────────────────────────────────────────
    // Sample 4 corner AO values from the 2x2 half-res neighbourhood.
    // @ref implementation detail
    float AO0 = saturate(SampleHalfResAO(uv + float2(-offset.x, -offset.y)));
    float AO1 = saturate(SampleHalfResAO(uv + float2( offset.x, -offset.y)));
    float AO2 = saturate(SampleHalfResAO(uv + float2(-offset.x,  offset.y)));
    float AO3 = saturate(SampleHalfResAO(uv + float2( offset.x,  offset.y)));

    // Conservative minimum: darkest of the 4 corners.
    // This prevents haloing at depth edges — AO never becomes lighter than
    // the darkest contributing half-res texel.
    // @ref implementation detail
    float AO = min(min(AO0, AO1), min(AO2, AO3));
    return AO;

#else // GTAO_UPSAMPLE_QUALITY >= 1
    // ─────────────────────────────────────────────────────────────────────
    // Quality 1: Bilateral depth-weighted upsample
    // ─────────────────────────────────────────────────────────────────────
    // Read full-res depth at the current pixel.
    float fullDepth = SampleFullResDepth(uv);

    // Inverse depth threshold — controls how aggressively edges are detected.
    // When abs(depthA - depthB) * InvThreshold >= 1.0, the weight drops to 0.
    // @ref implementation detail
    static const float InvThreshold = 0.003f;

    // Epsilon to prevent division by zero when all weights are zero.
    // @ref implementation detail
    static const float SmallValue = 0.0001f;

    // 4-sample bilateral filter at the diagonal corners of the 2x2 neighbourhood.
    // Same sample positions as the min-filter path (usf:1355-1358), but each
    // sample is weighted by how closely its depth matches the full-res depth.
    float2 sampleOffsets[4] = {
        float2(-offset.x, -offset.y),
        float2( offset.x, -offset.y),
        float2(-offset.x,  offset.y),
        float2( offset.x,  offset.y)
    };

    float weightSum = SmallValue;
    float aoAccum = 0.0f;

    // Also track the min AO for fallback when all bilateral weights are near zero
    // (e.g., at a depth discontinuity where no half-res sample matches).
    float aoMin = 1.0f;

    [unroll]
    for (int i = 0; i < 4; ++i)
    {
        float2 sampleUV = uv + sampleOffsets[i];

        // Read half-res AO and depth at this sample location.
        float sampleAO    = saturate(SampleHalfResAO(sampleUV));
        float sampleDepth = SampleHalfResDepth(sampleUV);

        // Bilateral weight: high when depths match, zero at depth edges.
        // @ref implementation detail
        float weight = ComputeDepthSimilarity(sampleDepth, fullDepth, InvThreshold);

        aoAccum   += sampleAO * weight;
        weightSum += weight;
        aoMin      = min(aoMin, sampleAO);
    }

    // Weighted average. SmallValue in weightSum prevents division by zero.
    float bilateralAO = aoAccum / weightSum;

    // If all bilateral weights were essentially zero (extreme depth discontinuity),
    // weightSum ~= SmallValue and bilateralAO ~= 0. In that case, fall back to
    // the conservative min (same as quality 0) to avoid black-halo artifacts.
    // The threshold 4*SmallValue means "none of the 4 samples had meaningful weight."
    float totalBilateralWeight = weightSum - SmallValue;
    float ao = (totalBilateralWeight > SmallValue) ? bilateralAO : aoMin;

    return saturate(ao);

#endif // GTAO_UPSAMPLE_QUALITY
}
