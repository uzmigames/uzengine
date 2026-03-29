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
/// # Alpha Blending
/// This shader outputs premultiplied alpha. The PSO blend state should be:
///   SrcBlend = SrcAlpha, DestBlend = InvSrcAlpha
///   DepthTest = enabled, DepthWrite = disabled, Cull = None
///
/// # Root Signature
/// - b0: VFXBillboardConstants (unified bindless indices for VS buffers + PS textures)
/// - b1: PerFrameCB (nearPlane, farPlane for depth linearization)
/// - b2: VFXBillboardCB (unified VS emitter params + PS rendering params)
/// - s0: linearSampler (bilinear, clamp — for sprite texture)
/// - s1: pointSampler (point, clamp — for depth buffer sampling)
///
/// @ref GPU Gems 3 "Soft Particles" — distance-based alpha fade near surfaces
/// @ref Nguyen 2007 "GPU Gems 3" ch. 23 — flipbook/SubUV sprite sheet animation
/// @constants SOFT_FADE_MIN_ALPHA=0.001 (discard threshold to skip transparent pixels)
///            kFlipbookBlendNone=0, kFlipbookBlendLinear=1, kFlipbookBlendRandom=2
/// @deviations NONE

#include "../include/depth_common.hlsli"
#include "vfx_flipbook.hlsli"

// ─────────────────────────────────────────────────────────────────────────────
// Constants
// ─────────────────────────────────────────────────────────────────────────────

/// Alpha threshold below which pixels are discarded entirely.
/// Avoids blending near-invisible fragments that waste bandwidth.
static const float SOFT_FADE_MIN_ALPHA = 0.001f;

/// Flipbook blend mode constants matching VFXBillboardCB::flipbookBlendMode.
static const uint kFlipbookBlendNone   = 0; ///< No inter-frame blending; snap to nearest frame.
static const uint kFlipbookBlendLinear = 1; ///< Linearly interpolate between adjacent frames.
static const uint kFlipbookBlendRandom = 2; ///< Deterministic per-particle random frame (no animation).

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
};

// ─────────────────────────────────────────────────────────────────────────────
// Root Constants (b0) — Bindless Descriptor Indices
// ─────────────────────────────────────────────────────────────────────────────

#ifdef UZ_BINDLESS
/// Unified root constants for bindless resource access (shared by VS and PS).
/// Layout MUST match VFXBillboardConstants in particle_billboard_vs.hlsl exactly.
/// PS reads spriteTextureIdx and depthBufferIdx from bytes 32-39.
struct VFXBillboardConstants {
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
    uint _pad0;
    uint _pad1;
};
ConstantBuffer<VFXBillboardConstants> vfxConstants : register(b0);
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
    float    _solPad0;
    float    _solPad1;
};

// ─────────────────────────────────────────────────────────────────────────────
// Samplers
// ─────────────────────────────────────────────────────────────────────────────

/// Bilinear sampler with clamp addressing for sprite texture sampling.
/// Uses s2 from the global root signature (bilinear + clamp).
/// s0 = anisotropic wrap (wrong for sprites), s1 = comparison (shadow PCF).
SamplerState linearSampler : register(s2);

/// Depth buffer sampling reuses the bilinear clamp sampler (s2).
/// No point sampler exists in the root signature; bilinear is acceptable
/// for the approximate soft-particle depth fade calculation.
SamplerState pointSampler  : register(s2);

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
    Texture2D<float> depthTex = ResourceDescriptorHeap[vfxConstants.depthBufferIdx];
    sceneDepthRaw = depthTex.SampleLevel(pointSampler, depthUV, 0);
#else
    sceneDepthRaw = depthBuffer.SampleLevel(pointSampler, depthUV, 0);
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
    // ── Sample sprite texture (with optional flipbook/SubUV animation) ──
    //
    // When flipbook is active (rows*cols > 1), we compute frame UVs from
    // the particle's age/lifetime and sample the correct sub-rectangle of
    // the sprite sheet atlas. When flipbook is inactive (1x1), we fall
    // through to the standard single-frame sample for zero overhead.
    float4 spriteColor;

    // When no sprite texture is bound (spriteTextureIdx == 0xFFFFFFFF),
    // use solid white — the particle's vertex color alone determines appearance.
#ifdef UZ_BINDLESS
    bool hasSpriteTexture = (vfxConstants.spriteTextureIdx != 0xFFFFFFFF);
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
    } else if (flipbookRows > 1 || flipbookCols > 1) {
        // Flipbook active — compute frame UVs based on blend mode.
        FlipbookResult fb;
        if (flipbookBlendMode == kFlipbookBlendRandom) {
            // Random mode: deterministic per-particle frame via Wang hash.
            // Not animated — each particle shows a fixed frame for its lifetime.
            fb = ComputeFlipbookFrameRandom(
                input.particleIdx,
                flipbookRows, flipbookCols, flipbookTotalFrames);
        } else {
            // Animated mode (None or Linear): frame advances with particle age.
            fb = ComputeFlipbookFrame(
                input.age, input.lifetime,
                flipbookRows, flipbookCols,
                flipbookFPS, flipbookTotalFrames);
        }

        // Remap billboard [0,1] UV into the atlas sub-rectangle for the current frame.
        float2 uv0 = SampleFlipbookUV(input.texcoord, fb.uv0, fb.frameSize);

#ifdef UZ_BINDLESS
        Texture2D<float4> spriteTex = ResourceDescriptorHeap[vfxConstants.spriteTextureIdx];
#endif

        // Sample the current frame.
#ifdef UZ_BINDLESS
        spriteColor = spriteTex.Sample(linearSampler, uv0);
#else
        spriteColor = spriteTexture.Sample(linearSampler, uv0);
#endif

        if (flipbookBlendMode == kFlipbookBlendLinear) {
            // Linear blend: sample the next frame and interpolate for smooth transitions.
            float2 uv1 = SampleFlipbookUV(input.texcoord, fb.uv1, fb.frameSize);
            float4 nextColor;
#ifdef UZ_BINDLESS
            nextColor = spriteTex.Sample(linearSampler, uv1);
#else
            nextColor = spriteTexture.Sample(linearSampler, uv1);
#endif
            spriteColor = lerp(spriteColor, nextColor, fb.blendFactor);
        }
    } else {
        // No flipbook — standard single-frame sprite sampling (original path).
#ifdef UZ_BINDLESS
        Texture2D<float4> spriteTex = ResourceDescriptorHeap[vfxConstants.spriteTextureIdx];
        spriteColor = spriteTex.Sample(linearSampler, input.texcoord);
#else
        spriteColor = spriteTexture.Sample(linearSampler, input.texcoord);
#endif
    }

    // ── Evaluate color over lifetime (linear interpolation start→end) ──
    // normalizedAge in [0,1] maps the particle's age to its full lifespan.
    float normalizedAge = saturate(input.age / max(input.lifetime, 0.0001f));
    float4 colorLifeScale = lerp(colorLifeStart, colorLifeEnd, normalizedAge);

    // ── Combine sprite with particle color and lifetime color ──
    float4 finalColor = spriteColor * input.color * colorLifeScale;

    // ── Apply alpha erosion (subtractive dissolution) ──
    finalColor.a = saturate(finalColor.a - alphaErosion);

    // ── Early discard for nearly-invisible pixels ──
    // Saves blend bandwidth for fully transparent regions of the sprite
    if (finalColor.a < SOFT_FADE_MIN_ALPHA) {
        discard;
    }

    // ── Soft particle depth fade ──
    // Retrieve render target size from SV_Position (DX12 guarantees this is valid)
    // Note: screen dimensions come from the viewport, but we need the actual RT size
    // for correct UV mapping. We derive it from the integer pixel coords.
    // The CPU must set the viewport to match the RT dimensions exactly.
    //
    // For the soft fade, we need screen dimensions. These are passed via the
    // PerFrameCB or derived from the depth buffer dimensions. Here we use
    // GetDimensions() on the depth texture for robustness.
    float2 screenSize;
#ifdef UZ_BINDLESS
    {
        Texture2D<float> depthTex = ResourceDescriptorHeap[vfxConstants.depthBufferIdx];
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
    finalColor.a *= softFade;

    // ── Final discard after soft fade ──
    if (finalColor.a < SOFT_FADE_MIN_ALPHA) {
        discard;
    }

    // ── Add emissive contribution ──
    // Emissive brightens the particle using its own tinted color (not raw sprite white).
    // This preserves the color-over-lifetime tint in the glow.
    finalColor.rgb += finalColor.rgb * emissiveIntensity;

    return finalColor;
}
