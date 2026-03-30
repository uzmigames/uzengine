/// Billboard pixel shader for GPU particle rendering.
///
/// Samples the sprite texture at the interpolated UV from the vertex shader,
/// multiplies by the particle's vertex-interpolated color, and applies
/// soft-particle depth fading to avoid hard intersections with scene geometry.
///
/// # Soft Particle Depth Fade
/// Compares the particle's linear depth with the scene depth buffer to produce
/// a fade factor when the particle is close to opaque surfaces. This prevents
/// the visually jarring "hard cut" where a billboard quad intersects geometry.
/// The fade distance is controlled by `softFadeDistance` in VFXBillboardCB.
///
/// Uses reversed-Z depth convention (near=1.0, far=0.0) via depth_common.hlsli.
///
/// # Alpha Blending (Blend Mode Aware)
/// The shader supports 3 blend modes via `blendModeHint` in VFXBillboardCB:
///   - 0 AlphaBlend: SrcAlpha/InvSrcAlpha — outputs straight alpha (no premultiply).
///   - 1 Additive:   One/One — outputs straight RGB, alpha unused by blend state.
///   - 2 PremulAlpha: One/InvSrcAlpha — outputs premultiplied alpha (RGB *= alpha).
/// All modes: DepthTest = enabled, DepthWrite = disabled, Cull = None.
/// The CPU selects the matching PSO; the PS adjusts its alpha output accordingly.
///
/// # Root Signature
/// - b0: VFXBillboardConstants (unified bindless indices for VS buffers + PS textures)
/// - b1: PerFrameCB (nearPlane, farPlane for depth linearization)
/// - b2: VFXBillboardCB (unified VS emitter params + PS rendering params)
/// - s0: linearSampler (bilinear, clamp — for sprite texture)
/// - s1: linearSampler (point, clamp — for depth buffer sampling)
///
/// @ref GPU Gems 3 "Soft Particles" — distance-based alpha fade near surfaces
/// @ref Nguyen 2007 "GPU Gems 3" ch. 23 — flipbook/SubUV sprite sheet animation
/// @constants SOFT_FADE_MIN_ALPHA=0.001 (discard threshold to skip transparent pixels)
///            kFlipbookBlendNone=0, kFlipbookBlendLinear=1, kFlipbookBlendRandom=2
/// @deviations NONE

#include "../include/depth_common.hlsli"

// ─────────────────────────────────────────────────────────────────────────────
// Constants
// ─────────────────────────────────────────────────────────────────────────────

/// Alpha threshold below which pixels are discarded entirely.
/// Avoids blending near-invisible fragments that waste bandwidth.
static const float SOFT_FADE_MIN_ALPHA = 0.001f;


// ─────────────────────────────────────────────────────────────────────────────
// Input Structure
// ─────────────────────────────────────────────────────────────────────────────

/// Interpolants from the billboard vertex shader.
struct PSInput {
    float4 positionCS     : SV_Position; ///< Screen-space position (used for depth buffer UV).
    float2 texcoord       : TEXCOORD0;   ///< Sprite UV in [0,1].
    float4 color          : COLOR0;      ///< Particle RGBA color, vertex-interpolated.
    float3 positionWS     : TEXCOORD1;   ///< World-space position for depth calculations.
    float  age            : TEXCOORD2;   ///< Particle age in seconds (for flipbook animation).
    float  lifetime       : TEXCOORD3;   ///< Particle total lifetime in seconds.
    uint   particleIdx    : TEXCOORD4;   ///< Particle index for deterministic random frame hash.
    float4 prevPositionCS : TEXCOORD5;   ///< Previous frame clip-space position (for motion vector PS).
    float2 subUV0         : TEXCOORD6;   ///< Atlas UV for current flipbook frame (computed in VS).
    float2 subUV1         : TEXCOORD7;   ///< Atlas UV for next flipbook frame (computed in VS).
    float  subUVLerp      : TEXCOORD8;   ///< Inter-frame blend factor [0,1) (computed in VS).
};

// ─────────────────────────────────────────────────────────────────────────────
// Root Constants (b0) — Bindless Descriptor Indices
// ─────────────────────────────────────────────────────────────────────────────

#ifdef UZ_BINDLESS
/// Unified root constants for bindless resource access (shared by VS and PS).
/// Layout MUST match VFXBillboardConstants in particle_billboard_vs.hlsl exactly.
/// PS reads spriteTextureIdx and depthBufferIdx from bytes 32-39.
///
/// Uses plain cbuffer syntax instead of ConstantBuffer<T> to ensure correct
/// DXIL code generation with DXC for root constants. ConstantBuffer<T> can
/// produce incorrect cbufferLoadLegacy offsets when the PS only reads members
/// in the latter half of the struct (DWORDs 8+), because DXC may eliminate
/// the "dead" prefix members and shift the surviving members' byte offsets.
/// Plain cbuffer declares all members as global-scope variables with fixed
/// offsets, which DXC handles correctly regardless of usage patterns.
cbuffer VFXBillboardConstants : register(b0) {
    // VS indices (bytes 0-31) — not read by PS, declared for layout correctness.
    uint positionBufferIdx;
    uint sizeBufferIdx;
    uint colorBufferIdx;
    uint rotationBufferIdx;
    uint sortedIndicesSrvIndex;
    uint velocityBufferIdx;
    uint agesBufferIdx;
    uint lifespansBufferIdx;
    // PS indices (bytes 32-47)
    uint spriteTextureIdx;     ///< SRV index: Texture2D<float4> sprite/atlas.
    uint depthBufferIdx;       ///< SRV index: Texture2D<float> scene depth (reversed-Z).
    uint _vfxPad0;
    uint _vfxPad1;
};
#else
/// SM5 fallback: explicit texture bindings.
Texture2D<float4> spriteTexture : register(t0);
Texture2D<float>  depthBuffer   : register(t4);
#endif

// ─────────────────────────────────────────────────────────────────────────────
// Per-Frame Constant Buffer (b1) — Camera Parameters
// ─────────────────────────────────────────────────────────────────────────────

/// Per-frame constant buffer. Layout MUST match the C++ PerFrameCB struct.
/// The pixel shader only reads nearPlane/farPlane for depth linearization,
/// but all preceding fields must be declared for correct memory layout.
cbuffer PerFrameCB : register(b1) {
    row_major float4x4 viewProj;        ///< View * Projection (unused in PS, declared for layout).
    float4   cameraPos;                 ///< xyz = camera world position, w = unused.
    float4   timeParams;                ///< x = totalTime (unused here).
    float4   lightDir;                  ///< Unused in particle PS.
    float4   lightColor;                ///< Unused in particle PS.
    float4   ambientColor;              ///< Unused in particle PS.
    row_major float4x4 invViewProj;     ///< Unused in particle PS.
    float    nearPlane;                 ///< Camera near plane distance (for depth linearization).
    float    farPlane;                  ///< Camera far plane distance (for depth linearization).
};

// ─────────────────────────────────────────────────────────────────────────────
// Unified Billboard Constant Buffer (b2) — VS Emitter + PS Rendering Params
// ─────────────────────────────────────────────────────────────────────────────

/// Unified per-emitter constant buffer (shared by VS and PS).
/// Layout MUST match VFXBillboardCB in particle_billboard_vs.hlsl exactly.
/// PS reads the rendering parameters starting at byte offset 160.
cbuffer VFXBillboardCB : register(b2) {
    // ── VS data (emitter parameters, 160 bytes) — not read by PS ────────
    row_major float4x4 invView;
    float3   emitterPosition;
    float    globalSizeMultiplier;
    float4   globalColorTint;
    row_major float4x4 prevViewProj;
    // ── PS data (rendering parameters, 48 bytes) ────────────────────────
    float    softFadeDistance;      ///< World-space distance over which particles fade near surfaces.
                                   ///< 0.0 = hard edges (no soft fade). Typical: 0.5 to 2.0.
    float    alphaErosion;         ///< Subtractive alpha erosion. Useful for dissolve effects.
                                   ///< 0.0 = no erosion. Range [0, 1].
    float    emissiveIntensity;    ///< Multiplier for additive emissive contribution.
                                   ///< 0.0 = no emission (default). Used for fire/sparks.
    float    _psPad0;

    // Flipbook / SubUV sprite sheet parameters.
    // When flipbookRows == 1 && flipbookCols == 1, no flipbook logic runs
    // and the shader behaves identically to a single-frame sprite.
    uint     flipbookRows;         ///< Rows in sprite sheet. 1 = no flipbook (default).
    uint     flipbookCols;         ///< Columns in sprite sheet. 1 = no flipbook (default).
    uint     flipbookTotalFrames;  ///< Total valid frames (rows * cols or less if partially filled).
    float    flipbookFPS;          ///< Playback speed in frames per second. Typical: 24-60.
    uint     flipbookBlendMode;    ///< 0 = None (snap), 1 = Linear (lerp), 2 = Random (per-particle).
    uint     _fbPad0;
    uint     _fbPad1;
    uint     _fbPad2;
    // ── Size over lifetime (declared for layout, not read by PS, 16 bytes) ──
    float    sizeOverLifeStart;
    float    sizeOverLifePeak;
    float    sizeOverLifeEnd;
    float    sizeOverLifePeakTime;
    // ── Color over lifetime (linear interpolation, 32 bytes) ──
    float4   colorLifeStart;       ///< RGBA color at birth.
    float4   colorLifeEnd;         ///< RGBA color at death.
    // ── Billboard alignment and stretching (16 bytes, declared for layout) ──
    uint     alignmentMode;        ///< 0=CameraFacing, 1=VelocityAligned (not read by PS).
    float    stretchFactor;        ///< Velocity stretch multiplier (not read by PS).
    uint     blendModeHint;        ///< 0=AlphaBlend, 1=Additive, 2=PremulAlpha — controls PS alpha output.
    uint     spriteChannelMask;  ///< 0=RGBA, 1=R as alpha, 2=G as alpha, 3=B as alpha, 4=A as alpha.
};

// ─────────────────────────────────────────────────────────────────────────────
// Samplers
// ─────────────────────────────────────────────────────────────────────────────

/// Bilinear clamp sampler for sprite sheet atlas sampling (s4).
/// CLAMP addressing prevents bleeding across atlas cell boundaries.
SamplerState spriteSampler : register(s4);

/// Bilinear clamp sampler for depth buffer reads (s2).
/// Uses the global post-process sampler already in the root signature.
SamplerState depthSampler : register(s2);

// ─────────────────────────────────────────────────────────────────────────────
// Helper Functions
// ─────────────────────────────────────────────────────────────────────────────

/// Computes soft particle alpha fade based on scene depth comparison.
///
/// Samples the scene depth buffer at the particle's screen position, linearizes
/// both the scene depth and the particle's depth, then computes a fade factor
/// based on the distance between them. Particles close to surfaces fade to
/// transparent, eliminating hard intersection artifacts.
///
/// # Parameters
/// - screenPos: SV_Position (pixel coordinates + depth in .z).
/// - screenSize: render target dimensions (width, height) for UV computation.
/// - fadeDistance: world-space distance over which the fade occurs.
///
/// # Returns
/// Fade factor in [0, 1]. 0 = fully faded (touching surface), 1 = fully opaque.
float ComputeSoftParticleFade(float4 screenPos, float2 screenSize, float fadeDistance) {
    // Skip soft fade when disabled (fadeDistance <= 0)
    if (fadeDistance <= 0.0f) {
        return 1.0f;
    }

    // Convert screen-space pixel position to UV for depth buffer sampling
    float2 depthUV = screenPos.xy / screenSize;

    // Sample scene depth from the depth buffer
    float sceneDepthRaw;
#ifdef UZ_BINDLESS
    Texture2D<float> depthTex = ResourceDescriptorHeap[depthBufferIdx];
    sceneDepthRaw = depthTex.SampleLevel(depthSampler, depthUV, 0);
#else
    sceneDepthRaw = depthBuffer.SampleLevel(depthSampler, depthUV, 0);
#endif

    // Skip fade for sky pixels (far plane in reversed-Z = 0)
    if (IsSkyDepth(sceneDepthRaw)) {
        return 1.0f;
    }

    // Linearize both depths to view-space distance for meaningful comparison.
    // Raw reversed-Z depth from SV_Position.z is in [1, 0] (near=1, far=0).
    float sceneDepthLinear   = LinearizeDepthReversedZ(sceneDepthRaw, nearPlane, farPlane);
    float particleDepthLinear = LinearizeDepthReversedZ(screenPos.z, nearPlane, farPlane);

    // Compute distance between particle and scene surface
    float depthDifference = sceneDepthLinear - particleDepthLinear;

    // Smooth fade: saturate maps [0, fadeDistance] to [0, 1]
    // Negative depthDifference means particle is behind the surface (should not happen
    // with depth test enabled, but guard against precision edge cases)
    return saturate(depthDifference / fadeDistance);
}

// ─────────────────────────────────────────────────────────────────────────────
// Channel Mask Helper
// ─────────────────────────────────────────────────────────────────────────────

/// Applies the sprite channel mask to a raw texture sample.
///
/// When spriteChannelMask == 0, returns the full RGBA sample as-is.
/// When spriteChannelMask == 1..4, extracts R/G/B/A respectively as an alpha
/// mask with white RGB. This is the standard approach for grayscale sprite
/// sheets where shape data is stored in a single channel.
///
/// # Parameters
/// - rawSample: the RGBA texture sample.
/// - channelMask: 0=RGBA, 1=R as alpha, 2=G as alpha, 3=B as alpha, 4=A as alpha.
///
/// # Returns
/// Masked sprite color.
float4 ApplyChannelMask(float4 rawSample, uint channelMask) {
    if (channelMask == 0) {
        return rawSample;
    }
    // Extract the selected channel as alpha; RGB becomes white so particle
    // color (applied later via multiplication) fully controls hue.
    float channels[4] = { rawSample.r, rawSample.g, rawSample.b, rawSample.a };
    float alpha = channels[channelMask - 1];
    return float4(1.0f, 1.0f, 1.0f, alpha);
}

// ─────────────────────────────────────────────────────────────────────────────
// Pixel Shader Entry Point
// ─────────────────────────────────────────────────────────────────────────────

/// Samples the particle sprite texture, applies vertex color, and computes
/// soft-particle depth fade.
///
/// # Parameters
/// - input: interpolated vertex data from the billboard VS.
///
/// # Returns
/// Final RGBA color for alpha-blended compositing.
float4 main(PSInput input) : SV_Target {
    // ── Sample sprite texture using VS-computed SubUV coordinates ──
    //
    // SubUV atlas coordinates are computed in the vertex shader (Niagara pattern).
    // The PS only samples at the interpolated subUV0/subUV1 and lerps by subUVLerp.
    // This eliminates per-pixel flipbook computation and simplifies the PS significantly.
    float4 spriteColor;

    // When no sprite texture is bound (spriteTextureIdx == 0xFFFFFFFF),
    // generate a soft procedural circle — particle vertex color alone determines appearance.
#ifdef UZ_BINDLESS
    bool hasSpriteTexture = (spriteTextureIdx != 0xFFFFFFFF);
#else
    bool hasSpriteTexture = true; // SM5 path always has explicit texture binding
#endif

    if (!hasSpriteTexture) {
        // No sprite texture — generate a soft radial circle procedurally.
        // Maps UV [0,1] to [-1,+1] centered, then applies smooth falloff.
        float2 center = input.texcoord * 2.0f - 1.0f;
        float dist = dot(center, center); // squared distance from center
        float alpha = saturate(1.0f - dist); // linear falloff: 1 at center, 0 at edge
        alpha *= alpha; // quadratic for softer look
        spriteColor = float4(1.0f, 1.0f, 1.0f, alpha);
    } else {
        // Sample sprite at VS-computed SubUV coordinates (handles both flipbook and single-frame).
        // spriteSampler (s4) uses CLAMP addressing to prevent bleeding across atlas cells.
#ifdef UZ_BINDLESS
        Texture2D<float4> spriteTex = ResourceDescriptorHeap[spriteTextureIdx];
        float4 sampleA = spriteTex.Sample(spriteSampler, input.subUV0);
        float4 sampleB = spriteTex.Sample(spriteSampler, input.subUV1);
#else
        float4 sampleA = spriteTexture.Sample(spriteSampler, input.subUV0);
        float4 sampleB = spriteTexture.Sample(spriteSampler, input.subUV1);
#endif
        float4 rawSample = lerp(sampleA, sampleB, input.subUVLerp);
        spriteColor = ApplyChannelMask(rawSample, spriteChannelMask);
    }

    // ── Evaluate color over lifetime (linear interpolation start→end) ──
    // normalizedAge in [0,1] maps the particle's age to its full lifespan.
    float normalizedAge = saturate(input.age / max(input.lifetime, 0.0001f));
    float4 colorLifeScale = lerp(colorLifeStart, colorLifeEnd, normalizedAge);

    // ── Material output: emissive + opacity (Niagara Unlit pattern) ──
    float3 emissive = spriteColor.rgb * input.color.rgb * colorLifeScale.rgb;
    float  opacity  = spriteColor.a   * input.color.a   * colorLifeScale.a;

    // Apply emissive intensity boost.
    // Multiplies by (1 + intensity) so intensity=0 is neutral (no change),
    // and positive values brighten the particle emission.
    emissive *= (1.0f + emissiveIntensity);

    // ── Apply alpha erosion (subtractive dissolution) ──
    opacity = saturate(opacity - alphaErosion);

    // ── Early discard for nearly-invisible pixels ──
    // Saves blend bandwidth for fully transparent regions of the sprite.
    if (opacity < SOFT_FADE_MIN_ALPHA) {
        discard;
    }

    // ── Soft particle depth fade ──
    // Skip soft fade for additive particles — they add light, not block it,
    // so depth-based fading makes no visual sense.
    float2 screenSize;
#ifdef UZ_BINDLESS
    {
        Texture2D<float> depthTex = ResourceDescriptorHeap[depthBufferIdx];
        uint w, h;
        depthTex.GetDimensions(w, h);
        screenSize = float2(w, h);
    }
#else
    {
        uint w, h;
        depthBuffer.GetDimensions(w, h);
        screenSize = float2(w, h);
    }
#endif

    float softFade = ComputeSoftParticleFade(input.positionCS, screenSize, softFadeDistance);
    if (blendModeHint != 1) { // not additive
        opacity *= softFade;
    }

    // ── Final discard after soft fade ──
    if (opacity < SOFT_FADE_MIN_ALPHA) {
        discard;
    }

    // ── Blend mode output (Niagara exact match) ──
    // Each blend mode requires a different alpha treatment to produce correct
    // results with its corresponding PSO blend state.
    if (blendModeHint == 2) {
        // AlphaComposite (One / InvSrcAlpha): premultiplied alpha output.
        // RGB is pre-multiplied by opacity so the One src factor is correct.
        return float4(emissive * opacity, opacity);
    } else {
        // Additive (One / One) and Translucent (SrcAlpha / InvSrcAlpha):
        // Output straight alpha. Additive ignores alpha in blend (One/One on color),
        // Translucent uses SrcAlpha to modulate RGB in the blend hardware.
        return float4(emissive, opacity);
    }
}
