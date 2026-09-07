/// UzEngine tonemap pixel shader.
/// @ref Narkowicz 2015 "ACES Filmic Tone Mapping Curve"
///
/// Pipeline: chromatic aberration → HDR sample → bloom composite →
///           exposure → saturation → contrast → ACES → LUT → grain → vignette → sRGB

#include "../common/constants.hlsli"
#include "../common/math.hlsli"
#include "../common/color.hlsli"
#include "../common/bindless.hlsli"

// ─── Root constants at b0 (5 DWORDs) ──────────────────────────────────────

cbuffer RootConstants : register(b0)
{
    uint hdrSrvIndex;
    uint bloomSrvIndex;
    uint bloomIntensityBits;  // float reinterpreted as uint
    uint lensFlareSrvIndex;
    uint lutSrvIndex;
    uint tonemapOperator;     // 0=Hill ACES, 1=Narkowicz ACES, 2=Reinhard, 3=Hable, 4=Linear
};

// ─── PerFrameCB at b1 (768 bytes — only need post-process fields) ─────────

cbuffer PerFrameCB : register(b1)
{
    row_major float4x4 _viewProj;
    float4 _camPos, timeParams, _lightDir, _lightColor, _ambient;
    row_major float4x4 _invViewProj;
    float _near, _far;
    uint _irr, _pre, _brdf, _mips;
    float _skyInt, _skyCR, _skyCG, _skyCB, _skyMin;
    float exposure;
    float4 _sh[7];
    uint _sDbg, _apEn, _tLut;
    float _apBot, _apTop, _rBX, _rBY, _rBZ, _rH;
    float _mB, _mH, _mA, _sI, _rS, _mS;
    float _tcs, _tct, _tcsh, _tcbc, _tcwc;
    uint _gtao, _brdfDbg;
    float _fogCont;
    float filmGrainIntensity;
    float vignetteIntensity;
    float chromaAbIntensity;
    float _contact;
    uint _ssrSrv;
    float _mipBias;
    uint _ssgiSrv, _ssgiAO, _ssgiEn;
    uint _rcEn, _rcSH, _rcP;
    float _rcE, _rcOX, _rcOY, _rcOZ, _rcI;
    uint _apVol, _rpCnt, _rpBuf;
    float colorSaturation;
    float colorContrast;
};

SamplerState samLinear : register(s0);
SamplerState samClamp  : register(s2);

#ifndef UZ_BINDLESS
Texture2D t_hdr      : register(t0);
Texture2D t_bloom    : register(t1);
Texture2D t_lensFlare : register(t3);
Texture3D t_lut      : register(t4);
#endif

struct PSIn { float4 position : SV_Position; float2 texCoord : TEXCOORD0; };

float4 main(PSIn i) : SV_Target
{
    float2 uv = i.texCoord;

    // ── Passthrough mode (tonemapOperator == 0) ─────────────────────────
    // Scene lighting was calibrated without tonemapping. Default mode
    // outputs HDR scene color directly (bloom composited, no exposure/sRGB
    // adjustments). Enable ACES via r.TonemapOperator=1.
#ifdef UZ_BINDLESS
    if (tonemapOperator == 0)
    {
        Texture2D<float4> hdr = ResourceDescriptorHeap[hdrSrvIndex];
        float3 color = hdr.SampleLevel(samClamp, uv, 0).rgb;

        // Composite bloom in passthrough mode
        if (bloomSrvIndex > 0)
        {
            float bloomInt = asfloat(bloomIntensityBits);
            if (bloomInt > 0.0)
            {
                Texture2D<float4> bloomTex = ResourceDescriptorHeap[bloomSrvIndex];
                color += bloomTex.SampleLevel(samClamp, uv, 0).rgb * bloomInt;
            }
        }
        if (lensFlareSrvIndex > 0)
        {
            Texture2D<float4> flareTex = ResourceDescriptorHeap[lensFlareSrvIndex];
            color += flareTex.SampleLevel(samClamp, uv, 0).rgb;
        }

        return float4(color, 1.0);
    }
#endif

    // ── Chromatic aberration ──────────────────────────────────────────────
    float2 uvR = uv, uvG = uv, uvB = uv;
    if (chromaAbIntensity > 0.0)
    {
        float2 dir = uv - 0.5;
        float dist = length(dir);
        float2 offset = dir * dist * chromaAbIntensity;
        uvR = uv + offset;
        uvB = uv - offset;
    }

    // ── Sample HDR scene ──────────────────────────────────────────────────
    float3 color;
#ifdef UZ_BINDLESS
    Texture2D<float4> hdrTex = ResourceDescriptorHeap[hdrSrvIndex];
    if (chromaAbIntensity > 0.0)
    {
        color.r = hdrTex.SampleLevel(samClamp, uvR, 0).r;
        color.g = hdrTex.SampleLevel(samClamp, uvG, 0).g;
        color.b = hdrTex.SampleLevel(samClamp, uvB, 0).b;
    }
    else
    {
        color = hdrTex.SampleLevel(samClamp, uv, 0).rgb;
    }
#else
    if (chromaAbIntensity > 0.0)
    {
        color.r = t_hdr.SampleLevel(samClamp, uvR, 0).r;
        color.g = t_hdr.SampleLevel(samClamp, uvG, 0).g;
        color.b = t_hdr.SampleLevel(samClamp, uvB, 0).b;
    }
    else
    {
        color = t_hdr.SampleLevel(samClamp, uv, 0).rgb;
    }
#endif

    // ── Bloom composite ───────────────────────────────────────────────────
    float bloomIntensity = asfloat(bloomIntensityBits);

    // Bloom composite

#ifdef UZ_BINDLESS
    if (bloomSrvIndex > 0 && bloomIntensity > 0.0)
    {
        Texture2D<float4> bloomTex = ResourceDescriptorHeap[bloomSrvIndex];
        color += bloomTex.SampleLevel(samClamp, uv, 0).rgb * bloomIntensity;
    }
    if (lensFlareSrvIndex > 0)
    {
        Texture2D<float4> flareTex = ResourceDescriptorHeap[lensFlareSrvIndex];
        color += flareTex.SampleLevel(samClamp, uv, 0).rgb;
    }
#else
    if (bloomIntensity > 0.0)
        color += t_bloom.SampleLevel(samClamp, uv, 0).rgb * bloomIntensity;
#endif

    // ── Exposure ──────────────────────────────────────────────────────────
    color *= exposure;

    // ── Saturation ────────────────────────────────────────────────────────
    float luma = Luminance(color);
    color = max(lerp(float3(luma, luma, luma), color, colorSaturation), 0.0);

    // ── Contrast ──────────────────────────────────────────────────────────
    float midgray = 0.18;
    color = pow(max(color / midgray, 0.0), colorContrast) * midgray;

    // ── Tone Mapping ─────────────────────────────────────────────────────
    // Selectable operator via root constant (CVar r.TonemapOperator).
    // No pre-exposure — scene lighting is already calibrated for these curves.
#ifdef UZ_BINDLESS
    switch (tonemapOperator)
    {
    case 1: color = ACESHillFit(color); break;                     // @ref Hill 2016 "ACES Fitted Curves"
    case 2: color = ACESFilmic(color); break;                      // @ref Narkowicz 2015 "ACES Filmic"
    case 3: color = ReinhardTonemap(color); break;                 // @ref Reinhard 2002
    case 4: color = HableUncharted2(color) / HableUncharted2(float3(11.2,11.2,11.2)); break; // @ref Hable 2010
    default:
    case 0: break;                                                  // None (exposure + sat + contrast only)
    }
#else
    // SM5 path: no tonemap curve (matches default behavior)
#endif

    // ── 3D LUT color grading ──────────────────────────────────────────────
#ifdef UZ_BINDLESS
    if (lutSrvIndex > 0)
    {
        float3 lutUV = color * (31.0 / 32.0) + (0.5 / 32.0);
        Texture3D<float4> lutTex = ResourceDescriptorHeap[lutSrvIndex];
        color = lutTex.SampleLevel(samClamp, lutUV, 0).rgb;
    }
#endif

    // ── Film grain ────────────────────────────────────────────────────────
    if (filmGrainIntensity > 0.0)
    {
        float noise = InterleavedGradientNoise(i.position.xy, uint(timeParams.z)) * 2.0 - 1.0;
        color += noise * filmGrainIntensity * 0.05;
    }

    // ── Vignette ──────────────────────────────────────────────────────────
    if (vignetteIntensity > 0.0)
    {
        float2 d = uv - 0.5;
        float vign = 1.0 - dot(d, d) * vignetteIntensity * 4.0;
        color *= saturate(vign);
    }

    // ── Linear → sRGB ────────────────────────────────────────────────────
    color = LinearToSRGB(color);

    return float4(color, 1.0);
}
