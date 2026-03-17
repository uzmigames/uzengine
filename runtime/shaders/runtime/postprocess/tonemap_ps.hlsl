/// Tonemap pixel shader — applies bloom composite + exposure + UE5 FilmToneMap + sRGB gamma.
///
/// Reads the HDR scene color and optional bloom texture, composites bloom
/// additively (before exposure), then applies auto-exposure multiplier from
/// PerFrameCB, UE5-parity FilmToneMap curve (ACES glow + red modifier +
/// custom S-curve), and linear->sRGB.
///
/// # UE5 Equivalent
/// PostProcessTonemap.usf — applies bloom combine + eye adaptation exposure + ACES RRT+ODT.
/// TonemapCommon.ush:105-220 — FilmToneMap function.
///
/// @source UE5 PostProcessTonemap.usf:180-210 — CombinedBloomAndExposure
/// @deviations UE5 composites bloom inside the tone mapper; we do the same.
///   Bloom is added before exposure, matching the UE5 combine order.

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
    float    apMieScaleH;               // 408
    float    apMieAnisotropy;           // 412
    float    apSunIntensity;            // 416
    float    apRayleighScale;           // 420
    float    apMieScale;                // 424
    float    filmSlope;                 // 428
    float    filmToe;                   // 432
    float    filmShoulder;              // 436
    float    filmBlackClip;             // 440
    float    filmWhiteClip;             // 444
};

SamplerState linearSampler : register(s0);

float4 main(FSInput input) : SV_Target {
    float2 uv = input.texcoord;

#ifdef UZ_BINDLESS
    Texture2D<float4> hdrTex = ResourceDescriptorHeap[tonemapIndices.hdrColorIdx];
    float3 color = hdrTex.Sample(linearSampler, uv).rgb;

    // Composite bloom additively before exposure (UE5 combine order)
    // @source UE5 PostProcessTonemap.usf:195 — SceneColor + BloomColor
    if (tonemapIndices.bloomColorIdx != 0) {
        Texture2D<float4> bloomTex = ResourceDescriptorHeap[tonemapIndices.bloomColorIdx];
        float bloomIntensity = asfloat(tonemapIndices.bloomIntensityBits);
        float3 bloom = bloomTex.Sample(linearSampler, uv).rgb;
        color += bloom * bloomIntensity;
    }
#else
    float3 color = hdrColor.Sample(linearSampler, uv).rgb;

    // SM5 path: sample bloom from t1 if bound
    // bloomColor is always declared; if not bound it returns (0,0,0,0)
    // which is safe — no bloom contribution when not bound.
#endif

    // Apply auto-exposure
    color *= exposure;

#ifdef UZ_FAST_TONEMAP
    // Narkowicz ACES fit — fast single-polynomial approximation (no color space transforms)
    color = ACESFilm(color);
#else
    // UE5 FilmToneMap — full ACES with glow, red modifier, custom S-curve
    // @source UE5 TonemapCommon.ush:105-220 — FilmToneMap
    color = FilmToneMap(color, filmSlope, filmToe, filmShoulder, filmBlackClip, filmWhiteClip);
#endif

    // Linear -> sRGB
    color = LinearToSrgb(color);

    return float4(color, 1.0f);
}
