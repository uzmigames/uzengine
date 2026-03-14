/// Bloom Vertical Gaussian Blur Pass
///
/// Applies a separable Gaussian blur in the vertical direction.
/// Second pass of a two-pass blur pipeline: horizontal (bloom_blur_h_ps.hlsl)
/// followed by this vertical pass.
///
/// # Gaussian Kernel
/// Uses a 5-tap Gaussian kernel with sigma ≈ 1.0:
/// ```
/// weights:  [0.0545, 0.2442, 0.4027, 0.2442, 0.0545]
/// offsets:  [-2.0, -1.0, 0.0, 1.0, 2.0]
/// ```
/// Symmetric filtering reduces aliasing and blur artifacts compared to box filtering.
///
/// # Separable Blur
/// Full 2D Gaussian would require 25 taps. Separable filtering reduces this to
/// 5+5=10 taps total (horizontal pass + vertical pass). This shader handles
/// only the vertical direction; the horizontal pass is in bloom_blur_h_ps.hlsl.
///
/// # Input
/// - Horizontally-blurred bloom texture from bloom_blur_h_ps.hlsl
/// - Screen UVs from fullscreen vertex shader
/// - Texture dimensions (for normalized texel offset)
///
/// # Output
/// - Fully blurred (horizontally + vertically) bloom texture for compositing

// ─────────────────────────────────────────────────────────────────────────────
// Constant Buffers
// ─────────────────────────────────────────────────────────────────────────────

/// Bound at root slot 2 (kSlotPerObject / register b2) via SetRootCBV.
/// Per-pass blur parameters.
cbuffer BlurParams : register(b2) {
    /// Reciprocal of texture height (1.0 / height). Used to compute texel size
    /// for offset sampling. For a 540-pixel-tall texture: 1.0 / 540 ≈ 0.00185.
    float invTextureHeight;

    /// Bloom blur intensity multiplier. Scales the blur contribution.
    /// Values > 1.0 strengthen the blur, < 1.0 weaken it. Default: 1.0.
    float blurIntensity;

    /// Unused padding.
    float _pad0;
    float _pad1;
};

// ─────────────────────────────────────────────────────────────────────────────
// Textures & Samplers
// ─────────────────────────────────────────────────────────────────────────────

#ifdef UZ_BINDLESS
/// Root constants at b0 — SRV heap index for the input bloom texture.
cbuffer RootConstants : register(b0) {
    uint inputTextureIndex;   ///< SRV descriptor heap index for horizontally-blurred bloom.
    uint _rcPad0;
    uint _rcPad1;
    uint _rcPad2;
};
#else
/// Input bloom texture (horizontally-blurred from bloom_blur_h_ps.hlsl).
Texture2D<float4> bloomTexture : register(t0);
#endif

/// Bilinear sampler for smooth interpolation during blur.
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
// Vertical Gaussian Blur
// ─────────────────────────────────────────────────────────────────────────────

/// Main pixel shader — 5-tap vertical Gaussian blur.
///
/// Samples 5 pixels vertically centered on the current UV, weights them
/// with a Gaussian kernel, and returns the blurred result. The horizontal
/// direction has already been blurred in a previous pass; this completes the
/// 2D blur with minimal tap count (5+5=10 instead of 5x5=25).
///
/// # Gaussian Kernel (sigma = 1.0)
/// Weights are computed via the 1D Gaussian formula:
/// w(x) = exp(-(x^2) / (2 * sigma^2)) / sqrt(2 * pi * sigma^2)
///
/// Normalized weights sum to 1.0 to preserve energy.
///
/// # Filtering
/// Each texel is offset by invTextureHeight to step vertically.
/// The bilinear sampler handles sub-texel interpolation within the mip level.
///
/// # Returns
/// Fully blurred (horizontal + vertical) HDR bloom color, ready for composite.
float4 main(PSInput input) : SV_Target {
    float2 texCoord = input.texcoord;
    float texelOffset = invTextureHeight;

    // 5-tap Gaussian kernel (normalized)
    // Weights for offsets [-2, -1, 0, 1, 2]
    float weights[5] = {
        0.0545f,   // offset -2
        0.2442f,   // offset -1
        0.4027f,   // offset  0 (center)
        0.2442f,   // offset +1
        0.0545f    // offset +2
    };

    float3 result = float3(0.0f, 0.0f, 0.0f);

    // Apply 5-tap filter: sample at [-2, -1, 0, +1, +2] texels vertically
#ifdef UZ_BINDLESS
    Texture2D<float4> inputTex = ResourceDescriptorHeap[inputTextureIndex];
#endif
    for (int i = -2; i <= 2; ++i) {
        float2 sampleTexCoord = texCoord + float2(0.0f, i * texelOffset);
#ifdef UZ_BINDLESS
        float3 sampleColor = inputTex.Sample(samplerLinear, sampleTexCoord).rgb;
#else
        float3 sampleColor = bloomTexture.Sample(samplerLinear, sampleTexCoord).rgb;
#endif
        result += sampleColor * weights[i + 2];
    }

    // Apply blur intensity multiplier
    result *= blurIntensity;

    return float4(result, 1.0f);
}
