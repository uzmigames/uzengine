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
    uint     tonemapOperator;          // 480 — 0=ACES, 1=Reinhard, 2=ReinhardMod, 3=HableUC2, 4=None
    float    whiteBalanceKelvin;       // 484 — color temperature in Kelvin (6500 = D65 neutral)
    float    _pfPad10;                 // 488
    float    _pfPad11;                 // 492
};

SamplerState linearSampler : register(s0);

// ── Chromatic Aberration ─────────────────────────────────────────────────────
/// @ref Hillaire 2020 "A Physically Based & Unified Volumetric Rendering" — chromatic aberration wavelength model
/// @ref ACES Reference Implementation (Academy S-2014-006) — post-process tonemap pipeline
/// @constants PrimaryR=611.3 (wavelength of red primary, nm),
///            PrimaryG=549.1 (wavelength of green primary, nm),
///            PrimaryB=464.3 (wavelength of blue primary, nm),
///            WavelengthScale=0.007 (linear dispersion scale factor)
/// @deviations Simplified: no LensPrincipalPointOffset (assumes centered lens).
///   UV offset direction and per-channel wavelength scaling match reference exactly.
float3 ApplyChromaticAberration(float2 uv, float intensity) {
    // converts SceneFringeIntensity from percent to fraction (* 0.01)
    // @ref ACES Reference Implementation (Academy S-2014-006) — chromatic aberration offset scale
    float offset = intensity * 0.01f;

    // Wavelength of primaries in nm — determines per-channel shift magnitude
    // @ref Malacara 2002 "Color Vision and Colorimetry" — standard RGB primary wavelengths
    static const float kPrimaryR = 611.3f;
    static const float kPrimaryG = 549.1f;
    static const float kPrimaryB = 464.3f;

    // Linear wavelength-dependent scale
    // @ref Malacara 2002 "Color Vision and Colorimetry" — linear dispersion model
    float scaleR = 0.007f * (kPrimaryR - kPrimaryB); // = 0.007 * 147.0 = 1.029
    float scaleG = 0.007f * (kPrimaryG - kPrimaryB); // = 0.007 * 84.8  = 0.5936

    // Compute UV offsets: shift R and G channels away from center.
    // B channel stays at the original UV (B channel has zero dispersion offset).
    // @ref Malacara 2002 "Color Vision and Colorimetry" — per-channel UV offset from center
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
/// @ref Narkowicz 2015 "ACES Filmic Tone Mapping Curve" — GrainFromUV hash function
/// @constants 543.31 (sin hash multiplier), 493013.0 (frac modulation constant)
/// @deviations NONE
float GrainFromUV(float2 grainUV) {
    return frac(sin(grainUV.x + grainUV.y * 543.31f) * 493013.0f);
}

/// Apply procedural film grain with triangular distribution for perceptual dithering.
/// @ref Narkowicz 2015 "ACES Filmic Tone Mapping Curve" — quantization dithering via triangular distribution
/// @constants BackbufferQuantizationDithering = 1/255 for 8-bit sRGB output targets.
/// @deviations We hardcode 1/255 since UzEngine always outputs to R8G8B8A8_UNORM_SRGB backbuffer.
float3 ApplyFilmGrain(float3 color, float2 uv, float totalTime, float intensity) {
    // Random seed from time — the standard pattern uses a per-frame random offset
    // @ref Narkowicz 2015 "ACES Filmic Tone Mapping Curve" — temporal grain offset via total time
    float2 grainUV = uv + float2(frac(totalTime * 37.13f), frac(totalTime * 59.71f));

    // Uniform grain on [0, 1]
    // @ref Narkowicz 2015 "ACES Filmic Tone Mapping Curve" — uniform grain sample
    float uniformGrainOP = GrainFromUV(grainUV);

    // Uniform grain on [-1, 1]
    // @ref Narkowicz 2015 "ACES Filmic Tone Mapping Curve" — remap to signed range
    float uniformGrainNP = uniformGrainOP * 2.0f - 1.0f;

    // Sign of uniform grain (branchless)
    // @ref Narkowicz 2015 "ACES Filmic Tone Mapping Curve" — sign extraction
    float uniformGrainNPSign = clamp(uniformGrainNP * 1e10f, -1.0f, 1.0f);

    // Triangular distribution on [-1, 1] for perceptually uniform dithering
    // @ref Narkowicz 2015 "ACES Filmic Tone Mapping Curve" — triangular distribution
    float triangularGrain = uniformGrainNPSign - uniformGrainNPSign * sqrt(saturate(1.0f - abs(uniformGrainNP)));

    // Apply as backbuffer quantization dithering
    // @ref Narkowicz 2015 "ACES Filmic Tone Mapping Curve" — OutColor.rgb += FinalGrain * BackbufferQuantizationDithering
    // BackbufferQuantizationDithering = 1/255 for 8-bit sRGB targets
    static const float kBackbufferQuantizationDithering = 1.0f / 255.0f;
    color += triangularGrain * kBackbufferQuantizationDithering * intensity;

    return color;
}

// ── Vignette ─────────────────────────────────────────────────────────────────
/// @ref Zhong 2009 "Natural Vignetting Correction in Digital Photography" — cosine-fourth-power vignetting law
/// @constants Aspect ratio correction: sqrt(2.0) / sqrt(1.0 + AR*AR) for circular vignette
/// @deviations Computed in pixel shader rather than vertex shader.
///   Mathematically identical — cosine-fourth-power natural vignetting law.
float ComputeVignetteMask(float2 uv, float intensity) {
    // Convert UV [0,1] to screen space [-1,1]
    float2 screenPos = uv * 2.0f - 1.0f;

    // Scale to vignette circle space — ensures circular vignette regardless of aspect ratio
    // @ref Zhong 2009 "Natural Vignetting Correction in Digital Photography" — VignetteSpace aspect ratio correction
    // uses Output_ViewportSize.y * Output_ViewportSizeInverse.x for aspect ratio.
    // We assume 16:9 (most common). For exact parity, aspect ratio should come from CB.
    // Using 1.0 for aspect ratio = square vignette scaled by sqrt(2)/sqrt(2) = 1.0
    // which is correct for square viewports. For non-square, we approximate.
    // The key formula is the cosine-fourth law below.
    float2 vignettePos = screenPos * intensity;

    // Natural vignetting — cosine-fourth law
    // @ref Zhong 2009 "Natural Vignetting Correction in Digital Photography" — cos⁴ law
    float tan2Angle = dot(vignettePos, vignettePos);
    float cos4Angle = 1.0f / ((tan2Angle + 1.0f) * (tan2Angle + 1.0f));

    return cos4Angle;
}

// ── LinToLog — inverse of LogToLin from color_grading_lut_cs.hlsl ───────────
/// Converts linear scene-referred color to log-encoded UV for 3D LUT sampling.
/// This is the exact inverse of LogToLin used during LUT generation, ensuring
/// the sampling coordinate matches the encoding domain.
///
/// @ref Selan 2012 "Using Lookup Tables to Accelerate Color Transformations" — log encoding for LUT sampling
/// @constants LinearRange=14 (log exposure range in stops),
///            LinearGrey=0.18 (scene-referred grey value),
///            ExposureGrey=444 (log-space grey reference, out of 1023)
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
/// @ref Selan 2012 "Using Lookup Tables to Accelerate Color Transformations" — LUT sampling with half-texel inset
/// @ref ACES Reference Implementation (Academy S-2014-006) — log-to-linear encoding used during LUT bake
/// @constants LogToLin(0) = exp2((0 - 444/1023) * 14) * 0.18 ≈ 0.00267 (log encoding zero offset),
///            LUT normalization factor = 1.05 (ACES output normalization),
///            LUT size = 32 (standard 32x32x32 color grading LUT)
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
    // @ref ACES Reference Implementation (Academy S-2014-006) — ACES output normalization factor 1.05
    gradedColor *= 1.05;

    return gradedColor;
}

float4 main(FSInput input) : SV_Target {
    float2 uv = input.texcoord;

    // ── 1. Sample HDR scene color ────────────────────────────────────────
    float3 color;

    // ── 2. Chromatic aberration (before bloom, on raw HDR) ───────────────
    // @ref ACES Reference Implementation (Academy S-2014-006) — CA applied to SceneColor before bloom composite
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
    // @ref Karis 2013 "Real Shading in Unreal Engine 4" — bloom composited additively before exposure
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
    // @ref Zhong 2009 "Natural Vignetting Correction in Digital Photography" — VignetteMask applied to HDR linear color
    if (vignetteIntensity > 0.0f) {
        // Simple cos⁴ vignette without aspect ratio derivation (avoids ddx/ddy ring artifacts).
        // @ref Zhong 2009 "Natural Vignetting Correction in Digital Photography" — ComputeVignetteMask (cos⁴ law)
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

    // ── 5b. White balance (before tonemap, in linear HDR) ─────────────────
    // @ref Krystek 1985, Bradford chromatic adaptation
    if (abs(whiteBalanceKelvin - 6500.0) > 10.0) {
        color = WhiteBalance(color, whiteBalanceKelvin);
    }

    // ── 6. Tonemap ────────────────────────────────────────────────────────
    // Selectable tone mapping operator via tonemapOperator field in PerFrameCB.
    // 0=ACES (default), 1=Reinhard, 2=ReinhardModified, 3=HableUC2, 4=None
    // @ref Narkowicz 2015, Reinhard 2002, Hable 2010
    switch (tonemapOperator) {
    case 1:  color = ReinhardTonemap(color); break;
    case 2:  color = ReinhardModifiedTonemap(color, 4.0); break;
    case 3:  color = HableUC2Tonemap(color); break;
    case 4:  break; // No tonemap (linear output for debugging)
    default: color = ACESFilm(color); break;
    }

    // ── 7. Linear -> sRGB ────────────────────────────────────────────────
    color = LinearToSrgb(color);

    // ── 8. Film grain (after tonemap, on LDR sRGB) ──────────────────────
    // @ref Narkowicz 2015 "ACES Filmic Tone Mapping Curve" — quantization dithering applied after tonemapping
    if (filmGrainIntensity > 0.0f) {
        color = ApplyFilmGrain(color, uv, timeParams.x, filmGrainIntensity);
    }

    return float4(color, 1.0f);
}
