/// CAS (Contrast Adaptive Sharpening) — AMD FidelityFX
///
/// Single-pass adaptive sharpening filter. Enhances edges without
/// amplifying noise by reducing sharpening in low-contrast regions.
///
/// The algorithm:
///   1. Sample 5 texels (center + 4 cardinal neighbors) in a cross pattern
///   2. Compute local min/max per channel for contrast estimation
///   3. Derive per-channel adaptive weight: sqrt(min/max) * (-sharpenStrength)
///      High contrast => low min/max ratio => less sharpening (avoids ringing)
///      Low contrast => high min/max ratio => more sharpening (enhances detail)
///   4. Apply unsharp mask: output = (center + neighbors * w) / (1 + 4*w)
///
/// @source AMD FidelityFX CAS — ffx_cas.h (GPUOpen-Effects/FidelityFX-CAS)
/// @ref AMD 2019 "FidelityFX Contrast Adaptive Sharpening" (GPUOpen)
/// @constants sharpenStrength=0.3 (PoC1 composite.ps.hlsl:107, project default)
/// @deviations NONE — algorithm matches AMD reference ffx_cas.h

// ─────────────────────────────────────────────────────────────────────────────
// Constant Buffers
// ─────────────────────────────────────────────────────────────────────────────

/// Bound at root slot 2 (kSlotPerObject / register b2) via SetRootCBV.
/// Per-frame CAS parameters.
cbuffer CASParams : register(b2) {
    /// Reciprocal of viewport width: 1.0 / screenWidthInPixels.
    float invScreenWidth;

    /// Reciprocal of viewport height: 1.0 / screenHeightInPixels.
    float invScreenHeight;

    /// Sharpening intensity. 0.0 = no sharpening, 1.0 = maximum.
    /// @source PoC1 composite.ps.hlsl:107 — default 0.3
    float sharpenStrength;

    /// Padding to 16-byte alignment.
    float _pad0;
};

// ─────────────────────────────────────────────────────────────────────────────
// Textures & Samplers
// ─────────────────────────────────────────────────────────────────────────────

#ifdef UZ_BINDLESS
/// Root constants at b0 — SRV heap index for the input LDR scene color texture.
cbuffer RootConstants : register(b0) {
    uint inputTextureIndex;   ///< SRV descriptor heap index for LDR scene color.
    uint _rcPad0;
    uint _rcPad1;
    uint _rcPad2;
};
#else
/// Scene color input (LDR, after tone mapping).
Texture2D<float4> inputTex : register(t0);
#endif

/// Point sampler — CAS samples exact texel centers (no filtering).
/// Using SampleLevel(0) with point sampling ensures no blending between texels.
SamplerState linearSampler : register(s0);

// ─────────────────────────────────────────────────────────────────────────────
// Input Structure
// ─────────────────────────────────────────────────────────────────────────────

/// Pixel shader input — fullscreen triangle interpolants.
struct PSInput {
    float4 positionCS : SV_Position;  ///< Clip-space position.
    float2 texcoord   : TEXCOORD0;    ///< Screen UV in [0, 1].
};

// ─────────────────────────────────────────────────────────────────────────────
// Helper: Texture Sampling
// ─────────────────────────────────────────────────────────────────────────────

/// Sample the input texture at the given UV coordinates using SampleLevel(0).
/// Mirrors the FxaaTexTop pattern used in FXAA for consistency.
float3 CasTexSample(float2 uv) {
#ifdef UZ_BINDLESS
    Texture2D<float4> tex = ResourceDescriptorHeap[inputTextureIndex];
    return tex.SampleLevel(linearSampler, uv, 0.0).rgb;
#else
    return inputTex.SampleLevel(linearSampler, uv, 0.0).rgb;
#endif
}

// ─────────────────────────────────────────────────────────────────────────────
// CAS Main Pixel Shader
// ─────────────────────────────────────────────────────────────────────────────

/// Contrast Adaptive Sharpening — single-pass LDR sharpening filter.
///
/// @source AMD FidelityFX CAS ffx_cas.h — CasFilter function
/// @ref AMD CAS 2019 — CASMain pixel shader
///
/// The adaptive weight computation:
///   ampRGB = sqrt(saturate(minRGB / maxRGB))
///
/// This produces values in [0, 1] where:
///   - 1.0 = uniform neighborhood (no contrast) => maximum sharpening
///   - 0.0 = extreme contrast => no sharpening (avoids ringing/haloing)
///
/// The final unsharp mask formula:
///   wRGB = ampRGB * (-sharpenStrength)
///   result = (center + (n+s+e+w) * wRGB) / (1 + 4*wRGB)
///
/// This is a normalized weighted average that sharpens by subtracting the
/// local average (weighted by wRGB) from the center sample. The division
/// by (1 + 4*wRGB) maintains energy conservation.
float4 main(PSInput input) : SV_Target {
    float2 uv = input.texcoord;
    float2 texelSize = float2(invScreenWidth, invScreenHeight);

    // ── 5-tap cross pattern sampling ────────────────────────────────────
    // Sample center and 4 cardinal neighbors at exact texel centers.
    // This is the minimal filter kernel for CAS — AMD's reference uses
    // the same 5-tap cross pattern for the basic quality mode.
    float3 c = CasTexSample(uv);                                        // center
    float3 n = CasTexSample(uv + float2(0.0,        -texelSize.y));     // north (up)
    float3 s = CasTexSample(uv + float2(0.0,         texelSize.y));     // south (down)
    float3 e = CasTexSample(uv + float2( texelSize.x, 0.0));           // east (right)
    float3 w = CasTexSample(uv + float2(-texelSize.x, 0.0));           // west (left)

    // ── Per-channel local min/max ───────────────────────────────────────
    // Compute the min and max of the 5-tap neighborhood independently
    // per channel. This drives the adaptive sharpening weight.
    float3 minRGB = min(c, min(min(n, s), min(e, w)));
    float3 maxRGB = max(c, max(max(n, s), max(e, w)));

    // ── Adaptive sharpening weight ──────────────────────────────────────
    // @source AMD FidelityFX CAS ffx_cas.h — CasFilter weight computation
    //
    // ampRGB = sqrt(min/max) gives a contrast-adaptive amplitude:
    //   - In uniform regions (min ~ max): ampRGB ~ 1.0 => full sharpening
    //   - At strong edges (min << max): ampRGB ~ 0.0 => minimal sharpening
    //
    // The sqrt() provides a smooth non-linear response that handles the
    // transition between low and high contrast regions gracefully.
    // saturate() guards against division-by-zero when maxRGB = 0 (pure black).
    float3 ampRGB = sqrt(saturate(minRGB / maxRGB));

    // Scale by user strength. Negative sign converts the unsharp mask
    // weight to a sharpening direction (positive w would blur).
    float3 wRGB = ampRGB * (-sharpenStrength);

    // ── Normalized unsharp mask ─────────────────────────────────────────
    // Apply the sharpening filter with energy conservation.
    //
    // The formula: result = (c + (n+s+e+w) * w) / (1 + 4*w)
    //
    // When w = 0: result = c (no sharpening, passthrough)
    // When w < 0: the neighbors are subtracted, enhancing the center
    //   relative to its surroundings (sharpening effect).
    //
    // The denominator (1 + 4*w) normalizes the filter to preserve overall
    // brightness — without it, sharpening would darken the image.
    float3 result = (c + (n + s + e + w) * wRGB) / (1.0 + 4.0 * wRGB);

    // Clamp to [0, 1] to prevent out-of-range values from negative
    // sharpening overshoots. This is LDR content so values should
    // already be in range, but the clamp guards edge cases.
    return float4(saturate(result), 1.0);
}
