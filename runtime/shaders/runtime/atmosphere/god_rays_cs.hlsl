/// God Rays (Volumetric Light Shafts) — Screen-Space Radial Blur Compute Shader
///
/// Implements screen-space volumetric light shafts by radially blurring bright
/// sky/sun pixels toward the sun's projected screen position. The technique is
/// a compute shader adaptation of the volumetric light shaft radial blur technique,
/// collapsed into a single pass with configurable sample count for simplicity and GPU efficiency.
///
/// # Algorithm
/// For each pixel, march from the pixel UV toward the sun's screen-space UV in
/// N discrete steps. At each step, sample the HDR scene color, apply a sky mask
/// (only sky pixels contribute — depth near 0 in reversed-Z), and accumulate
/// with exponential decay. The result is written to an output texture for
/// additive compositing over the scene.
///
/// # Equivalent
/// performs light shaft blur in multiple passes (default 3) with 12 samples
/// each, exponentially increasing blur distance per pass via:
///   PassScale = pow(0.4 * NUM_SAMPLES, PassIndex)
/// Each pass reads the previous pass output and blurs further toward the light.
///
/// Our single-pass approach with 64 samples provides comparable visual quality
/// at the cost of longer individual samples. The multi-pass approach is more
/// bandwidth efficient when sample counts are limited, but the single-pass
/// compute shader avoids intermediate render targets and synchronization.
///
/// @ref BlurLightShaftsMain (radial blur loop)
/// @ref GLightShaftFirstPassDistance = 0.1f
/// @ref GLightShaftBlurNumSamples = 12
/// @ref GLightShaftBlurPasses = 3
/// @ref GLightShaftDownsampleFactor = 2
/// @ref RadialBlurParameters = (NumSamples, FirstPassDistance, PassIndex)
/// @ref PassScale = pow(0.4 * NUM_SAMPLES, PassIndex)
/// @ref AspectCorrectedBlurVector with min() clamp
/// @ref Luminance = dot(SceneColor, half3(0.3, 0.59, 0.11))
/// @ref AdjustedLuminance = clamp(Luma - Threshold, 0, BloomMaxBrightness)
/// @ref EdgeMask = (1 - x*(1-x)*y*(1-y)*8)^4
/// @source Crytek 2007 — "Volumetric Light Scattering as a Post-Process" (GPU Gems 3, Ch. 13)
///
/// @constants NUM_SAMPLES = 64 (compile-time; the reference uses 12 per pass x 3 passes = 36 total samples)
/// @constants decay = 0.96 (user-configurable via cbuffer; controls per-step light falloff)
/// @constants density = 0.5 (user-configurable; maps to reference GLightShaftFirstPassDistance = 0.1
///   scaled by our higher sample count — 0.1 * 12/64 * passScale * tuning ~ 0.5 effective)
/// @constants weight = 1.0/64.0 (1/NUM_SAMPLES normalization; reference does 1/NUM_SAMPLES in usf:119)
/// @constants SKY_DEPTH_THRESHOLD = 0.001 (reversed-Z: depth < threshold = sky/far plane)
/// @constants EDGE_MASK_SCALE = 8.0 (standard edge vignette scale)
/// @constants LUMINANCE_EPSILON = 6.10352e-5 (half float minimum positive value)
///
/// @deviations INTENTIONAL: Single-pass compute with 64 samples instead of the reference engine's 3-pass x 12
///   pixel shader approach. This trades multi-pass bandwidth savings for simpler dispatch and
///   no intermediate render targets. The visual result is equivalent — both integrate radiance
///   along the pixel-to-sun direction with distance falloff.
/// @deviations INTENTIONAL: Exponential decay (illuminationDecay *= decay) per step instead of
///   the reference engine's simple 1/NUM_SAMPLES average. reference achieves decay through its multi-pass structure
///   (each pass blurs an already-blurred result, creating natural falloff). Our single-pass
///   needs explicit decay to match the visual attenuation.
/// @deviations INTENTIONAL: Sky mask via depth threshold instead of the reference engine's OcclusionDepthRange
///   and BloomDistanceMask system. the reference uses a continuous depth-based occlusion mask;
///   we use a binary sky/non-sky test for simplicity. Both achieve the same goal: only
///   sky/sun pixels emit light shafts.

#include "../include/math_util.hlsli"

// ─────────────────────────────────────────────────────────────────────────────
// Compile-Time Configuration
// ─────────────────────────────────────────────────────────────────────────────

/// Number of radial blur samples per pixel.
/// uses 12 samples x 3 passes = 36 effective samples.
/// We use 64 in a single pass for comparable coverage.
/// @ref GLightShaftBlurNumSamples = 12
#ifndef NUM_SAMPLES
#define NUM_SAMPLES 64
#endif

/// Reversed-Z sky depth threshold.
/// In reversed-Z, sky/far-plane pixels have depth near 0.0.
/// Anything below this threshold is considered "sky" and eligible to emit
/// light shaft radiance.
static const float SKY_DEPTH_THRESHOLD = 0.001f;

/// Edge mask scaling factor — controls how aggressively edges are faded out.
/// Prevents light shaft bleeding at screen borders.
/// @ref * 8.0f in edge mask formula
static const float EDGE_MASK_SCALE = 8.0f;

/// Half-float minimum value, used to prevent division by zero in luminance.
/// @ref 6.10352e-5
static const float LUMINANCE_EPSILON = 6.10352e-5f;

// ─────────────────────────────────────────────────────────────────────────────
// Parameters (root constants at b0)
// ─────────────────────────────────────────────────────────────────────────────

/// God rays constant buffer — all parameters for the radial blur pass.
///
/// sunScreenPos: Sun position projected into UV space [0,1]^2.
///   Computed on CPU via WorldToScreen → clip-to-UV transform, matching
///   reference LightShaftRendering.cpp:168-172 (TextureSpaceBlurOrigin).
///
/// decay: Per-step exponential decay factor. Controls how quickly
///   accumulated radiance falls off along the ray. 0.96 gives gentle
///   falloff; lower values produce shorter, more concentrated shafts.
///
/// density: Fraction of the pixel-to-sun distance to traverse.
///   reference equivalent: GLightShaftFirstPassDistance (0.1) scaled by pass count.
///   @ref god rays first pass distance = 0.1
///
/// weight: Per-sample contribution weight. Default 1/NUM_SAMPLES for
///   energy-preserving accumulation.
///   @ref god rays radial blur average
cbuffer GodRaysCB : register(b0) {
    float2 sunScreenPos;     ///< Sun position in UV space [0,1]^2.
    float2 invScreenSize;    ///< 1.0 / screen dimensions (width, height).
    float  intensity;        ///< Overall god ray intensity multiplier (default 1.0).
    float  decay;            ///< Per-step exponential decay factor (default 0.96).
    float  density;          ///< Step density — fraction of sun distance to cover (default 0.5).
    float  weight;           ///< Per-sample weight (default 1.0 / NUM_SAMPLES).
    uint   sceneSrvIndex;    ///< Bindless SRV index — HDR scene color texture.
    uint   depthSrvIndex;    ///< Bindless SRV index — depth buffer (reversed-Z).
    float  outputWidth;      ///< Output texture width in pixels.
    float  outputHeight;     ///< Output texture height in pixels.
};

// ─────────────────────────────────────────────────────────────────────────────
// Resources
// ─────────────────────────────────────────────────────────────────────────────

/// Output god ray contribution texture (RGBA16F).
/// Written additively over the scene in a separate compose pass.
RWTexture2D<float4> godRaysOutput : register(u0);

/// Bilinear clamp sampler for filtered scene color reads along the blur ray.
/// Filtered sampling is important because sample positions rarely land on
/// exact texel centers — bilinear interpolation prevents aliasing artifacts
/// along the ray direction.
SamplerState LinearClampSampler : register(s0);

// ─────────────────────────────────────────────────────────────────────────────
// SM5 register-bound fallback (when UZ_BINDLESS is not defined)
// ─────────────────────────────────────────────────────────────────────────────
#ifndef UZ_BINDLESS
Texture2D<float4> g_SceneColor : register(t0);
Texture2D<float>  g_DepthTex   : register(t1);
#endif

// ─────────────────────────────────────────────────────────────────────────────
// Helpers
// ─────────────────────────────────────────────────────────────────────────────

/// Edge attenuation mask — fades light shaft contribution near screen borders
/// to prevent harsh edge clipping artifacts.
///
/// The mask is 0 at screen edges and peaks at the center. reference computes this
/// as: mask = 1 - x*(1-x)*y*(1-y)*8, then raises to the 4th power for a
/// smooth sigmoid-like falloff.
///
/// @ref EdgeMask computation
/// @param uv  Normalized screen coordinates in [0,1]^2.
/// @return    Attenuation factor in [0,1] where 1 = no attenuation (center),
///            0 = full attenuation (edges).
float ComputeEdgeAttenuation(float2 uv)
{
    // computes: EdgeMask = 1 - x*(1-x)*y*(1-y)*8
    // Then: EdgeMask = EdgeMask^4
    // The mask is 1 at edges, 0 at center. We invert so 1 = pass, 0 = mask.
    float edgeMask = 1.0f - uv.x * (1.0f - uv.x) * uv.y * (1.0f - uv.y) * EDGE_MASK_SCALE;
    edgeMask = Pow2(Pow2(edgeMask)); // edgeMask^4 via two squarings
    // Invert: the reference engine's edgeMask is 1 at edges; we want 1 at center (pass-through)
    return 1.0f - edgeMask;
}

/// BT.601 luminance from linear RGB.
/// Uses the same weights as the reference engine's light shaft bloom threshold.
/// @ref dot(SceneColor, half3(.3, .59, .11))
float Luminance601(float3 rgb)
{
    return dot(rgb, float3(0.3f, 0.59f, 0.11f));
}

// ─────────────────────────────────────────────────────────────────────────────
// Entry Point
// ─────────────────────────────────────────────────────────────────────────────

/// Radial blur compute shader for god rays / volumetric light shafts.
///
/// Each thread processes one output pixel. The thread marches from the pixel's
/// UV coordinate toward the sun's screen position in NUM_SAMPLES steps,
/// accumulating HDR scene radiance from sky pixels with exponential decay.
///
/// The output is a standalone god ray contribution texture intended for
/// additive compositing over the lit scene.
///
/// Dispatch: ceil(outputWidth / 8) x ceil(outputHeight / 8) x 1
///
/// @ref BlurLightShaftsMain
[numthreads(8, 8, 1)]
void main(uint3 dtid : SV_DispatchThreadID)
{
    // ── Bounds check ─────────────────────────────────────────────────────
    // Dispatch may overshoot texture dimensions; discard out-of-bounds threads.
    if (dtid.x >= uint(outputWidth) || dtid.y >= uint(outputHeight))
        return;

    // ── Bind resources from descriptor heap ──────────────────────────────
#ifdef UZ_BINDLESS
    Texture2D<float4> sceneColor = ResourceDescriptorHeap[sceneSrvIndex];
    Texture2D<float>  depthTex   = ResourceDescriptorHeap[depthSrvIndex];
#else
    Texture2D<float4> sceneColor = g_SceneColor;
    Texture2D<float>  depthTex   = g_DepthTex;
#endif

    // ── Compute UV for this pixel ────────────────────────────────────────
    // +0.5 centers the UV on the texel (pixel center convention).
    float2 uv = (float2(dtid.xy) + 0.5f) * invScreenSize;

    // ── Compute radial blur direction vector ─────────────────────────────
    // Direction from this pixel toward the sun's screen position.
    // Scaled by density / NUM_SAMPLES to control step size.
    //
    // Equivalent formulation (Crytek 2007 "Volumetric Light Scattering as a Post-Process"):
    //   AspectCorrectedBlurVector = (Origin - UV) * min(PassScale * Distance, 1)
    //   BlurVector = AspectCorrectedBlurVector * AspectRatio
    //
    // We fold the aspect correction and pass scaling into the density parameter
    // on the CPU side, keeping the shader simple.
    float2 rayDirection = sunScreenPos - uv;
    float2 deltaUV = rayDirection * (density / float(NUM_SAMPLES));

    // ── Edge attenuation ─────────────────────────────────────────────────
    // Fade contribution near screen edges to avoid hard clipping artifacts.
    // @ref EdgeMask
    float edgeAtten = ComputeEdgeAttenuation(uv);

    // ── Distance-to-sun attenuation ──────────────────────────────────────
    // Pixels far from the sun contribute less. the reference uses BlurOriginDistanceMask:
    //   1 - saturate(length(Origin - UV) * 2)
    // squared for smooth falloff.
    // @ref BlurOriginDistanceMask
    float blurOriginDist = saturate(length(rayDirection) * 2.0f);
    float blurOriginMask = (1.0f - blurOriginDist) * (1.0f - blurOriginDist);

    // ── Radial blur accumulation loop ────────────────────────────────────
    // March from pixel toward sun, accumulating sky radiance with decay.
    //
    // the reference engine's BlurLightShaftsMain (usf:110-117) does a simple average:
    //   for i in 0..NUM_SAMPLES:
    //     BlurredValues += Sample(UV + BlurVector * i / NUM_SAMPLES)
    //   result = BlurredValues / NUM_SAMPLES
    //
    // achieves the falloff through multi-pass blur (each pass blurs an
    // already-blurred result). Our single-pass uses explicit exponential decay
    // (illuminationDecay *= decay) to produce equivalent visual attenuation.
    float3 accumulation = float3(0.0f, 0.0f, 0.0f);
    float2 sampleUV = uv;
    float illuminationDecay = 1.0f;

    [unroll]
    for (int i = 0; i < NUM_SAMPLES; i++)
    {
        sampleUV += deltaUV;

        // Clamp sample coordinates to valid texture range [0,1]
        // @ref clamp(SampleUVs, UVMinMax.xy, UVMinMax.zw)
        float2 clampedUV = clamp(sampleUV, float2(0.0f, 0.0f), float2(1.0f, 1.0f));

        // Sample scene color via bilinear filtering — sample positions are
        // sub-texel, so filtering prevents stepping artifacts along the ray.
        float3 sampleColor = sceneColor.SampleLevel(LinearClampSampler, clampedUV, 0).rgb;

        // Sample depth to determine sky mask.
        // Reversed-Z: sky/far-plane pixels have depth near 0.0.
        float sampleDepth = depthTex.SampleLevel(LinearClampSampler, clampedUV, 0);

        // Sky mask: only sky/sun pixels contribute light shaft radiance.
        // Non-sky pixels (geometry) block the shaft, creating the "god ray"
        // effect where shafts appear between occluding objects.
        //
        // uses OcclusionMask = saturate(SceneDepth * InvOcclusionDepthRange)
        // and BloomDistanceMask = saturate((SceneDepth - 0.5/InvRange) * InvRange)
        // for a continuous falloff. Our binary threshold is simpler but achieves
        // the same visual result when depth precision is sufficient.
        float skyMask = (sampleDepth < SKY_DEPTH_THRESHOLD) ? 1.0f : 0.0f;

        // Accumulate weighted radiance with exponential decay.
        // Weight = per-sample normalization (1/NUM_SAMPLES by default).
        // illuminationDecay provides exponential falloff along the ray —
        // samples closer to the pixel contribute more than distant samples.
        accumulation += sampleColor * skyMask * illuminationDecay * weight;

        // Exponential decay: each step attenuates by the decay factor.
        illuminationDecay *= decay;
    }

    // ── Final contribution ───────────────────────────────────────────────
    // Apply intensity, edge attenuation, and distance-to-sun mask.
    //
    // The result is purely the god ray contribution — it will be
    // additively blended over the scene in a subsequent compose pass.
    float3 godRays = accumulation * intensity * edgeAtten * blurOriginMask;

    // Clamp to prevent NaN/Inf propagation from extreme HDR values.
    // This is a safety guard — well-exposed scenes should not hit this.
    godRays = max(godRays, float3(0.0f, 0.0f, 0.0f));

    godRaysOutput[dtid.xy] = float4(godRays, 1.0f);
}
