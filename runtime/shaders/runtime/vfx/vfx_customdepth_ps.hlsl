/// VFX CustomDepth prepass pixel shader (phase 8.17.0).
///
/// Depth-only render: samples the particle sprite mask (alpha test) and
/// discards fully transparent pixels so the CustomDepth RT captures only
/// the visible silhouette. No colour output.
///
/// # Pipeline state
/// - Render target: none (depth-only). `SV_Depth` output.
/// - Depth state: `DepthEnable = TRUE, DepthWrite = TRUE, DepthFunc =
///                Greater` (reversed-Z).
/// - Blend state: none.
///
/// # Inputs
/// Same interpolants as the standard billboard PS — we only need
/// `positionCS` and the alpha-test sprite sample.
///
/// @ref UE5 UNiagaraRendererProperties::bRenderCustomDepth
/// @ref docs/engine/25-vfx/34-stencil-customdepth.md

#ifdef UZ_BINDLESS
uint spriteTextureIdx : register(b0);
#else
Texture2D<float4> spriteTexture : register(t0);
#endif

SamplerState linearSampler : register(s0);

struct PSInput {
    float4 positionCS : SV_Position;
    float2 texcoord   : TEXCOORD0;
    float  alpha      : TEXCOORD1; ///< Vertex alpha × color-over-life alpha.
};

/// Alpha threshold below which the pixel is discarded (does not write depth).
/// Matches the billboard PS's `SOFT_FADE_MIN_ALPHA` so silhouettes agree.
static const float kCustomDepthAlphaThreshold = 0.05f;

void main(PSInput input) {
    // Sample the sprite mask; when no sprite is bound use the procedural
    // soft-circle alpha so CustomDepth matches the visible billboard even
    // for untextured particles.
    float maskAlpha;
#ifdef UZ_BINDLESS
    if (spriteTextureIdx != 0xFFFFFFFF) {
        Texture2D<float4> spriteTex = ResourceDescriptorHeap[spriteTextureIdx];
        maskAlpha = spriteTex.SampleLevel(linearSampler, input.texcoord, 0).a;
    } else {
        const float2 c = input.texcoord * 2.0f - 1.0f;
        maskAlpha = saturate(1.0f - dot(c, c));
    }
#else
    maskAlpha = spriteTexture.SampleLevel(linearSampler, input.texcoord, 0).a;
#endif

    const float finalAlpha = maskAlpha * input.alpha;
    if (finalAlpha < kCustomDepthAlphaThreshold) {
        discard;
    }
    // No SV_Target output — depth is written automatically from SV_Position.z.
}
