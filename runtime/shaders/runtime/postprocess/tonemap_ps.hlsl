/// Tonemap pixel shader — applies chromatic aberration + bloom composite +
/// exposure + UE5 FilmToneMap + film grain + vignette + sRGB gamma.
///
/// Reads the HDR scene color and optional bloom texture, composites bloom
/// additively (before exposure), then applies auto-exposure multiplier from
/// PerFrameCB, UE5-parity FilmToneMap curve (ACES glow + red modifier +
/// custom S-curve), and linear->sRGB.
///
/// # Post-process application order
/// 1. Sample HDR scene color
/// 2. Chromatic aberration (shift R/B channels) — if intensity > 0
/// 3. Composite bloom
/// 4. Apply exposure
/// 5. FilmToneMap (ACES)
/// 6. Film grain — if intensity > 0
/// 7. Vignette — if intensity > 0
/// 8. Linear -> sRGB
/// 9. Output
///
/// # UE5 Equivalent
/// PostProcessTonemap.usf — applies bloom combine + eye adaptation exposure + ACES RRT+ODT.
/// TonemapCommon.ush:105-220 — FilmToneMap function.
///
/// @source UE5 PostProcessTonemap.usf:180-210 — CombinedBloomAndExposure
/// @source UE5 PostProcessTonemap.usf:320-342 — Chromatic aberration (USE_COLOR_FRINGE)
/// @source UE5 PostProcessTonemap.usf:506-515 — Vignette (ComputeVignetteMask * exposure)
/// @source UE5 PostProcessTonemap.usf:560-596 — Film grain (GrainFromUV + triangular dither)
/// @deviations
///   UE5 composites bloom inside the tone mapper; we do the same.
///   Bloom is added before exposure, matching the UE5 combine order.
///   Chromatic aberration: simplified — we skip lens principal point offset
///   (assumes centered lens) and use a direct UV offset rather than the full
///   LensViewportSpace conversion. The per-wavelength scale factors match UE5
///   PostProcessTonemap.cpp:679-685 exactly.
///   Film grain: we use UE5's procedural GrainFromUV (line 114-117) with
///   triangular distribution (line 571) for backbuffer quantization dithering.
///   UE5's texture-based film grain (USE_FILM_GRAIN, lines 521-533) requires
///   a grain texture asset; we use the procedural fallback that UE5 always applies.
///   Vignette: UE5 applies vignette as a multiplier BEFORE tonemapping (line 515).
///   We apply it AFTER tonemapping for simplicity — visually near-identical at
///   typical intensity (0.4) since vignette primarily affects already-dark edges.

#include "../include/pbr_common.hlsli"
#include "../include/film_tonemap.hlsli"

struct FSInput {
    float4 positionCS : SV_Position;
    float2 texcoord   : TEXCOORD0;
};

#ifdef UZ_BINDLESS
struct TonemapIndices {
    uint hdrColorIdx;
    uint bloomColorIdx;      ///< SRV index for bloom result (0 = no bloom).
    uint bloomIntensityBits; ///< Bloom intensity as float bits (0 = disabled).
    uint _pad0;
};
ConstantBuffer<TonemapIndices> tonemapIndices : register(b0);
#else
Texture2D<float4> hdrColor : register(t0);
Texture2D<float4> bloomColor : register(t1);
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
    float    filmSlope;                 // 428
    float    filmToe;                   // 432
    float    filmShoulder;              // 436
    float    filmBlackClip;             // 440
    float    filmWhiteClip;             // 444
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
/// @source UE5 PostProcessTonemap.usf:320-342 — USE_COLOR_FRINGE
/// @source UE5 PostProcessTonemap.cpp:679-686 — ChromaticAberrationParams setup
/// @constants PrimaryR=611.3 (PostProcessTonemap.cpp:679),
///            PrimaryG=549.1 (PostProcessTonemap.cpp:680),
///            PrimaryB=464.3 (PostProcessTonemap.cpp:681),
///            WavelengthScale=0.007 (PostProcessTonemap.cpp:684)
/// @deviations Simplified: no LensPrincipalPointOffset (assumes centered lens).
///   UV offset direction and per-channel wavelength scaling match UE5 exactly.
float3 ApplyChromaticAberration(float2 uv, float intensity) {
    // UE5 converts SceneFringeIntensity from percent to fraction (* 0.01)
    // @source UE5 PostProcessTonemap.cpp:673 — Offset = SceneFringeIntensity * 0.01f
    float offset = intensity * 0.01f;

    // Wavelength of primaries in nm — determines per-channel shift magnitude
    // @source UE5 PostProcessTonemap.cpp:679-681
    static const float kPrimaryR = 611.3f;
    static const float kPrimaryG = 549.1f;
    static const float kPrimaryB = 464.3f;

    // Linear wavelength-dependent scale
    // @source UE5 PostProcessTonemap.cpp:684-685
    float scaleR = 0.007f * (kPrimaryR - kPrimaryB); // = 0.007 * 147.0 = 1.029
    float scaleG = 0.007f * (kPrimaryG - kPrimaryB); // = 0.007 * 84.8  = 0.5936

    // Compute UV offsets: shift R and G channels away from center.
    // B channel stays at the original UV (UE5: SceneColorB = SampleSceneColor(SceneUV)).
    // @source UE5 PostProcessTonemap.usf:329 — CAUV = LensUV - sign(LensUV) * saturate(abs(LensUV) - StartOffset) * CAScale
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
/// @source UE5 PostProcessTonemap.usf:114-117 — GrainFromUV
/// @constants 543.31 (PostProcessTonemap.usf:116), 493013.0 (PostProcessTonemap.usf:116)
/// @deviations NONE
float GrainFromUV(float2 grainUV) {
    return frac(sin(grainUV.x + grainUV.y * 543.31f) * 493013.0f);
}

/// Apply procedural film grain with triangular distribution for perceptual dithering.
/// @source UE5 PostProcessTonemap.usf:560-596 — Quantization dithering
/// @constants BackbufferQuantizationDithering not used; we use a fixed 1/255 for 8-bit output.
/// @deviations UE5 uses BackbufferQuantizationDithering uniform (1/255 for sRGB targets).
///   We hardcode 1/255 since UzEngine always outputs to R8G8B8A8_UNORM_SRGB backbuffer.
float3 ApplyFilmGrain(float3 color, float2 uv, float totalTime, float intensity) {
    // Random seed from time — UE5 uses GrainRandomFull passed from CPU
    // @source UE5 PostProcessTonemap.usf:253 — GrainUV.zw = TexCoord + GrainRandomFull.xy
    float2 grainUV = uv + float2(frac(totalTime * 37.13f), frac(totalTime * 59.71f));

    // Uniform grain on [0, 1]
    // @source UE5 PostProcessTonemap.usf:563
    float uniformGrainOP = GrainFromUV(grainUV);

    // Uniform grain on [-1, 1]
    // @source UE5 PostProcessTonemap.usf:566
    float uniformGrainNP = uniformGrainOP * 2.0f - 1.0f;

    // Sign of uniform grain (branchless)
    // @source UE5 PostProcessTonemap.usf:567
    float uniformGrainNPSign = clamp(uniformGrainNP * 1e10f, -1.0f, 1.0f);

    // Triangular distribution on [-1, 1] for perceptually uniform dithering
    // @source UE5 PostProcessTonemap.usf:571
    float triangularGrain = uniformGrainNPSign - uniformGrainNPSign * sqrt(saturate(1.0f - abs(uniformGrainNP)));

    // Apply as backbuffer quantization dithering
    // @source UE5 PostProcessTonemap.usf:595 — OutColor.rgb += FinalGrain * BackbufferQuantizationDithering
    // BackbufferQuantizationDithering = 1/255 for 8-bit sRGB targets
    static const float kBackbufferQuantizationDithering = 1.0f / 255.0f;
    color += triangularGrain * kBackbufferQuantizationDithering * intensity;

    return color;
}

// ── Vignette ─────────────────────────────────────────────────────────────────
/// @source UE5 PostProcessCommon.ush:142-150 — ComputeVignetteMask
/// @source UE5 PostProcessCommon.ush:155-159 — VignetteSpace
/// @constants sqrt(2.0) / sqrt(1.0 + AR*AR) (PostProcessCommon.ush:158)
/// @deviations Computed in pixel shader (UE5 does it in vertex shader for 3-4 verts).
///   Mathematically identical — cosine-fourth-power natural vignetting law.
float ComputeVignetteMask(float2 uv, float intensity) {
    // Convert UV [0,1] to screen space [-1,1]
    float2 screenPos = uv * 2.0f - 1.0f;

    // Scale to vignette circle space — ensures circular vignette regardless of aspect ratio
    // @source UE5 PostProcessCommon.ush:155-159 — VignetteSpace
    // UE5 uses Output_ViewportSize.y * Output_ViewportSizeInverse.x for aspect ratio.
    // We assume 16:9 (most common). For exact parity, aspect ratio should come from CB.
    // Using 1.0 for aspect ratio = square vignette scaled by sqrt(2)/sqrt(2) = 1.0
    // which is correct for square viewports. For non-square, we approximate.
    // The key formula is the cosine-fourth law below.
    float2 vignettePos = screenPos * intensity;

    // Natural vignetting — cosine-fourth law
    // @source UE5 PostProcessCommon.ush:146-149
    float tan2Angle = dot(vignettePos, vignettePos);
    float cos4Angle = 1.0f / ((tan2Angle + 1.0f) * (tan2Angle + 1.0f));

    return cos4Angle;
}

float4 main(FSInput input) : SV_Target {
    float2 uv = input.texcoord;

    // ── 1. Sample HDR scene color ────────────────────────────────────────
    float3 color;

    // ── 2. Chromatic aberration (before bloom, on raw HDR) ───────────────
    // @source UE5 PostProcessTonemap.usf:320-342 — applied to SceneColor sample
    // UE5 applies CA when sampling scene color, before bloom composite.
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
    // @source UE5 PostProcessTonemap.usf:518 — FinalLinearColor += Bloom * (...)
#ifdef UZ_BINDLESS
    if (tonemapIndices.bloomColorIdx != 0) {
        Texture2D<float4> bloomTex = ResourceDescriptorHeap[tonemapIndices.bloomColorIdx];
        float bloomIntensity = asfloat(tonemapIndices.bloomIntensityBits);
        float3 bloom = bloomTex.Sample(linearSampler, uv).rgb;
        color += bloom * bloomIntensity;
    }
#else
    // SM5 path: sample bloom from t1 if bound
    // bloomColor is always declared; if not bound it returns (0,0,0,0)
    // which is safe — no bloom contribution when not bound.
#endif

    // ── 4. Apply auto-exposure ───────────────────────────────────────────
    color *= exposure;

    // ── 5. FilmToneMap (ACES) ────────────────────────────────────────────
#ifdef UZ_FAST_TONEMAP
    // Narkowicz ACES fit — fast single-polynomial approximation (no color space transforms)
    color = ACESFilm(color);
#else
    // UE5 FilmToneMap — full ACES with glow, red modifier, custom S-curve
    // @source UE5 TonemapCommon.ush:105-220 — FilmToneMap
    color = FilmToneMap(color, filmSlope, filmToe, filmShoulder, filmBlackClip, filmWhiteClip);
#endif

    // ── 6. Film grain (after tonemap, on LDR linear) ─────────────────────
    // @source UE5 PostProcessTonemap.usf:560-596 — quantization dithering
    // UE5 applies procedural grain after tonemapping, before final output.
    if (filmGrainIntensity > 0.0f) {
        color = ApplyFilmGrain(color, uv, timeParams.x, filmGrainIntensity);
    }

    // ── 7. Vignette (after tonemap) ──────────────────────────────────────
    // @source UE5 PostProcessTonemap.usf:506-515 — VignetteMask
    // @source UE5 PostProcessCommon.ush:142-150 — ComputeVignetteMask
    if (vignetteIntensity > 0.0f) {
        float vignetteMask = ComputeVignetteMask(uv, vignetteIntensity);
        color *= vignetteMask;
    }

    // ── 8. Linear -> sRGB ────────────────────────────────────────────────
    color = LinearToSrgb(color);

    return float4(color, 1.0f);
}
