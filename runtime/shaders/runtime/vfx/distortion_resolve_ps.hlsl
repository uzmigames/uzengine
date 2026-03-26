/// VFX Distortion Resolve Pixel Shader -- fullscreen UV warp of HDR color.
///
/// Reads the RGBA16F distortion accumulation texture at each pixel's UV,
/// computes a warped UV from the accumulated offset, and samples the
/// pre-distortion HDR color at that warped position.
///
/// # Warp Formula
/// ```
/// float2 offset    = distortion.Sample(uv).rg;    // UV offset direction
/// float  magnitude = distortion.Sample(uv).a;      // Warp strength
/// float2 warpedUV  = saturate(uv + offset * magnitude);
/// output           = preDistortionColor.Sample(warpedUV);
/// ```
///
/// When distortion.a == 0 (no distortion), the sample position equals the
/// original UV, producing an identity transform with no visual artifact.
///
/// # Root Signature
/// - b0: Root constants (2 SRV descriptor indices: color + distortion).
/// - s0: linearSampler (bilinear, clamp — for both texture samples).
///
/// @ref Vlachos 2015 "Advanced VR Rendering" — screen-space UV distortion
/// @constants NONE (all parameters are runtime SRV indices)
/// @deviations NONE

// ─────────────────────────────────────────────────────────────────────────────
// Input Structure
// ─────────────────────────────────────────────────────────────────────────────

/// Interpolants from the fullscreen triangle vertex shader.
/// The VS generates these from SV_VertexID with no vertex buffer.
struct PSInput {
    float4 positionCS : SV_Position; ///< Screen-space position.
    float2 texcoord   : TEXCOORD0;   ///< Fullscreen UV in [0,1].
};

// ─────────────────────────────────────────────────────────────────────────────
// Root Constants (b0) — SRV Descriptor Indices
// ─────────────────────────────────────────────────────────────────────────────

#ifdef UZ_BINDLESS
/// Root constants for bindless resource access.
/// Contains SRV descriptor heap indices for the pre-distortion HDR color
/// texture and the distortion accumulation texture.
struct DistortionResolveIndices {
    uint colorTextureSrvIndex;      ///< SRV index: pre-distortion HDR color (Texture2D<float4>).
    uint distortionTextureSrvIndex; ///< SRV index: RGBA16F distortion target (Texture2D<float4>).
    uint _pad0;
    uint _pad1;
};
ConstantBuffer<DistortionResolveIndices> resolveParams : register(b0);
#else
/// SM5 fallback: explicit texture bindings.
Texture2D<float4> colorTexture      : register(t0);
Texture2D<float4> distortionTexture : register(t1);
#endif

// ─────────────────────────────────────────────────────────────────────────────
// Samplers
// ─────────────────────────────────────────────────────────────────────────────

/// Bilinear sampler with clamp addressing for both color and distortion sampling.
/// Clamp prevents UV wrapping artifacts at screen edges.
SamplerState linearSampler : register(s0);

// ─────────────────────────────────────────────────────────────────────────────
// Pixel Shader Entry Point
// ─────────────────────────────────────────────────────────────────────────────

/// Warps the pre-distortion HDR color by the accumulated distortion offsets.
///
/// Samples the distortion texture to get the UV offset direction (RG) and
/// warp magnitude (A), computes the warped UV, then samples the pre-distortion
/// color at that location. The result is the final HDR color with distortion
/// applied.
///
/// # Parameters
/// - input: interpolated fullscreen triangle vertex data.
///
/// # Returns
/// float4 — warped HDR color for the color target.
float4 main(PSInput input) : SV_Target {
    // ── Sample distortion accumulation texture ──
    float4 distortion;
#ifdef UZ_BINDLESS
    Texture2D<float4> distortionTex = ResourceDescriptorHeap[resolveParams.distortionTextureSrvIndex];
    distortion = distortionTex.Sample(linearSampler, input.texcoord);
#else
    distortion = distortionTexture.Sample(linearSampler, input.texcoord);
#endif

    // ── Compute warped UV ──
    // offset.rg = UV offset direction (accumulated from all distortion sources).
    // offset.a  = warp magnitude (accumulated distortion intensity).
    // When offset.a == 0, warpedUV == input.texcoord (identity, no branch needed).
    float2 warpedUV = input.texcoord + distortion.rg * distortion.a;

    // Clamp to [0,1] to prevent sampling outside the texture boundaries.
    // This avoids wrapping artifacts at screen edges where large distortion
    // could push UVs out of range.
    warpedUV = saturate(warpedUV);

    // ── Sample pre-distortion HDR color at warped position ──
    float4 color;
#ifdef UZ_BINDLESS
    Texture2D<float4> colorTex = ResourceDescriptorHeap[resolveParams.colorTextureSrvIndex];
    color = colorTex.Sample(linearSampler, warpedUV);
#else
    color = colorTexture.Sample(linearSampler, warpedUV);
#endif

    return color;
}
