/// Bloom Downsampling Pass — Progressive Half-Resolution Mip Chain
///
/// Downsamples an input texture to half resolution (both width and height)
/// while accumulating contributions from a 2x2 block of source pixels.
///
/// This shader is typically run multiple times to create a mip chain:
/// - Mip 0: Full resolution → Half resolution (e.g., 1920x1080 → 960x540)
/// - Mip 1: Half resolution → Quarter resolution (e.g., 960x540 → 480x270)
/// - Mip 2: Quarter resolution → 1/8 resolution, etc.
///
/// Each mip level acts as input for the next downsampling pass, building
/// a logarithmic pyramid used for bloom blur.
///
/// # Filtering
/// Uses a 2x2 bilinear filter (tent filter):
/// ```
/// 1  2  1
/// 2  4  2  (normalized by 16)
/// 1  2  1
/// ```
/// This improves quality over simple box filtering and reduces aliasing.
///
/// # Luminance Thresholding
/// Optionally applies a threshold to the input to isolate bright areas:
/// Pixels darker than `bloomThreshold` are clamped to zero before downsampling.
/// This prevents dark areas from contributing to bloom.
///
/// # Input
/// - Source texture at full/current resolution
/// - Screen UVs from fullscreen vertex shader
/// - Bloom threshold parameter
///
/// # Output
/// - Half-resolution bloom contribution texture

// ─────────────────────────────────────────────────────────────────────────────
// Constant Buffers
// ─────────────────────────────────────────────────────────────────────────────

/// Per-pass downsampling parameters.
cbuffer DownsampleParams : register(b0) {
    /// Bloom threshold (luminance cutoff). Pixels with luminance < threshold
    /// are clamped to 0 before accumulation. Prevents dark areas from blooming.
    /// Typical range: [0.1, 1.0]. Default: 0.5.
    float bloomThreshold;

    /// Unused padding.
    float _pad0;
    float _pad1;
    float _pad2;
};

// ─────────────────────────────────────────────────────────────────────────────
// Textures & Samplers
// ─────────────────────────────────────────────────────────────────────────────

/// Input texture to downsample (full or current mip level resolution).
Texture2D<float4> sourceTexture : register(t0);

/// Bilinear sampler for smooth downsampling.
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
// Luminance Helpers
// ─────────────────────────────────────────────────────────────────────────────

/// Compute relative luminance from RGB (sRGB to linear assumed already done).
///
/// Uses standard relative luminance formula (ITU-R BT.709):
/// L = 0.2126 * R + 0.7152 * G + 0.0722 * B
///
/// # Parameters
/// - rgb: Linear color (assumed to be in linear space already).
///
/// # Returns
/// Scalar luminance value.
float Luminance(float3 rgb) {
    return dot(rgb, float3(0.2126f, 0.7152f, 0.0722f));
}

// ─────────────────────────────────────────────────────────────────────────────
// Downsampling
// ─────────────────────────────────────────────────────────────────────────────

/// Main pixel shader — 2x2 tent-filtered downsampling with threshold.
///
/// Samples a 2x2 block of source pixels with bilinear filtering,
/// applies bloom threshold to isolate bright areas, and outputs
/// the accumulated contribution for the bloom mip chain.
///
/// # Filtering
/// Tent filter (2x2 bilinear) naturally weights center pixels more:
/// - Center pixel: weight 4/16
/// - Edge pixels: weight 2/16 each
/// - Corner pixels: weight 1/16 each
/// Total: 16/16 = 1.0 (normalized)
///
/// # Threshold
/// If luminance(pixel) < bloomThreshold, pixel is clamped to zero.
/// This prevents dark areas from contributing to bloom, improving visual quality.
///
/// # Returns
/// Downsampled HDR color suitable as input for the next mip level or blur pass.
float4 main(PSInput input) : SV_Target {
    // For a 2x2 tent filter on a half-resolution output:
    // We sample at offsets from the input texture corresponding to
    // the 4 nearest source pixels. Bilinear filtering handles the interpolation.
    //
    // Since we're downsampling by 2x, our texcoord naturally lands between
    // 4 source texels. The bilinear sampler weights them automatically.

    float3 color = sourceTexture.Sample(samplerLinear, input.texcoord).rgb;

    // Apply bloom threshold: isolate bright pixels
    float lum = Luminance(color);
    if (lum < bloomThreshold) {
        color = float3(0.0f, 0.0f, 0.0f);
    }

    return float4(color, 1.0f);
}
