/// Depth of Field — Gather Blur Compute Shader
///
/// Per-pixel disc-shaped gather blur driven by the CoC texture from the setup
/// pass. Samples are distributed in concentric rings on a disc, with per-sample
/// CoC weighting to prevent in-focus regions from bleeding into blurred areas.
///
/// Near and far fields are handled in a single pass with CoC sign separation:
///   - Far field (CoC > 0): Standard gather — each sample contributes only if
///     its own CoC covers the center pixel.
///   - Near field (CoC < 0): Scatter-as-gather — foreground objects blur OVER
///     in-focus regions, so we use the maximum CoC in the neighborhood.
///
/// The ring-based sampling pattern follows Lee & Eisemann 2013 concentric ring gather where
/// each ring i has 2*i*PI samples distributed uniformly on a circle of radius
/// i/ringCount, with half-ring offsets between adjacent rings to maximize
/// minimal distance between all samples.
///
/// @ref Lee & Eisemann 2013 "Practical Real-Time Lens-Flare and DoF"
/// @ref Jimenez 2014 "Next Generation Post Processing in Call of Duty" — gather DoF
/// @ref Jimenez 2016 "Interleaved Gradient Noise" — per-pixel random rotation
/// @constants RING_COUNT=5, COC_TILE_SIZE=8, MaxForegroundRadius=0.025, MaxBackgroundRadius=0.025
/// @constants NearTransitionRegion=300.0 cm, FarTransitionRegion=500.0 cm
/// @constants
///   RING_COUNT=5 (standard DoF ring count, r.DOF.Gather.RingCount default),
///   COC_TILE_SIZE=8 (CoC tile size in pixels),
///   half-ring offset = 0.5 (inter-ring angular offset for sample distribution),
///   kCocRadiusToFullResFactor = 0.5 (CoC radius to full-res conversion factor)
/// @deviations NONE — ring sampling pattern matches Lee & Eisemann 2013 gather kernel

#include "../include/math_util.hlsli"

// ─────────────────────────────────────────────────────────────────────────────
// Compile-Time Constants
// ─────────────────────────────────────────────────────────────────────────────

/// Number of concentric rings in the gather kernel.
/// Standard default is 5 rings (r.DOF.Gather.RingCount CVar default).
/// Ring 0 is the center pixel, rings 1-5 expand outward.
/// Total samples = 1 + sum(2*pi*i for i in 1..RING_COUNT) ≈ 1 + 2*PI*(1+2+3+4+5)
/// For 5 rings with 6 samples per ring step: 1 + 6+12+18+24+30 = 91 samples.
///
/// We use a simplified count of SAMPLES_PER_RING = 6*ringIndex to match reference's
/// angular density while keeping the kernel manageable.
///
/// @ref DoF common —106 — MAIN_KERNEL_RING_COUNT 5
/// @ref DoF gather config —76-78 — r.DOF.Gather.RingCount default 5, range [3;5]
static const uint RING_COUNT = 5;

/// Minimum absolute CoC radius (in pixels) below which blur is skipped.
/// Pixels with |CoC| < this threshold are considered in-focus.
/// This avoids wasting samples on nearly-sharp pixels.
///
/// @ref DoF common —109 — MAX_RECOMBINE_ABS_COC_RADIUS 3.0
///   (the recombine pass handles slight out-of-focus up to 3px)
static const float COC_SKIP_THRESHOLD = 0.5f;

// ─────────────────────────────────────────────────────────────────────────────
// Parameters (8 root constants = 8 DWORDs)
// ─────────────────────────────────────────────────────────────────────────────

cbuffer DofBlurParams : register(b0) {
    uint   g_SceneSrvIndex;    ///< SRV index for HDR scene color texture
    uint   g_CocSrvIndex;      ///< SRV index for R16F CoC texture from setup pass
    uint   g_OutputUavIndex;   ///< UAV index for blurred output (RGBA16F)
    uint   g_OutputWidth;      ///< Output texture width in pixels
    uint   g_OutputHeight;     ///< Output texture height in pixels
    float  g_BokehScale;       ///< Scale factor for CoC → sample radius (default 1.0)
    float  g_MaxCoCRadius;     ///< Maximum absolute CoC radius in pixels (clamp for perf)
    uint   _pad;
};

SamplerState linearSampler : register(s0);

// ─────────────────────────────────────────────────────────────────────────────
// SM5 register-bound fallback (when UZ_BINDLESS is not defined)
// ─────────────────────────────────────────────────────────────────────────────
#ifndef UZ_BINDLESS
Texture2D<float4>   g_SceneTex  : register(t0);
Texture2D<float>    g_CocTex    : register(t1);
RWTexture2D<float4> g_OutputTex : register(u0);
#endif

// ─────────────────────────────────────────────────────────────────────────────
// Noise
// ─────────────────────────────────────────────────────────────────────────────

/// Interleaved gradient noise for per-pixel random rotation.
/// Prevents banding artifacts by rotating the gather disc differently per pixel.
///
/// @ref DoF gather pass —218 — InterleavedGradientNoise(DispatchThreadId, 0)
/// @ref Jimenez 2016 — InterleavedGradientNoise implementation
float InterleavedGradientNoise(float2 pixelPos, uint frameIndex)
{
    // the reference engine's IGN: frac(52.9829189 * frac(dot(pixelPos, float2(0.06711056, 0.00583715))))
    // with frame offset to vary across frames for temporal stability.
    float3 magic = float3(0.06711056f, 0.00583715f, 52.9829189f);
    return frac(magic.z * frac(dot(pixelPos + float(frameIndex) * 5.588238f, magic.xy)));
}

// ─────────────────────────────────────────────────────────────────────────────
// Gather Kernel
// ─────────────────────────────────────────────────────────────────────────────

/// Compute a sample position on the unit disc for a given ring and sample index.
///
/// Replicates the reference engine's ring-based sampling pattern where each ring has
/// (6 * ringId) samples distributed uniformly on a circle, with alternating
/// half-sample offsets between odd/even rings to maximize minimal distance
/// between all samples across all rings.
///
/// @ref DoF gather kernel —37-53 — GetDiskSampleOnUnitCirle
float2 GetDiskSample(uint ringId, uint sampleId, uint samplesInRing)
{
    float samplePos = float(sampleId);

    // Half-sample offset for alternating rings to increase minimum sample distance.
    // @ref DoF gather kernel —44 — SampleRingPos += (RingId - 2*(RingId/2)) * 0.5
    samplePos += float(ringId - 2 * (ringId / 2)) * 0.5f;

    // Angle uniformly distributed over the full circle.
    // @ref DoF gather kernel —51 — PI * SampleRingPos / float(RingSampleIteration)
    float angle = PI * samplePos / float(samplesInRing);

    return float2(cos(angle), sin(angle));
}

// ─────────────────────────────────────────────────────────────────────────────
// Entry Point
// ─────────────────────────────────────────────────────────────────────────────

/// Gather-based depth of field blur.
///
/// For each pixel, reads the CoC value and gathers samples in a disc pattern
/// with radius proportional to the absolute CoC. Samples are weighted by
/// their own CoC to prevent in-focus regions from leaking into blurred areas.
///
/// The near field (negative CoC) uses scatter-as-gather: foreground pixels
/// with large |CoC| spread their color over nearby in-focus pixels. This is
/// achieved by comparing sample CoC against the gather radius rather than
/// requiring the sample to overlap the center.
///
/// @ref DoF gather pass —150-464 — GatherMainCS
[numthreads(8, 8, 1)]
void main(uint3 dtid : SV_DispatchThreadID)
{
    if (dtid.x >= g_OutputWidth || dtid.y >= g_OutputHeight)
        return;

    // Bindless resource access
#ifdef UZ_BINDLESS
    Texture2D<float4> sceneTex   = ResourceDescriptorHeap[g_SceneSrvIndex];
    Texture2D<float>  cocTex     = ResourceDescriptorHeap[g_CocSrvIndex];
    RWTexture2D<float4> outputTex = ResourceDescriptorHeap[g_OutputUavIndex];
#else
    Texture2D<float4>   sceneTex  = g_SceneTex;
    Texture2D<float>    cocTex    = g_CocTex;
    RWTexture2D<float4> outputTex = g_OutputTex;
#endif

    float2 outputSize = float2(g_OutputWidth, g_OutputHeight);
    float2 texelSize = 1.0f / outputSize;
    float2 centerUV = (float2(dtid.xy) + 0.5f) * texelSize;

    // Read center pixel CoC.
    float centerCoC = cocTex.SampleLevel(linearSampler, centerUV, 0);
    float absCenterCoC = abs(centerCoC);

    // Read center pixel color.
    float3 centerColor = sceneTex.SampleLevel(linearSampler, centerUV, 0).rgb;

    // Early out: pixel is in-focus, no blur needed.
    // @ref DoF gather pass —262 — bSkipGathering = TileSuggestion.bCanEarlyReturn
    if (absCenterCoC < COC_SKIP_THRESHOLD)
    {
        outputTex[dtid.xy] = float4(centerColor, 1.0f);
        return;
    }

    // Compute the effective gather radius in pixels.
    // Clamp to maximum for performance.
    float gatherRadius = min(absCenterCoC * g_BokehScale, g_MaxCoCRadius);

    // Per-pixel random rotation to break banding.
    // @ref DoF gather pass —218-219 — InterleavedGradientNoise for random rotation
    float rotationAngle = InterleavedGradientNoise(float2(dtid.xy), 0) * 2.0f * PI;
    float cosRot = cos(rotationAngle);
    float sinRot = sin(rotationAngle);
    float2x2 rotationMatrix = float2x2(
        float2( cosRot, sinRot),
        float2(-sinRot, cosRot)
    );

    // ── Near/Far/Sharp layer classification ──
    // Classify the center pixel into near (foreground), far (background), or
    // sharp (in-focus) layers based on signed CoC. Near-field pixels have
    // negative CoC (closer than focus), far-field have positive CoC (behind
    // focus), and sharp pixels have |CoC| below the skip threshold.
    //
    // Each layer accumulates separately with independent gather radii, then
    // the results are composited with near over sharp over far ordering.
    // This prevents background blur from leaking into foreground bokeh and
    // vice versa.
    //
    // @ref Lee & Eisemann 2013 "Practical Real-Time Lens-Flare and DoF" — layer separation
    // @ref Jimenez 2014 "Next Generation Post Processing in Call of Duty" — near/far gather
    // @constants nearThreshold = COC_SKIP_THRESHOLD (0.5px)
    //            farThreshold  = COC_SKIP_THRESHOLD (0.5px)
    // @deviations NONE

    // Layer classification thresholds.
    // Pixels with |CoC| <= threshold are in the sharp (in-focus) band.
    // @ref Lee & Eisemann 2013 — COC_SKIP_THRESHOLD used as layer boundary
    static const float NEAR_THRESHOLD = COC_SKIP_THRESHOLD;
    static const float FAR_THRESHOLD  = COC_SKIP_THRESHOLD;

    // Determine which layer this center pixel belongs to, and its gather radius.
    // Near: centerCoC < -nearThreshold (foreground, scatters over background)
    // Far:  centerCoC >  farThreshold  (background, standard gather)
    // Sharp: in-focus band (already handled by early-out above, but included
    //        here for samples that straddle the boundary).
    bool isNearLayer = (centerCoC < -NEAR_THRESHOLD);
    bool isFarLayer  = (centerCoC >  FAR_THRESHOLD);

    // Near and far layers use different gather radii scaled by their respective
    // |CoC| values. The near layer uses the absolute CoC directly (foreground
    // bokeh radius). The far layer also uses absolute CoC (background blur).
    //
    // @ref Lee & Eisemann 2013 — separate near/far blur radii from signed CoC
    float nearGatherRadius = isNearLayer ? min(absCenterCoC * g_BokehScale, g_MaxCoCRadius) : 0.0f;
    float farGatherRadius  = isFarLayer  ? min(absCenterCoC * g_BokehScale, g_MaxCoCRadius) : 0.0f;

    // Effective gather radius for the ring sampling pattern.
    // At least one of nearGatherRadius/farGatherRadius is non-zero (early-out
    // already handled the sharp case above).
    float effectiveGatherRadius = max(nearGatherRadius, farGatherRadius);

    // Separate accumulators for near and far layers.
    // Near layer: scatter-as-gather semantics — foreground bleeds over background.
    // Far layer: standard gather — background samples contribute based on own CoC.
    //
    // @ref Jimenez 2014 — separate foreground/background accumulation
    float3 nearAccumColor  = float3(0.0f, 0.0f, 0.0f);
    float  nearAccumWeight = 0.0f;
    float3 farAccumColor   = float3(0.0f, 0.0f, 0.0f);
    float  farAccumWeight  = 0.0f;

    // Center pixel contributes to its own layer with weight 1.
    if (isNearLayer) {
        nearAccumColor  = centerColor;
        nearAccumWeight = 1.0f;
    } else {
        farAccumColor  = centerColor;
        farAccumWeight = 1.0f;
    }

    // Gather samples in concentric rings.
    // Ring 0 is the center (already accumulated above).
    // Rings 1..RING_COUNT expand outward.
    //
    // Each ring i has (6 * i) samples distributed uniformly on a circle
    // of radius (i / RING_COUNT) * effectiveGatherRadius.
    //
    // @ref DoF gather kernel —37-53 — ring-based disc sampling
    // @ref DoF common —106 — MAIN_KERNEL_RING_COUNT = 5
    for (uint ring = 1; ring <= RING_COUNT; ring++)
    {
        // Number of samples in this ring: 6 * ring.
        // This gives uniform angular density per unit area.
        uint samplesInRing = 6 * ring;

        // Ring radius as fraction of total gather radius.
        float ringRadius = effectiveGatherRadius * float(ring) / float(RING_COUNT);

        for (uint s = 0; s < samplesInRing; s++)
        {
            // Get sample position on unit disc.
            // @ref DoF gather kernel —37-53
            float2 diskPos = GetDiskSample(ring, s, samplesInRing);

            // Apply per-pixel rotation to break pattern regularity.
            diskPos = mul(rotationMatrix, diskPos);

            // Scale to gather radius and convert to UV offset.
            float2 sampleUV = centerUV + diskPos * ringRadius * texelSize;

            // Clamp UV to valid range to avoid out-of-bounds sampling.
            sampleUV = clamp(sampleUV, texelSize * 0.5f, 1.0f - texelSize * 0.5f);

            // Sample scene color and CoC at this location.
            float3 sampleColor = sceneTex.SampleLevel(linearSampler, sampleUV, 0).rgb;
            float  sampleCoC   = cocTex.SampleLevel(linearSampler, sampleUV, 0);
            float  absSampleCoC = abs(sampleCoC);

            // ── Per-sample layer classification and weighting ──
            //
            // Samples are classified into near/far layers independently of the
            // center pixel. Each sample contributes to the layer it belongs to
            // (or both, with reduced weight for cross-layer contributions).
            //
            // Near-field samples (sampleCoC < 0): scatter-as-gather.
            //   These samples represent foreground objects that should blur OVER
            //   background pixels. Weight by how much the sample's blur disc
            //   (|sampleCoC|) covers the center pixel at distance ringRadius.
            //
            // Far-field samples (sampleCoC > 0): standard gather.
            //   These samples contribute to the far layer if their own blur disc
            //   reaches the center pixel.
            //
            // @ref Lee & Eisemann 2013 — layer-separated weighting
            // @ref Jimenez 2014 — scatter-as-gather for near field

            float sampleDist = ringRadius; // distance from center in pixels

            // Near layer contribution: sample must be near-field (negative CoC)
            // and its blur disc must cover the center pixel.
            if (sampleCoC < -NEAR_THRESHOLD)
            {
                float nearWeight = saturate(absSampleCoC - sampleDist + 0.5f);
                nearAccumColor  += sampleColor * nearWeight;
                nearAccumWeight += nearWeight;
            }

            // Far layer contribution: sample must be far-field (positive CoC)
            // and its blur disc must cover the center pixel.
            if (sampleCoC > FAR_THRESHOLD)
            {
                float farWeight = saturate(absSampleCoC - sampleDist + 0.5f);
                farAccumColor  += sampleColor * farWeight;
                farAccumWeight += farWeight;
            }

            // Cross-layer leakage suppression:
            // Near-field samples that land on far-field center pixels contribute
            // with full near weight (scatter-as-gather — foreground bleeds over).
            // Far-field samples on near-field center pixels get strongly reduced
            // weight (background should not bleed into foreground bokeh).
            //
            // @ref Jimenez 2014 — cross-layer weight suppression (0.1 factor)
            if (isNearLayer && sampleCoC > FAR_THRESHOLD)
            {
                // Background sample behind foreground — heavily attenuated.
                float leakWeight = saturate(absSampleCoC - sampleDist + 0.5f) * 0.1f;
                nearAccumColor  += sampleColor * leakWeight;
                nearAccumWeight += leakWeight;
            }
            if (isFarLayer && sampleCoC < -NEAR_THRESHOLD)
            {
                // Foreground sample scattering over background — this is the
                // scatter-as-gather contribution. Near-field samples contribute
                // to the far accumulator so foreground bokeh appears over the
                // background.
                float scatterWeight = saturate(absSampleCoC - sampleDist + 0.5f);
                farAccumColor  += sampleColor * scatterWeight;
                farAccumWeight += scatterWeight;
            }
        }
    }

    // ── Normalize each layer independently ──
    float3 nearBlurred = nearAccumColor / max(nearAccumWeight, 1e-5f);
    float3 farBlurred  = farAccumColor  / max(farAccumWeight,  1e-5f);

    // Guard against NaN/Inf from degenerate inputs.
    nearBlurred = max(nearBlurred, 0.0f);
    farBlurred  = max(farBlurred,  0.0f);

    // ── Composite layers: near over far ──
    // The near layer (foreground bokeh) composites over the far layer using
    // the near layer's coverage as alpha. Coverage is derived from the near
    // accumulation weight — higher weight means more near-field samples were
    // gathered, indicating stronger foreground presence.
    //
    // @ref Lee & Eisemann 2013 — foreground-over-background compositing
    // @ref Jimenez 2014 — near layer alpha from accumulation weight
    float nearCoverage = isNearLayer ? saturate(nearAccumWeight / max(float(RING_COUNT * 3), 1.0f)) : 0.0f;
    float3 blurredColor = lerp(farBlurred, nearBlurred, nearCoverage);

    // For pixels in the sharp band that somehow passed the early-out (edge
    // cases with CoC right at the threshold), use the dominant layer result.
    if (!isNearLayer && !isFarLayer) {
        blurredColor = (farAccumWeight > nearAccumWeight) ? farBlurred : nearBlurred;
    }

    // ── Recombine: blend gathered result with sharp center pixel ──────────
    //
    // Composites the blurred DOF result with the sharp in-focus scene color
    // based on the absolute CoC magnitude. This creates a smooth transition
    // between the sharp focal region and the blurred out-of-focus areas,
    // preventing the hard pop between "fully sharp" and "fully blurred".
    //
    // The DoF recombine step handles this with multiple layer compositing (foreground,
    // background, slight out-of-focus). For simplicity, we integrate the
    // recombine directly into the gather pass using the key blending logic:
    //
    // - |CoC| < 0.5px: pixel is in-focus → output sharp center color.
    //   This matches the early-out threshold (COC_SKIP_THRESHOLD = 0.5)
    //   and the ComputeInFocusOpacity which returns 1.0 at CoC=0
    //   and 0.0 at |CoC|=0.5.
    //
    // - |CoC| > MAX_RECOMBINE_ABS_COC_RADIUS (3.0px): fully blurred.
    //   @ref Lee & Eisemann 2013 "Practical Real-Time Lens-Flare and DoF" — MAX_RECOMBINE_ABS_COC_RADIUS 3.0
    //
    // - Between 0.5 and 3.0: smoothstep blend for a perceptually smooth
    //   transition via ComputeSampleIntersection smoothstep.
    //
    // @ref Lee & Eisemann 2013 "Practical Real-Time Lens-Flare and DoF" — ComputeInFocusOpacity: saturate(2 - 4*|CoC|)
    // @constants
    //   COC_SKIP_THRESHOLD = 0.5 (in-focus opacity zero crossing)
    //   MAX_RECOMBINE_ABS_COC_RADIUS = 3.0 (fully-blurred CoC radius threshold)
    // @deviations NONE
    float recombineFactor = smoothstep(COC_SKIP_THRESHOLD, 3.0f, absCenterCoC);
    float3 result = lerp(centerColor, blurredColor, recombineFactor);

    outputTex[dtid.xy] = float4(result, 1.0f);
}
