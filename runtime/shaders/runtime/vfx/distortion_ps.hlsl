/// VFX Distortion Pixel Shader -- outputs screen-space UV offset vectors.
///
/// Samples a normal map texture, transforms the tangent-space normal to a
/// screen-space UV offset, and outputs the offset modulated by particle alpha
/// and a global distortion strength multiplier. The result is accumulated in
/// an RGBA16F render target via additive blending.
///
/// The resolve pass reads RG as the UV warp direction and A as the warp
/// magnitude, applying `warpedUV = baseUV + offset.xy * offset.a`.
///
/// # Output Format
/// - R: Horizontal UV offset (positive = warp right).
/// - G: Vertical UV offset (positive = warp down).
/// - B: Unused (zero).
/// - A: Distortion intensity (particle alpha * strength). Controls warp magnitude.
///
/// # Root Signature
/// - b0: DistortionPSConstants (distortion strength, normal map SRV index).
/// - b1: PerEmitterCB (view/proj, viewport — shared with billboard VS).
/// - s0: linearSampler (bilinear, clamp — for normal map sampling).
///
/// @ref Vlachos 2015 "Advanced VR Rendering" — screen-space UV distortion
/// @constants DISTORTION_UV_SCALE=0.01 (UV offset scale per unit normal displacement)
/// @deviations NONE

// ─────────────────────────────────────────────────────────────────────────────
// Constants
// ─────────────────────────────────────────────────────────────────────────────

/// UV offset scale factor per unit of normal displacement.
/// A normal.xy of (1,0) at distortionStrength=1.0 produces a 0.01 UV offset
/// (approximately 10 pixels at 1080p). This matches the typical scale used
/// in real-time VFX pipelines for heat haze and energy shield effects.
///
/// @ref Vlachos 2015 "Advanced VR Rendering" — empirically validated scale
static const float DISTORTION_UV_SCALE = 0.01f;

/// Alpha threshold below which distortion pixels are discarded.
/// Prevents near-zero distortion fragments from wasting blend bandwidth.
static const float DISTORTION_MIN_ALPHA = 0.001f;

// ─────────────────────────────────────────────────────────────────────────────
// Input Structure
// ─────────────────────────────────────────────────────────────────────────────

/// Interpolants from the billboard vertex shader.
struct PSInput {
    float4 positionCS : SV_Position; ///< Screen-space position.
    float2 texcoord   : TEXCOORD0;   ///< Billboard UV in [0,1].
    float4 color      : COLOR0;      ///< Particle RGBA color (alpha controls intensity).
};

// ─────────────────────────────────────────────────────────────────────────────
// Root Constants (b0) — Distortion Parameters
// ─────────────────────────────────────────────────────────────────────────────

#ifdef UZ_BINDLESS
/// Per-emitter distortion constants for bindless path.
/// Contains the global strength multiplier and the SRV descriptor heap index
/// for the normal map texture.
struct DistortionPSIndices {
    float distortionStrength;   ///< Global strength multiplier for UV offset.
    uint  normalMapSrvIndex;    ///< SRV index: Texture2D<float4> normal map.
    uint  _pad0;
    uint  _pad1;
};
ConstantBuffer<DistortionPSIndices> distortionParams : register(b0);
#else
/// SM5 fallback: explicit texture binding for normal map.
Texture2D<float4> normalMapTexture : register(t0);

/// SM5 fallback: distortion strength passed via per-emitter CB.
cbuffer DistortionPSConstants : register(b0) {
    float distortionStrength;   ///< Global strength multiplier for UV offset.
    uint  normalMapSrvIndex;    ///< Unused in SM5 path (texture bound explicitly).
    float _dPad0;
    float _dPad1;
};
#endif

// ─────────────────────────────────────────────────────────────────────────────
// Samplers
// ─────────────────────────────────────────────────────────────────────────────

/// Bilinear sampler with clamp addressing for normal map sampling.
SamplerState linearSampler : register(s0);

// ─────────────────────────────────────────────────────────────────────────────
// Pixel Shader Entry Point
// ─────────────────────────────────────────────────────────────────────────────

/// Samples the normal map and outputs a screen-space UV offset vector.
///
/// The normal map is expected in tangent-space [0,1] encoding (uncompressed
/// RGB normal map). The shader decodes to [-1,1], extracts the XY components
/// as the warp direction, and scales by the distortion strength and UV scale
/// factor.
///
/// Particle alpha from the vertex shader controls per-particle intensity,
/// allowing particles to fade in/out smoothly. The output alpha is the
/// product of particle alpha and distortion strength, providing the resolve
/// pass with a single "warp magnitude" value.
///
/// # Parameters
/// - input: interpolated vertex data from the billboard VS.
///
/// # Returns
/// float4(uvOffset.x, uvOffset.y, 0.0, intensity) for RGBA16F accumulation.
float4 main(PSInput input) : SV_Target {
    // ── Early discard for invisible particles ──
    float particleAlpha = input.color.a;
    if (particleAlpha < DISTORTION_MIN_ALPHA) {
        discard;
    }

    // ── Sample normal map ──
    float3 normalTS;
#ifdef UZ_BINDLESS
    Texture2D<float4> normalMap = ResourceDescriptorHeap[distortionParams.normalMapSrvIndex];
    normalTS = normalMap.Sample(linearSampler, input.texcoord).xyz;
    float strength = distortionParams.distortionStrength;
#else
    normalTS = normalMapTexture.Sample(linearSampler, input.texcoord).xyz;
    float strength = distortionStrength;
#endif

    // ── Decode tangent-space normal from [0,1] to [-1,1] ──
    float3 normal = normalTS * 2.0f - 1.0f;

    // ── Compute screen-space UV offset ──
    // The XY components of the decoded normal define the warp direction.
    // Scale by strength and the per-unit UV scale factor.
    float2 uvOffset = normal.xy * strength * DISTORTION_UV_SCALE;

    // ── Modulate by particle alpha for per-particle intensity control ──
    float intensity = particleAlpha * saturate(strength);

    return float4(uvOffset.x, uvOffset.y, 0.0f, intensity);
}
