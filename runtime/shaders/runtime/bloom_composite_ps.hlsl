/// Bloom Composite Pass — Blend Blurred Bloom onto Scene
///
/// Combines the blurred bloom texture with the original scene color
/// using additive blending. This final post-processing step adds a glow
/// effect to bright areas of the scene.
///
/// # Blending
/// Output = SceneColor + BloomColor * BloomIntensity
///
/// The bloom contributes additively, brightening the final image.
/// Typical intensity: [0.1, 1.0]. Higher values produce more prominent glow.
///
/// # Input
/// - Scene color texture (LDR after tone mapping)
/// - Blurred bloom texture (from bloom_blur_v_ps.hlsl)
/// - Bloom intensity parameter
///
/// # Output
/// - Scene + bloom composite (final color before display)

// ─────────────────────────────────────────────────────────────────────────────
// Constant Buffers
// ─────────────────────────────────────────────────────────────────────────────

/// Bound at root slot 2 (kSlotPerObject / register b2) via SetRootCBV.
/// Bloom composite parameters.
cbuffer BloomCompositeParams : register(b2) {
    /// Bloom contribution intensity. Scales the blurred bloom texture
    /// before adding to scene color. Typical range: [0.1, 1.0].
    /// Default: 0.5.
    float bloomIntensity;

    /// Unused padding.
    float _pad0;
    float _pad1;
    float _pad2;
};

// ─────────────────────────────────────────────────────────────────────────────
// Textures & Samplers
// ─────────────────────────────────────────────────────────────────────────────

#ifdef UZ_BINDLESS
/// Root constants at b0 — SRV heap indices for scene color and bloom textures.
cbuffer RootConstants : register(b0) {
    uint sceneColorIndex;     ///< SRV descriptor heap index for LDR scene color.
    uint bloomTextureIndex;   ///< SRV descriptor heap index for blurred bloom.
    uint _rcPad0;
    uint _rcPad1;
};
#else
/// Scene color texture (LDR, after tone mapping).
/// This is the main output from the renderer that will receive bloom.
Texture2D<float4> sceneColor : register(t0);

/// Fully blurred bloom texture (from bloom_blur_v_ps.hlsl).
/// This texture is typically at lower resolution than the scene (e.g., 1/4 res)
/// and will be upsampled via bilinear filtering during sampling.
Texture2D<float4> bloomTexture : register(t1);
#endif

/// Bilinear sampler for smooth upsampling of bloom.
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
// Bloom Composite
// ─────────────────────────────────────────────────────────────────────────────

/// Main pixel shader — additive bloom composite.
///
/// Samples scene color and blurred bloom at the same screen UV,
/// then additively blends them: output = scene + bloom * intensity.
///
/// The bloom texture is typically lower resolution and will be
/// bilinearly upsampled to match the scene color dimensions.
///
/// # Returns
/// Scene color with bloom glow added. Output is in [0, 1] range
/// (assuming scene was already tone-mapped to LDR).
float4 main(PSInput input) : SV_Target {
    float2 texCoord = input.texcoord;

    // Sample scene color (at full resolution)
#ifdef UZ_BINDLESS
    Texture2D<float4> sceneTex = ResourceDescriptorHeap[sceneColorIndex];
    Texture2D<float4> bloomTex = ResourceDescriptorHeap[bloomTextureIndex];
    float3 scene = sceneTex.Sample(samplerLinear, texCoord).rgb;
#else
    float3 scene = sceneColor.Sample(samplerLinear, texCoord).rgb;
#endif

    // Sample blurred bloom (at lower resolution, automatically upsampled by bilinear)
#ifdef UZ_BINDLESS
    float3 bloom = bloomTex.Sample(samplerLinear, texCoord).rgb;
#else
    float3 bloom = bloomTexture.Sample(samplerLinear, texCoord).rgb;
#endif

    // Additive blend: scene + bloom
    float3 composite = scene + (bloom * bloomIntensity);

    return float4(composite, 1.0f);
}
