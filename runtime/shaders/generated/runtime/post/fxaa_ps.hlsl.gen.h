// fxaa_ps.hlsl.gen.h -- AUTO-GENERATED, DO NOT EDIT
#pragma once
namespace uz::generated {
inline constexpr char kFxaaPS[] = R"hlsl(
/// FXAA (Fast Approximate Anti-Aliasing) — Fast GPU-based AA
///
/// Implements a simplified version of FXAA to reduce aliasing artifacts
/// without the cost of supersampling or temporal AA.
///
/// # Algorithm
/// FXAA detects edges in the image by computing per-pixel luminance differences
/// and then blurs across detected edges. The algorithm runs as a post-process
/// after rendering and tone mapping.
///
/// # Edge Detection
/// Computes the contrast of a pixel vs. its 4 neighbors (up/down/left/right).
/// If contrast exceeds `edgeThreshold`, the pixel is considered on an edge.
///
/// # Blend
/// For edge pixels, samples along the edge direction and blurs perpendicular
/// to reduce the aliasing pattern. Non-edge pixels are returned unchanged.
///
/// # Quality vs. Performance
/// FXAA is ~5x faster than 2xMSAA and provides similar visual quality for
/// high-frequency edges (diagonal lines, text). Effectiveness depends on
/// edge contrast and view angle.
///
/// # Input
/// - LDR scene color (after tone mapping and bloom composite)
/// - Screen UVs from fullscreen vertex shader
/// - Texture dimensions for neighbor sampling
///
/// # Output
/// - Anti-aliased scene color

// ─────────────────────────────────────────────────────────────────────────────
// Constant Buffers
// ─────────────────────────────────────────────────────────────────────────────

/// Bound at root slot 2 (kSlotPerObject / register b2) via SetRootCBV.
/// Per-frame FXAA parameters.
cbuffer FXAAParams : register(b2) {
    /// Reciprocal of viewport width (1.0 / width). Used for texel offset.
    float invScreenWidth;

    /// Reciprocal of viewport height (1.0 / height). Used for texel offset.
    float invScreenHeight;

    /// Edge detection threshold. Controls how sensitive edge detection is.
    /// Typical range: [0.05, 0.2]. Default: 0.1.
    /// Higher values detect only high-contrast edges; lower values detect subtle transitions.
    float edgeThreshold;

    /// Blur strength multiplier. Scales the blur amount for edge pixels.
    /// Typical range: [0.5, 2.0]. Default: 1.0.
    float blurStrength;
};

// ─────────────────────────────────────────────────────────────────────────────
// Textures & Samplers
// ─────────────────────────────────────────────────────────────────────────────

#ifdef UZ_BINDLESS
/// Root constants at b0 — SRV heap index for the input scene color texture.
cbuffer RootConstants : register(b0) {
    uint inputTextureIndex;   ///< SRV descriptor heap index for LDR scene color.
    uint _rcPad0;
    uint _rcPad1;
    uint _rcPad2;
};
#else
/// Scene color input (LDR, after tone mapping and bloom).
Texture2D<float4> sceneColor : register(t0);
#endif

/// Point sampler for precise per-pixel and per-neighbor lookups.
SamplerState samplerPoint : register(s0);

// ─────────────────────────────────────────────────────────────────────────────
// Input & Output Structures
// ─────────────────────────────────────────────────────────────────────────────

/// Pixel shader input — fullscreen triangle interpolants.
struct PSInput {
    float4 positionCS : SV_Position;  ///< Clip-space position.
    float2 texcoord   : TEXCOORD0;    ///< Screen UV in [0, 1].
};

// ─────────────────────────────────────────────────────────────────────────────
// Luminance & Edge Detection
// ─────────────────────────────────────────────────────────────────────────────

/// Compute relative luminance from RGB (linear space assumed).
///
/// Uses standard relative luminance formula (ITU-R BT.709):
/// L = 0.2126 * R + 0.7152 * G + 0.0722 * B
///
/// # Parameters
/// - rgb: Linear color.
///
/// # Returns
/// Scalar luminance.
float Luminance(float3 rgb) {
    return dot(rgb, float3(0.2126f, 0.7152f, 0.0722f));
}

// ─────────────────────────────────────────────────────────────────────────────
// FXAA Edge Detection & Blur
// ─────────────────────────────────────────────────────────────────────────────

/// Main pixel shader — FXAA edge detection and smoothing.
///
/// Samples the current pixel and its 4 neighbors (up/down/left/right).
/// Computes per-pixel luminance and detects edges via contrast.
/// For edge pixels, applies a directional blur to reduce aliasing.
///
/// # Edge Detection
/// Contrast = max(L_up, L_down, L_left, L_right) - min(L_up, L_down, L_left, L_right)
/// If contrast >= edgeThreshold, the pixel is on an edge.
///
/// # Blur Direction
/// Determines whether the edge is more horizontal or vertical by comparing
/// vertical vs. horizontal luminance differences. Blurs perpendicular to
/// the edge direction to smooth the transition.
///
/// # Returns
/// Blurred color for edge pixels, original color for flat regions.
float4 main(PSInput input) : SV_Target {
    float2 texCoord = input.texcoord;
    float texelWidth = invScreenWidth;
    float texelHeight = invScreenHeight;

    // Sample center and 4 neighbors (up, down, left, right)
#ifdef UZ_BINDLESS
    Texture2D<float4> inputTex = ResourceDescriptorHeap[inputTextureIndex];
    float3 centerColor = inputTex.Sample(samplerPoint, texCoord).rgb;
    float3 upColor    = inputTex.Sample(samplerPoint, texCoord + float2(0.0f, -texelHeight)).rgb;
    float3 downColor  = inputTex.Sample(samplerPoint, texCoord + float2(0.0f,  texelHeight)).rgb;
    float3 leftColor  = inputTex.Sample(samplerPoint, texCoord - float2(texelWidth, 0.0f)).rgb;
    float3 rightColor = inputTex.Sample(samplerPoint, texCoord + float2(texelWidth, 0.0f)).rgb;
#else
    float3 centerColor = sceneColor.Sample(samplerPoint, texCoord).rgb;
    float3 upColor    = sceneColor.Sample(samplerPoint, texCoord + float2(0.0f, -texelHeight)).rgb;
    float3 downColor  = sceneColor.Sample(samplerPoint, texCoord + float2(0.0f,  texelHeight)).rgb;
    float3 leftColor  = sceneColor.Sample(samplerPoint, texCoord - float2(texelWidth, 0.0f)).rgb;
    float3 rightColor = sceneColor.Sample(samplerPoint, texCoord + float2(texelWidth, 0.0f)).rgb;
#endif

    // Compute luminance for each
    float centerLum = Luminance(centerColor);
    float upLum     = Luminance(upColor);
    float downLum   = Luminance(downColor);
    float leftLum   = Luminance(leftColor);
    float rightLum  = Luminance(rightColor);

    // Edge detection: compute vertical and horizontal contrast
    float maxLum = max(centerLum, max(max(upLum, downLum), max(leftLum, rightLum)));
    float minLum = min(centerLum, min(min(upLum, downLum), min(leftLum, rightLum)));
    float contrast = maxLum - minLum;

    // If contrast below threshold, no edge — return original color
    if (contrast < edgeThreshold) {
        return float4(centerColor, 1.0f);
    }

    // Edge detected: determine if horizontal or vertical edge
    float verticalContrast = abs(upLum - centerLum) + abs(downLum - centerLum);
    float horizontalContrast = abs(leftLum - centerLum) + abs(rightLum - centerLum);

    float3 blurResult;
    if (verticalContrast > horizontalContrast) {
        // Vertical edge (sharper top-bottom transition) -> blur horizontally
        blurResult = (leftColor + rightColor + centerColor) / 3.0f;
    } else {
        // Horizontal edge (sharper left-right transition) -> blur vertically
        blurResult = (upColor + downColor + centerColor) / 3.0f;
    }

    // Blend between original and blurred based on contrast and blur strength
    float3 output = lerp(centerColor, blurResult, blurStrength * (contrast / (contrast + 1.0f)));

    return float4(output, 1.0f);
}
)hlsl";
} // namespace uz::generated
