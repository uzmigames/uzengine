/// Billboard pixel shader for GPU particle rendering.
///
/// Generates a procedural soft circle, multiplies by the particle's
/// vertex-interpolated color, and applies soft-particle depth fading
/// to avoid hard intersections with scene geometry.
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
/// - s2: depthSampler (bilinear, clamp — for depth buffer sampling)
///
/// @ref GPU Gems 3 "Soft Particles" — distance-based alpha fade near surfaces
/// @ref Pharr 2023 "Physically Based Rendering" Ch.12 — Lambertian diffuse NdotL for lit mode
/// @ref Harada 2012 "A 2.5D Culling for Forward+" — lit particle integration
/// @constants SOFT_FADE_MIN_ALPHA=0.001 (discard threshold to skip transparent pixels)
/// @deviations NONE

#include "../include/depth_common.hlsli"
#include "vfx_flipbook.hlsli"
#include "vfx_motion_vectors.hlsli"

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
    float2 texcoord       : TEXCOORD0;   ///< UV in [0,1] for procedural circle generation.
    float4 color          : COLOR0;      ///< Particle RGBA color, vertex-interpolated.
    float3 positionWS     : TEXCOORD1;   ///< World-space position for depth calculations.
    float  age            : TEXCOORD2;   ///< Particle age in seconds.
    float  lifetime       : TEXCOORD3;   ///< Particle total lifetime in seconds.
    uint   particleIdx    : TEXCOORD4;   ///< Particle index (reserved for future use).
    float4 prevPositionCS : TEXCOORD5;   ///< Previous frame clip-space position (for motion vector PS).
    float  speedFactor    : TEXCOORD6;   ///< Normalized speed [0,1] from VS for color-by-speed.
    float4 dynamicParams  : TEXCOORD7;   ///< Per-particle dynamic material parameters (Task 4.14.0). X=normalizedAge, YZW=user.
    // ── Niagara-style flipbook UVs (computed in VS, interpolated to PS) ──
    /// @ref Niagara NiagaraSpriteVertexFactory.ush — VS-precomputed atlas UVs
    float2 flipbookUV0    : TEXCOORD8;   ///< Atlas UV for current frame (interpolated).
    float2 flipbookUV1    : TEXCOORD9;   ///< Atlas UV for next frame.
    float  flipbookLerp   : TEXCOORD10;  ///< Inter-frame blend factor.
};

// ─────────────────────────────────────────────────────────────────────────────
// Root Constants (b0) — Bindless Descriptor Indices
// ─────────────────────────────────────────────────────────────────────────────

#ifdef UZ_BINDLESS
/// Unified root constants for bindless resource access (shared by VS and PS).
/// Layout MUST match VFXBillboardConstants in particle_billboard_vs.hlsl exactly.
/// PS reads depthBufferIdx from bytes 36-39. spriteTextureIdx is reserved but unused.
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
    uint spriteTextureIdx;     ///< Reserved (always UINT32_MAX, no sprite texture).
    uint depthBufferIdx;       ///< SRV index: Texture2D<float> scene depth (reversed-Z).
    uint dynamicParamsBufferIdx; ///< SRV index: StructuredBuffer<float4> dynamic params (Task 4.14.0). Not read by PS.
    /// @ref Niagara SubImageIndex module — declared for layout (read by VS only).
    uint subImageIndexBufferIdx; ///< SRV index: StructuredBuffer<float> subImageIndices. Not read by PS.
};
#else
/// SM5 fallback: explicit texture bindings.
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
    float4   lightDir;                  ///< xyz = direction toward light, w = unused.
    float4   lightColor;                ///< xyz = light color * intensity, w = unused.
    float4   ambientColor;              ///< xyz = ambient radiance, w = unused.
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
    uint     litMode;              ///< 0=Unlit (default), 1=Lit (NdotL directional + ambient).
                                   ///< @ref Pharr 2023 "Physically Based Rendering" Ch.12 — Lambertian diffuse.

    // ── Camera offset & pivot offset (declared for layout, not read by PS) ──
    float    cameraOffset;
    float    pivotOffsetX;
    float    pivotOffsetY;
    // Repurposed from _camPivotPad — screenSpacePixelSize (declared for layout, not read by PS).
    float    screenSpacePixelSize;
    // ── Size by speed (declared for layout, not read by PS) ──
    uint     sizeBySpeedEnabled;
    float    sizeBySpeedMin;
    float    sizeBySpeedMax;
    float    speedRangeMax;
    // ── Size over lifetime (declared for layout, not read by PS, 16 bytes) ──
    float    sizeOverLifeStart;
    float    sizeOverLifePeak;
    float    sizeOverLifeEnd;
    float    sizeOverLifePeakTime;
    // ── Color over lifetime (linear interpolation, 32 bytes) ──
    float4   colorLifeStart;       ///< RGBA color at birth.
    float4   colorLifeEnd;         ///< RGBA color at death.
    // ── Billboard facing mode + custom direction (16 bytes, declared for layout) ──
    uint     facingMode;           ///< 0=FaceCamera, 1=VelocityAligned, 2=FaceCameraPlane, 3=CustomVector, 4=FaceCameraPosition, 5=PlaneAxis, 6=WorldZAligned (not read by PS).
    float    customFacingX;        ///< Custom facing direction X (not read by PS).
    float    customFacingY;        ///< Custom facing direction Y (not read by PS).
    float    customFacingZ;        ///< Custom facing direction Z (not read by PS).
    // ── Billboard stretching + blend (16 bytes) ──
    float    stretchFactor;        ///< Velocity stretch multiplier (not read by PS).
    uint     blendModeHint;        ///< 0=AlphaBlend, 1=Additive, 2=PremulAlpha — controls PS alpha output.
    uint     colorRampLutSrvIndex; ///< Bindless SRV for color ramp 256x1 LUT. 0xFFFFFFFF = legacy lerp.
    // Repurposed from _alignPad — screenSpaceSizeEnabled (declared for layout, not read by PS).
    // @ref Valve 2006 "Rendering in Left 4 Dead" — constant-screen-size sprites
    uint     screenSpaceSizeEnabled; ///< 0=world-space (default), 1=screen-space. Not read by PS.
    // ── Color by speed (VS→PS, 16 bytes) ──────────────────────────────────
    /// When non-zero, the PS uses input.speedFactor instead of normalizedAge
    /// to drive the colorLifeStart→End lerp (or LUT sample).
    uint     colorBySpeedEnabled;   ///< 0=off, 1=on. PS uses speed instead of age for color lerp.
    // ── Flipbook / SubUV animation (12 bytes) ─────────────────────────────
    /// @ref Nguyen 2007 "GPU Gems 3" ch. 23 — SubUV sprite sheet animation
    uint     flipbookRows;          ///< Sprite sheet rows (1 = no flipbook).
    uint     flipbookCols;          ///< Sprite sheet columns (1 = no flipbook).
    uint     flipbookTotalFrames;   ///< Total valid frames (rows*cols or less for partial sheets).
    // ── Flipbook FPS + channel mask (16 bytes) ───────────────────────────
    float    flipbookFPS;           ///< Flipbook playback rate (frames per second).
    uint     spriteChannelMask;     ///< 0=RGBA, 1=R, 2=G, 3=B, 4=A.
    float    vfxTotalTime;          ///< Global total time (s) — same as debug quad.
    uint     _vfxBillboardPad1;
};

// ─────────────────────────────────────────────────────────────────────────────
// Samplers
// ─────────────────────────────────────────────────────────────────────────────

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
    if (depthBufferIdx == 0xFFFFFFFF) return 1.0f;
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

/// Applies the user-selected sprite channel mask to a sampled sprite color.
///
/// Selecting a single channel splats that channel to RGB and A so downstream
/// multiplication by particle color/tint produces expected grayscale alpha
/// mattes (common for single-channel smoke / alpha-only sprite sheets).
///
/// # Parameters
/// - sample: sampled sprite RGBA.
/// - mask: channel selector (0=RGBA, 1=R, 2=G, 3=B, 4=A).
///
/// # Returns
/// Masked RGBA color.
float4 ApplyChannelMask(float4 sample, uint mask) {
    if (mask == 1u) return float4(sample.rrr, sample.r);
    if (mask == 2u) return float4(sample.ggg, sample.g);
    if (mask == 3u) return float4(sample.bbb, sample.b);
    if (mask == 4u) return float4(sample.aaa, sample.a);
    return sample;
}

// ─────────────────────────────────────────────────────────────────────────────
// Pixel Shader Entry Point
// ─────────────────────────────────────────────────────────────────────────────

/// Multi-target output for particle billboard pixel shader.
///
/// SV_Target0: HDR scene color (RGBA16F, alpha-blended per blendModeHint).
/// SV_Target1: Per-pixel screen-space motion vector (RG16F, UV space).
///
/// The motion vector is the difference between the current frame's screen-space
/// UV and the previous frame's screen-space UV for this fragment. Consumers
/// (TAA / DLSS / FSR) reproject the previous frame's color using this vector.
///
/// The velocity RT is bound with blend disabled so the last-drawn particle
/// dominates the motion vector at each pixel. This matches the alpha-sort order
/// (back-to-front) — the foreground particle's motion is the one that matters
/// for reprojection of the visible pixel.
///
/// @ref Karis 2014 "High Quality Temporal Supersampling" — per-object motion vectors
/// @ref UE5 NiagaraSpriteVertexFactory motion-vector output
struct PSOutput {
    float4 color          : SV_Target0;   ///< HDR scene color.
    float2 motionVector   : SV_Target1;   ///< Screen-space UV delta (RG16F).
};

/// Generates a procedural soft circle, applies vertex color, color over
/// lifetime, and computes soft-particle depth fade.
///
/// # Parameters
/// - input: interpolated vertex data from the billboard VS.
///
/// # Returns
/// Final RGBA color for alpha-blended compositing and per-pixel motion vector.
PSOutput main(PSInput input) {
    PSOutput output;
    float4 spriteColor;

    if (flipbookTotalFrames > 1u) {
        // ── Niagara flipbook sample ──────────────────────────────────────
        // The VS has already computed per-vertex atlas UVs from the
        // per-particle subImageIndex SoA channel (written by the update CS
        // each frame). The PS just samples both frames and lerps by the
        // interpolated blend factor. No flipbook math in the PS.
        //
        /// @ref Niagara NiagaraSpriteVertexFactory.ush — PS is purely stateless
#ifdef UZ_BINDLESS
        if (spriteTextureIdx != 0xFFFFFFFF) {
            Texture2D<float4> spriteTex = ResourceDescriptorHeap[spriteTextureIdx];
            float4 c0 = spriteTex.SampleLevel(depthSampler, input.flipbookUV0, 0);
            float4 c1 = spriteTex.SampleLevel(depthSampler, input.flipbookUV1, 0);
            spriteColor = lerp(c0, c1, input.flipbookLerp);
            spriteColor = ApplyChannelMask(spriteColor, spriteChannelMask);
        } else {
            float2 ctr = input.texcoord * 2.0f - 1.0f;
            float d = dot(ctr, ctr);
            float a = saturate(1.0f - d);
            spriteColor = float4(1.0f, 1.0f, 1.0f, a * a);
        }
#else
        spriteColor = float4(1.0f, 1.0f, 1.0f, 1.0f);
#endif
    } else if (flipbookTotalFrames == 1u) {
        // Single-frame sprite texture (no animation).
#ifdef UZ_BINDLESS
        if (spriteTextureIdx != 0xFFFFFFFF) {
            Texture2D<float4> spriteTex = ResourceDescriptorHeap[spriteTextureIdx];
            spriteColor = spriteTex.SampleLevel(depthSampler, input.texcoord, 0);
            spriteColor = ApplyChannelMask(spriteColor, spriteChannelMask);
        } else {
            float2 ctr = input.texcoord * 2.0f - 1.0f;
            float d = dot(ctr, ctr);
            float a = saturate(1.0f - d);
            spriteColor = float4(1.0f, 1.0f, 1.0f, a * a);
        }
#else
        spriteColor = float4(1.0f, 1.0f, 1.0f, 1.0f);
#endif
    } else {
        // No flipbook — procedural soft circle.
        float2 center = input.texcoord * 2.0f - 1.0f;
        float dist = dot(center, center);
        float alpha = saturate(1.0f - dist);
        alpha *= alpha;
        spriteColor = float4(1.0f, 1.0f, 1.0f, alpha);
    }

    // ── Evaluate color over lifetime ──
    // Use per-particle normalizedAge passed from VS via dynamicParams.x.
    float normalizedAge = saturate(input.dynamicParams.x);

    // ── Color by speed override ──
    // When colorBySpeedEnabled is non-zero, replace normalizedAge with the
    // VS-computed speed factor for the color lerp/LUT lookup. This causes
    // colorLifeStart to represent "slow" color and colorLifeEnd "fast" color
    // (or the LUT to be indexed by speed instead of age).
    float colorParam = (colorBySpeedEnabled != 0u) ? input.speedFactor : normalizedAge;

    /// @ref GDC 2019 "Niagara: Building the Future of FX in Unreal Engine"
    ///     — LUT sample at colorParam replaces linear lerp when ramp is available
    float4 colorLifeScale;
#ifdef UZ_BINDLESS
    if (colorRampLutSrvIndex != 0xFFFFFFFF) {
        Texture2D<float4> rampTex = ResourceDescriptorHeap[colorRampLutSrvIndex];
        colorLifeScale = rampTex.SampleLevel(depthSampler, float2(colorParam, 0.5f), 0);
    } else {
        colorLifeScale = lerp(colorLifeStart, colorLifeEnd, colorParam);
    }
#else
    colorLifeScale = lerp(colorLifeStart, colorLifeEnd, colorParam);
#endif

    // ── Material output: emissive + opacity (Niagara Unlit pattern) ──
    float3 emissive = spriteColor.rgb * input.color.rgb * colorLifeScale.rgb;
    float  opacity  = spriteColor.a   * input.color.a   * colorLifeScale.a;

    // Apply emissive intensity boost.
    // Multiplies by (1 + intensity) so intensity=0 is neutral (no change),
    // and positive values brighten the particle emission.
    emissive *= (1.0f + emissiveIntensity);

    // ── Lit particle lighting (directional + ambient) ──
    // When litMode == 1, modulate emissive by a Lambertian NdotL directional
    // light contribution plus an ambient term from PerFrameCB. The billboard
    // normal is approximated as the camera-facing direction (since camera-facing
    // billboards have their surface normal pointing toward the viewer).
    //
    // @ref Pharr 2023 "Physically Based Rendering" Ch.12 — Lambertian diffuse: L = kd * (N·L) * Lc
    // @ref Harada 2012 "A 2.5D Culling for Forward+" — lit particle integration
    if (litMode == 1) {
        // Billboard normal: for camera-facing billboards the surface normal
        // is the direction from the particle toward the camera.
        float3 normalWS = normalize(cameraPos.xyz - input.positionWS);

        // Directional light NdotL (Lambertian diffuse).
        // lightDir.xyz = direction toward light (engine convention).
        float NdotL = saturate(dot(normalWS, normalize(lightDir.xyz)));

        // Combine ambient radiance with directional contribution.
        float3 litColor = ambientColor.rgb + lightColor.rgb * NdotL;

        // Modulate base color by lighting. Emissive intensity boost (above)
        // still applies, so emissiveIntensity > 0 brightens lit particles too.
        emissive *= litColor;
    }

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
    float2 screenSize = float2(1920.0f, 1080.0f); // fallback
#ifdef UZ_BINDLESS
    if (depthBufferIdx != 0xFFFFFFFF) {
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

    // ── Motion vector output (SV_Target1) ──
    // Computed from current SV_Position clip-space and the VS-provided
    // prevPositionCS (which uses prevViewProj * prevWorldPos). TAA / DLSS /
    // FSR use this to reproject the previous frame so fast-moving particles
    // do not leave stale history trails.
    //
    // @ref Karis 2014 "High Quality Temporal Supersampling"
    // @ref vfx_motion_vectors.hlsli — ComputeMotionVector NDC-to-UV helper
    output.motionVector = ComputeMotionVector(input.positionCS, input.prevPositionCS);

    // ── Blend mode output (Niagara exact match) ──
    // Each blend mode requires a different alpha treatment to produce correct
    // results with its corresponding PSO blend state.
    if (blendModeHint == 2) {
        // AlphaComposite (One / InvSrcAlpha): premultiplied alpha output.
        // RGB is pre-multiplied by opacity so the One src factor is correct.
        output.color = float4(emissive * opacity, opacity);
    } else {
        // Additive (One / One) and Translucent (SrcAlpha / InvSrcAlpha):
        // Output straight alpha. Additive ignores alpha in blend (One/One on color),
        // Translucent uses SrcAlpha to modulate RGB in the blend hardware.
        output.color = float4(emissive, opacity);
    }
    return output;
}
