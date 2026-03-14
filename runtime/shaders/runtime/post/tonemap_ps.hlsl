/// ACES Filmic Tone Mapping — HDR to LDR conversion
///
/// Implements the ACES (Academy Color Encoding System) RRT+ODT pipeline
/// to map HDR radiance to LDR display range [0, 1].
///
/// # Formula
/// The tone curve is approximated as:
/// ```
/// numerator   = x * (2.51 * x + 0.03)
/// denominator = x * (2.43 * x + 0.59) + 0.14
/// output      = saturate(numerator / denominator)
/// ```
///
/// This approximates the ACES tone curve without the full matrix operations.
/// Input HDR values are expected to be in radiance (can exceed [0, 1]).
///
/// # Input
/// - HDR scene color texture (any format >= RG16F)
/// - Screen UVs from fullscreen vertex shader
///
/// # Output
/// - LDR color in [0, 1] range, suitable for 8-bit display

// ─────────────────────────────────────────────────────────────────────────────
// Constant Buffers
// ─────────────────────────────────────────────────────────────────────────────

/// Bound at root slot 2 (kSlotPerObject / register b2) via SetRootCBV.
/// Per-frame tone mapping parameters.
cbuffer PostProcessParams : register(b2) {
    /// Global tone mapping intensity. Values > 1.0 brighten the image,
    /// < 1.0 darken. Typical range: [0.5, 1.5]. Default: 1.0.
    float tonemapIntensity;

    /// Exposure adjustment applied before tone mapping (log2 scale).
    /// +1.0 = 2x brighter, -1.0 = 2x darker. Default: 0.0.
    float exposureAdjust;

    /// When > 0.5, bypasses ACES curve and outputs saturate(hdr * intensity).
    /// Used for passthrough blit when tone mapping is disabled.
    float bypassTonemap;
    float _pad1;
};

// ─────────────────────────────────────────────────────────────────────────────
// Textures & Samplers
// ─────────────────────────────────────────────────────────────────────────────

#ifdef UZ_BINDLESS
/// Root constants at b0 — SRV heap index for the input HDR scene color texture.
cbuffer RootConstants : register(b0) {
    uint inputTextureIndex;   ///< SRV descriptor heap index for HDR scene color.
    uint _rcPad0;
    uint _rcPad1;
    uint _rcPad2;
};
#else
/// HDR scene color output from the main render passes (e.g., lighting pass output).
Texture2D<float4> sceneColor : register(t0);
#endif

/// Bilinear sampler for scene color lookup.
SamplerState samplerLinear : register(s0);

// ─────────────────────────────────────────────────────────────────────────────
// Input & Output Structures
// ─────────────────────────────────────────────────────────────────────────────

/// Pixel shader input — fullscreen triangle interpolants.
struct PSInput {
    float4 positionCS : SV_Position;  ///< Clip-space position.
    float2 texcoord   : TEXCOORD0;    ///< Screen UV in [0, 1].
};

// ─────────────────────────────────────────────────────────────────────────────
// Tone Mapping
// ─────────────────────────────────────────────────────────────────────────────

/// ACES approximation tone mapping function.
///
/// Maps HDR radiance to LDR [0, 1] using a rational approximation
/// of the ACES tone curve. Input can be any non-negative value.
///
/// # Parameters
/// - hdrColor: Linear HDR color (can exceed [0, 1]).
///
/// # Returns
/// Tone-mapped color clamped to [0, 1].
float3 ACESTonemapFast(float3 hdrColor) {
    float3 x = hdrColor;
    float3 numerator = x * (2.51f * x + 0.03f);
    float3 denominator = x * (2.43f * x + 0.59f) + 0.14f;
    return saturate(numerator / (denominator + 1e-6f));  // Add epsilon to avoid division by zero
}

/// Main pixel shader entry point.
///
/// Samples the HDR scene color, applies exposure adjustment, applies
/// ACES tone mapping, and returns the final LDR color.
///
/// # Returns
/// LDR color in [0, 1] range for display.
float4 main(PSInput input) : SV_Target {
    // Sample HDR scene color
#ifdef UZ_BINDLESS
    Texture2D<float4> inputTex = ResourceDescriptorHeap[inputTextureIndex];
    float3 hdrColor = inputTex.Sample(samplerLinear, input.texcoord).rgb;
#else
    float3 hdrColor = sceneColor.Sample(samplerLinear, input.texcoord).rgb;
#endif

    // Apply exposure adjustment (linear scale: 2^exposureAdjust)
    hdrColor *= exp2(exposureAdjust);

    float3 ldrColor;
    if (bypassTonemap > 0.5f) {
        // Passthrough — just clamp HDR to [0,1] without tone curve
        ldrColor = saturate(hdrColor * tonemapIntensity);
    } else {
        // Apply ACES tone mapping
        ldrColor = ACESTonemapFast(hdrColor);
        ldrColor *= tonemapIntensity;
        ldrColor = saturate(ldrColor);
    }

    return float4(ldrColor, 1.0f);
}
