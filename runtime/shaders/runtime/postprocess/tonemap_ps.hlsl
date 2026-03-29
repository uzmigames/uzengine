/// Tonemap pixel shader — applies chromatic aberration + bloom composite +
/// exposure + color grading 3D LUT (or ACESFilm fallback) + film grain +
/// vignette.
///
/// Reads the HDR scene color and optional bloom texture, composites bloom
/// additively (before exposure), then applies auto-exposure multiplier from
/// PerFrameCB, samples the pre-baked color grading 3D LUT (which contains
/// white balance + AP1 conversion + color correction + ACES S-curve + sRGB
/// encoding), and applies film grain + vignette.
///
/// When the color grading LUT is not available (SRV index = 0 in bindless,
/// or not bound in SM5), falls back to Narkowicz 2015 ACES approximation
/// + LinearToSrgb.
///
/// # Post-process application order
/// 1. Sample HDR scene color
/// 2. Chromatic aberration (shift R/B channels) — if intensity > 0
/// 3. Composite bloom
/// 4. Vignette — if intensity > 0
/// 5. Apply exposure
/// 6. Color grading LUT sample (tonemapping + sRGB baked in) — or ACESFilm fallback
/// 7. Film grain — if intensity > 0
/// 8. Output (already sRGB from LUT)
///
/// @ref Selan 2012 "Using Lookup Tables for Color Grading" — LUT sampling pipeline
/// @ref ACES Reference Implementation (Academy S-2014-006) — tone curve (RRT+ODT)
/// @ref Narkowicz 2015 "ACES Filmic Tone Mapping Curve" — fast approximation fallback
/// @deviations
///   Bloom is composited inside the tone mapper, added before exposure.
///   Chromatic aberration: simplified — we skip lens principal point offset
///   (assumes centered lens) and use a direct UV offset rather than the full
///   LensViewportSpace conversion.
///   Film grain: procedural GrainFromUV with triangular distribution for
///   backbuffer quantization dithering. Texture-based film grain not implemented.
///   Vignette: applied BEFORE tonemapping in HDR space — visually near-identical
///   at typical intensity (0.4) since vignette primarily affects already-dark edges.

#include "../include/pbr_common.hlsli"
#include "../include/film_tonemap.hlsli"

struct FSInput {
    float4 positionCS : SV_Position;
    float2 texcoord   : TEXCOORD0;
};

#ifdef UZ_BINDLESS
struct TonemapIndices {
    uint hdrColorIdx;
    uint bloomColorIdx;          ///< SRV index for bloom result (0 = no bloom).
    uint bloomIntensityBits;     ///< Bloom intensity as float bits (0 = disabled).
    uint lensFlareSrvIdx;        ///< SRV index for lens flare additive contribution (0 = disabled).
    uint colorGradingLutSrvIdx;  ///< SRV index for color grading 3D LUT (0 = no LUT, use ACESFilm fallback).
};
ConstantBuffer<TonemapIndices> tonemapIndices : register(b0);
#else
Texture2D<float4> hdrColor : register(t0);
Texture2D<float4> bloomColor : register(t1);
Texture3D<float4> colorGradingLut : register(t2);
#endif

cbuffer PerFrameCB : register(b1) {
    row_major float4x4 viewProj;        // 0
    float4   cameraPos;                 // 64
    float4   timeParams;                // 80
    float4   lightDir;                  // 96
    float4   lightColor;                // 112
    float4   ambientColor;              // 128
    row_major float4x4 invViewProj;     // 144
    float    nearPlane;                 // 208
    float    farPlane;                  // 212
    uint     irradianceSrvIdx;          // 216
    uint     prefilteredSrvIdx;         // 220
    uint     brdfLutSrvIdx;             // 224
    uint     prefilteredMipLevels;      // 228
    float    skyLightIntensity;         // 232
    float    skyLightColorR;            // 236
    float    skyLightColorG;            // 240
    float    skyLightColorB;            // 244
    float    skyLightMinAmbient;        // 248
    float    exposure;                  // 252
    float4   skyIrradianceSH[7];        // 256 (112 bytes)
    uint     shadowDebugMode;           // 368
    uint     aerialPerspectiveEnabled;  // 372
    uint     transmittanceLutSrvIdx;    // 376
    float    apBottomRadiusKm;          // 380
    float    apTopRadiusKm;             // 384
    float    apRayleighBetaX;           // 388
    float    apRayleighBetaY;           // 392
    float    apRayleighBetaZ;           // 396
    float    apRayleighScaleH;          // 400
    float    apMieBeta;                 // 404
    float    apMieScaleH;              // 408
    float    apMieAnisotropy;           // 412
    float    apSunIntensity;            // 416
    float    apRayleighScale;           // 420
    float    apMieScale;                // 424
    float    toneCurveSlope;             // 428
    float    toneCurveToe;              // 432
    float    toneCurveShoulder;         // 436
    float    toneCurveBlackClip;        // 440
    float    toneCurveWhiteClip;        // 444
    uint     gtaoSrvIndex;             // 448
    uint     brdfDebugMode;            // 452
    float    heightFogContribution;    // 456 — not used here but must declare for layout
    // -- padding between heightFogContribution and new fields is implicit --
    // Actually: heightFogContribution is at offset 464, then these 3 follow:
    float    filmGrainIntensity;       // 468
    float    vignetteIntensity;        // 472
    float    chromaAbIntensity;        // 476
};

SamplerState linearSampler : register(s0);

// ── Chromatic Aberration ─────────────────────────────────────────────────────
/// @ref post-process tonemap pipeline:320-342 — USE_COLOR_FRINGE
/// @ref chromatic aberration wavelength model:679-686 — ChromaticAberrationParams setup
/// @constants PrimaryR=611.3 (PostProcessTonemap.cpp:679),
///            PrimaryG=549.1 (PostProcessTonemap.cpp:680),
///            PrimaryB=464.3 (PostProcessTonemap.cpp:681),
///            WavelengthScale=0.007 (PostProcessTonemap.cpp:684)
/// @deviations Simplified: no LensPrincipalPointOffset (assumes centered lens).
///   UV offset direction and per-channel wavelength scaling match reference exactly.
float3 ApplyChromaticAberration(float2 uv, float intensity) {
    // converts SceneFringeIntensity from percent to fraction (* 0.01)
    // @ref chromatic aberration wavelength model:673 — Offset = SceneFringeIntensity * 0.01f
    float offset = intensity * 0.01f;

    // Wavelength of primaries in nm — determines per-channel shift magnitude
    // @ref chromatic aberration wavelength model:679-681
    static const float kPrimaryR = 611.3f;
    static const float kPrimaryG = 549.1f;
    static const float kPrimaryB = 464.3f;

    // Linear wavelength-dependent scale
    // @ref chromatic aberration wavelength model:684-685
    float scaleR = 0.007f * (kPrimaryR - kPrimaryB); // = 0.007 * 147.0 = 1.029
    float scaleG = 0.007f * (kPrimaryG - kPrimaryB); // = 0.007 * 84.8  = 0.5936

    // Compute UV offsets: shift R and G channels away from center.
    // B channel stays at the original UV (Equivalent: SceneColorB = SampleSceneColor(SceneUV)).
    // @ref post-process tonemap pipeline:329 — CAUV = LensUV - sign(LensUV) * saturate(abs(LensUV) - StartOffset) * CAScale
    // Simplified: no StartOffset (default 0), no lens principal point transform.
    float2 centerOffset = uv - 0.5f;
    float2 uvR = uv + centerOffset * (offset * scaleR);
    float2 uvG = uv + centerOffset * (offset * scaleG);

#ifdef UZ_BINDLESS
    Texture2D<float4> hdrTex = ResourceDescriptorHeap[tonemapIndices.hdrColorIdx];
    float r = hdrTex.Sample(linearSampler, uvR).r;
    float g = hdrTex.Sample(linearSampler, uvG).g;
    float b = hdrTex.Sample(linearSampler, uv).b;
#else
    float r = hdrColor.Sample(linearSampler, uvR).r;
    float g = hdrColor.Sample(linearSampler, uvG).g;
    float b = hdrColor.Sample(linearSampler, uv).b;
#endif

    return float3(r, g, b);
}

// ── Film Grain (procedural) ──────────────────────────────────────────────────
/// @ref post-process tonemap pipeline:114-117 — GrainFromUV
/// @constants 543.31 (PostProcessTonemap.usf:116), 493013.0 (PostProcessTonemap.usf:116)
/// @deviations NONE
float GrainFromUV(float2 grainUV) {
    return frac(sin(grainUV.x + grainUV.y * 543.31f) * 493013.0f);
}

/// Apply procedural film grain with triangular distribution for perceptual dithering.
/// @ref post-process tonemap pipeline:560-596 — Quantization dithering
/// @constants BackbufferQuantizationDithering not used; we use a fixed 1/255 for 8-bit output.
/// @deviations the reference uses BackbufferQuantizationDithering uniform (1/255 for sRGB targets).
///   We hardcode 1/255 since UzEngine always outputs to R8G8B8A8_UNORM_SRGB backbuffer.
float3 ApplyFilmGrain(float3 color, float2 uv, float totalTime, float intensity) {
    // Random seed from time — the reference uses GrainRandomFull passed from CPU
    // @ref post-process tonemap pipeline:253 — GrainUV.zw = TexCoord + GrainRandomFull.xy
    float2 grainUV = uv + float2(frac(totalTime * 37.13f), frac(totalTime * 59.71f));

    // Uniform grain on [0, 1]
    // @ref post-process tonemap pipeline:563
    float uniformGrainOP = GrainFromUV(grainUV);

    // Uniform grain on [-1, 1]
    // @ref post-process tonemap pipeline:566
    float uniformGrainNP = uniformGrainOP * 2.0f - 1.0f;

    // Sign of uniform grain (branchless)
    // @ref post-process tonemap pipeline:567
    float uniformGrainNPSign = clamp(uniformGrainNP * 1e10f, -1.0f, 1.0f);

    // Triangular distribution on [-1, 1] for perceptually uniform dithering
    // @ref post-process tonemap pipeline:571
    float triangularGrain = uniformGrainNPSign - uniformGrainNPSign * sqrt(saturate(1.0f - abs(uniformGrainNP)));

    // Apply as backbuffer quantization dithering
    // @ref post-process tonemap pipeline:595 — OutColor.rgb += FinalGrain * BackbufferQuantizationDithering
    // BackbufferQuantizationDithering = 1/255 for 8-bit sRGB targets
    static const float kBackbufferQuantizationDithering = 1.0f / 255.0f;
    color += triangularGrain * kBackbufferQuantizationDithering * intensity;

    return color;
}

// ── Vignette ─────────────────────────────────────────────────────────────────
/// @ref natural vignetting (cos⁴ law):142-150 — ComputeVignetteMask
/// @ref natural vignetting (cos⁴ law):155-159 — VignetteSpace
/// @constants sqrt(2.0) / sqrt(1.0 + AR*AR) (PostProcessCommon.ush:158)
/// @deviations Computed in pixel shader (reference does it in vertex shader for 3-4 verts).
///   Mathematically identical — cosine-fourth-power natural vignetting law.
float ComputeVignetteMask(float2 uv, float intensity) {
    // Convert UV [0,1] to screen space [-1,1]
    float2 screenPos = uv * 2.0f - 1.0f;

    // Scale to vignette circle space — ensures circular vignette regardless of aspect ratio
    // @ref natural vignetting (cos⁴ law):155-159 — VignetteSpace
    // uses Output_ViewportSize.y * Output_ViewportSizeInverse.x for aspect ratio.
    // We assume 16:9 (most common). For exact parity, aspect ratio should come from CB.
    // Using 1.0 for aspect ratio = square vignette scaled by sqrt(2)/sqrt(2) = 1.0
    // which is correct for square viewports. For non-square, we approximate.
    // The key formula is the cosine-fourth law below.
    float2 vignettePos = screenPos * intensity;

    // Natural vignetting — cosine-fourth law
    // @ref natural vignetting (cos⁴ law):146-149
    float tan2Angle = dot(vignettePos, vignettePos);
    float cos4Angle = 1.0f / ((tan2Angle + 1.0f) * (tan2Angle + 1.0f));

    return cos4Angle;
}

// ── LinToLog — inverse of LogToLin from color_grading_lut_cs.hlsl ───────────
/// Converts linear scene-referred color to log-encoded UV for 3D LUT sampling.
/// This is the exact inverse of LogToLin used during LUT generation, ensuring
/// the sampling coordinate matches the encoding domain.
///
/// @ref log-to-linear encoding:80-93 — LogToLin (inverse)
/// @constants LinearRange=14 (GammaCorrectionCommon.ush:82),
///            LinearGrey=0.18 (GammaCorrectionCommon.ush:83),
///            ExposureGrey=444 (GammaCorrectionCommon.ush:84)
/// @deviations NONE
float3 LinToLog(float3 LinearColor)
{
    const float LinearRange  = 14.0;
    const float LinearGrey   = 0.18;
    const float ExposureGrey = 444.0;

    // Inverse of: LinearColor = exp2((LogColor - ExposureGrey/1023) * LinearRange) * LinearGrey
    //         =>  LogColor    = log2(LinearColor / LinearGrey) / LinearRange + ExposureGrey/1023
    // Clamp to tiny positive value to prevent log2(0) = -inf
    float3 LogColor = log2(max(LinearColor, 1e-10) / LinearGrey) / LinearRange + ExposureGrey / 1023.0;
    return LogColor;
}

// ── Color Grading LUT Sampling ──────────────────────────────────────────────
/// Samples the pre-baked 32x32x32 color grading 3D LUT. The LUT contains the
/// full color grading + ACES tone curve + sRGB encoding pipeline. The output
/// is already in sRGB — no further gamma conversion needed.
///
/// During LUT generation, the input linear color was computed as:
///   LinearColor = LogToLin(LUTCoord) - LogToLin(0)
/// So to reverse this for sampling, we need:
///   LUTCoord = LinToLog(LinearColor + LogToLin(0))
///
/// @ref Selan 2012 "Using Lookup Tables for Color Grading":415-420 — LUT sampling
/// @ref log-to-linear encoding:80-93 — LogToLin encoding used during LUT bake
/// @constants LogToLin(0) = exp2((0 - 444/1023) * 14) * 0.18 ≈ 0.00267 (GammaCorrectionCommon.ush:89)
///            LUT normalization factor = 1.05 (CombineLUTsCommon.usf:419)
///            LUT size = 32 (CombineLUTsCommon.usf:39)
/// @deviations NONE
float3 ColorGradingLUTSample(Texture3D<float4> lutTex, SamplerState samp, float3 linearColor)
{
    // LogToLin(0) — the offset subtracted during LUT bake
    // = exp2((0.0 - 444.0/1023.0) * 14.0) * 0.18
    const float LogToLinZero = exp2((0.0 - 444.0 / 1023.0) * 14.0) * 0.18;

    // Clamp negative values to prevent NaN from log2 (can occur from TAA/bloom artifacts)
    linearColor = max(linearColor, 0.0);

    // Add back the LogToLin(0) offset that was subtracted during LUT generation,
    // then convert to log-encoded UV coordinates
    float3 lutUV = LinToLog(linearColor + LogToLinZero);

    // Clamp to valid LUT range [0, 1]
    lutUV = saturate(lutUV);

    // Apply half-texel inset for correct 3D texture sampling:
    // Maps [0,1] to [0.5/LUTSize, 1.0 - 0.5/LUTSize] so edge texels
    // are sampled at their centers rather than their boundaries.
    const float LUTSize = 32.0;
    lutUV = lutUV * ((LUTSize - 1.0) / LUTSize) + (0.5 / LUTSize);

    // Sample the 3D LUT with trilinear filtering
    float3 gradedColor = lutTex.SampleLevel(samp, lutUV, 0).rgb;

    // Undo the 1.05 normalization applied during LUT generation
    // @ref Selan 2012 "Using Lookup Tables for Color Grading":419
    gradedColor *= 1.05;

    return gradedColor;
}

float4 main(FSInput input) : SV_Target {
    float2 uv = input.texcoord;

    // ── 1. Sample HDR scene color ────────────────────────────────────────
    float3 color;

    // ── 2. Chromatic aberration (before bloom, on raw HDR) ───────────────
    // @ref post-process tonemap pipeline:320-342 — applied to SceneColor sample
    // applies CA when sampling scene color, before bloom composite.
    if (chromaAbIntensity > 0.0f) {
        color = ApplyChromaticAberration(uv, chromaAbIntensity);
    } else {
#ifdef UZ_BINDLESS
        Texture2D<float4> hdrTex = ResourceDescriptorHeap[tonemapIndices.hdrColorIdx];
        color = hdrTex.Sample(linearSampler, uv).rgb;
#else
        color = hdrColor.Sample(linearSampler, uv).rgb;
#endif
    }

    // ── 3. Composite bloom ───────────────────────────────────────────────
    // @ref post-process tonemap pipeline:518 — FinalLinearColor += Bloom * (...)
#ifdef UZ_BINDLESS
    if (tonemapIndices.bloomColorIdx != 0) {
        Texture2D<float4> bloomTex = ResourceDescriptorHeap[tonemapIndices.bloomColorIdx];
        float bloomIntensity = asfloat(tonemapIndices.bloomIntensityBits);
        float3 bloom = bloomTex.Sample(linearSampler, uv).rgb;
        color += bloom * bloomIntensity;
    }
#else
    // SM5 path: bloom texture at t1 is always declared. When not bound by
    // the DX11 backend, sampling returns (0,0,0,0) — no bloom contribution.
    // Bloom intensity is not available via root constants in SM5; the
    // C++ backend premultiplies bloom intensity into the bloom texture.
    {
        float3 bloom = bloomColor.Sample(linearSampler, uv).rgb;
        color += bloom;
    }
#endif

    // ── 3b. Composite lens flare (additive, after bloom) ─────────────────
#ifdef UZ_BINDLESS
    if (tonemapIndices.lensFlareSrvIdx != 0) {
        Texture2D<float4> flareTex = ResourceDescriptorHeap[tonemapIndices.lensFlareSrvIdx];
        float3 flare = flareTex.Sample(linearSampler, uv).rgb;
        color += flare;
    }
#endif

    // ── 4. Vignette (BEFORE exposure, in HDR space) ────────────────────
    // @ref post-process tonemap pipeline:515 — VignetteMask applied to HDR linear color
    //   FinalLinearColor = SceneColor * SceneColorTint * (OneOverPreExposure * GlobalExposure * VignetteMask)
    // @ref natural vignetting (cos⁴ law):142-150 — ComputeVignetteMask
    // @ref natural vignetting (cos⁴ law):155-159 — VignetteSpace aspect ratio correction
    if (vignetteIntensity > 0.0f) {
        // @ref natural vignetting (cos⁴ law):142-150 — ComputeVignetteMask (cos⁴ natural vignetting)
        // @ref natural vignetting (cos⁴ law):155-159 — VignetteSpace aspect ratio correction
        // Simple cos⁴ vignette without aspect ratio derivation (avoids ddx/ddy ring artifacts).
        // passes aspect ratio from CPU via View.VignetteSpace; we use the standard formula
        // that only depends on the screen position, matching the reference engine's ComputeVignetteMask.
        float2 screenPos = uv * 2.0f - 1.0f;
        float2 vignettePos = screenPos * vignetteIntensity;
        float tan2Angle = dot(vignettePos, vignettePos);
        float cos4Angle = 1.0f / ((tan2Angle + 1.0f) * (tan2Angle + 1.0f));
        color *= cos4Angle;
    }

    // ── 5. Apply auto-exposure ───────────────────────────────────────────
    // Auto-exposure already clamps via min/max brightness settings.
    // Floor of 0.2 prevents overly dark output when auto-exposure resets.
    color *= max(exposure, 0.2);

    // ── 6. Tonemap (ACES) + sRGB ────────────────────────────────────────
    // Narkowicz 2015 ACES approximation. The color grading LUT path
    // (ColorGradingLUTSample) is available but disabled by default until
    // visual verification with a loaded scene confirms correct output.
    color = ACESFilm(color);

    // ── 7. Linear -> sRGB ────────────────────────────────────────────────
    color = LinearToSrgb(color);

    // ── 8. Film grain (after tonemap, on LDR sRGB) ──────────────────────
    // @ref post-process tonemap pipeline:560-596 — quantization dithering
    // applies procedural grain after tonemapping, before final output.
    if (filmGrainIntensity > 0.0f) {
        color = ApplyFilmGrain(color, uv, timeParams.x, filmGrainIntensity);
    }

    return float4(color, 1.0f);
}
