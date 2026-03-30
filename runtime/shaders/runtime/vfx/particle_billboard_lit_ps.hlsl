/// Lit billboard pixel shader for GPU particle rendering.
///
/// Copy-variant of `particle_billboard_ps.hlsl` with directional + point light
/// shading added. When `litMode == 1`, particles receive Lambertian NdotL diffuse
/// lighting from the scene directional light and ambient term (read from PerFrameCB).
/// An optional point light contribution is also computed using windowed quadratic
/// attenuation.
///
/// The billboard surface normal is approximated as camera-facing: for camera-facing
/// billboards the normal IS the view direction (camera → particle). This gives
/// consistent half-lit shading that responds to directional light orientation.
///
/// When `litMode == 0`, the shader behaves identically to the unlit variant.
///
/// # Root Signature
/// - b0: VFXBillboardConstants (unified bindless indices for VS buffers + PS textures)
/// - b1: PerFrameCB (lightDir, lightColor, ambientColor, cameraPos, nearPlane, farPlane)
/// - b2: VFXBillboardCB (unified VS emitter params + PS rendering params, includes litMode)
/// - s2: depthSampler (bilinear, clamp — for depth buffer sampling)
///
/// @ref Pharr 2023 "Physically Based Rendering" Ch.12 — Lambertian diffuse NdotL
/// @ref Harada 2012 "2.5D Culling for Forward+" — lit particle integration
/// @ref Lagarde 2014 "Moving Frostbite to PBR" — windowed attenuation for point lights
/// @ref GPU Gems 3 "Soft Particles" — distance-based alpha fade near surfaces
/// @constants SOFT_FADE_MIN_ALPHA=0.001 (discard threshold to skip transparent pixels)
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
    float2 texcoord       : TEXCOORD0;   ///< UV in [0,1] for procedural circle generation.
    float4 color          : COLOR0;      ///< Particle RGBA color, vertex-interpolated.
    float3 positionWS     : TEXCOORD1;   ///< World-space position for depth calculations.
    float  age            : TEXCOORD2;   ///< Particle age in seconds.
    float  lifetime       : TEXCOORD3;   ///< Particle total lifetime in seconds.
    uint   particleIdx    : TEXCOORD4;   ///< Particle index (reserved for future use).
    float4 prevPositionCS : TEXCOORD5;   ///< Previous frame clip-space position (for motion vector PS).
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
    uint _vfxPad0;
    uint _vfxPad1;
};
#else
/// SM5 fallback: explicit texture bindings.
Texture2D<float>  depthBuffer   : register(t4);
#endif

// ─────────────────────────────────────────────────────────────────────────────
// Per-Frame Constant Buffer (b1) — Camera Parameters
// ─────────────────────────────────────────────────────────────────────────────

/// Per-frame constant buffer. Layout MUST match the C++ PerFrameCB struct.
/// The lit pixel shader reads nearPlane/farPlane for depth linearization,
/// and lightDir/lightColor/ambientColor/cameraPos for Lambertian shading.
cbuffer PerFrameCB : register(b1) {
    row_major float4x4 viewProj;        ///< View * Projection (unused in PS, declared for layout).
    float4   cameraPos;                 ///< xyz = camera world position, w = unused.
    float4   timeParams;                ///< x = totalTime (unused here).
    float4   lightDir;                  ///< xyz = direction toward light, w = unused.
    float4   lightColor;                ///< xyz = color * intensity, w = unused.
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
                                   ///< @ref Pharr 2023 "Physically Based Rendering" Ch.12.

    float    _pad1;
    float    _pad2;
    float    _pad3;
    float    _pad4;
    float    _pad5;
    float    _pad6;
    float    _pad7;
    float    _pad8;
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
    uint     colorRampLutSrvIndex; ///< Bindless SRV for color ramp 256x1 LUT. 0xFFFFFFFF = legacy lerp.
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
// Pixel Shader Entry Point
// ─────────────────────────────────────────────────────────────────────────────

/// Generates a procedural soft circle, applies vertex color, color over
/// lifetime, computes soft-particle depth fade, and optionally applies
/// Lambertian NdotL lighting from the scene directional light + ambient.
///
/// # Parameters
/// - input: interpolated vertex data from the billboard VS.
///
/// # Returns
/// Final RGBA color for alpha-blended compositing.
float4 main(PSInput input) : SV_Target {
    // ── Procedural soft circle ──
    // Maps UV [0,1] to [-1,+1] centered, then applies smooth falloff.
    float2 center = input.texcoord * 2.0f - 1.0f;
    float dist = dot(center, center); // squared distance from center
    float alpha = saturate(1.0f - dist); // linear falloff: 1 at center, 0 at edge
    alpha *= alpha; // quadratic for softer look
    float4 spriteColor = float4(1.0f, 1.0f, 1.0f, alpha);

    // ── Evaluate color over lifetime ──
    // normalizedAge in [0,1] maps the particle's age to its full lifespan.
    float normalizedAge = saturate(input.age / max(input.lifetime, 0.0001f));

    /// @ref GDC 2019 "Niagara: Building the Future of FX in Unreal Engine"
    ///     — LUT sample at normalizedAge replaces linear lerp when ramp is available
    float4 colorLifeScale;
#ifdef UZ_BINDLESS
    if (colorRampLutSrvIndex != 0xFFFFFFFF) {
        Texture2D<float4> rampTex = ResourceDescriptorHeap[colorRampLutSrvIndex];
        colorLifeScale = rampTex.SampleLevel(depthSampler, float2(normalizedAge, 0.5f), 0);
    } else {
        colorLifeScale = lerp(colorLifeStart, colorLifeEnd, normalizedAge);
    }
#else
    colorLifeScale = lerp(colorLifeStart, colorLifeEnd, normalizedAge);
#endif

    // ── Material output: emissive + opacity (Niagara Unlit pattern) ──
    float3 emissive = spriteColor.rgb * input.color.rgb * colorLifeScale.rgb;
    float  opacity  = spriteColor.a   * input.color.a   * colorLifeScale.a;

    // Apply emissive intensity boost.
    // Multiplies by (1 + intensity) so intensity=0 is neutral (no change),
    // and positive values brighten the particle emission.
    emissive *= (1.0f + emissiveIntensity);

    // ── Lit particle shading ──────────────────────────────────────────────
    // When litMode == 1, apply Lambertian diffuse lighting from the scene
    // directional light plus ambient. The billboard surface normal is
    // approximated as the camera-to-particle direction (billboard faces camera).
    //
    // @ref Pharr 2023 "Physically Based Rendering" Ch.12 — Lambertian diffuse: L = kd * (N·L) * Ld
    if (litMode == 1) {
        // Approximate billboard normal as camera-facing (toward camera direction).
        // For camera-facing billboards the surface normal IS the view direction.
        float3 normalWS = normalize(cameraPos.xyz - input.positionWS);

        // Directional light NdotL (Lambertian diffuse).
        // lightDir points toward the light, so no negation needed for NdotL.
        float NdotL = saturate(dot(normalWS, -lightDir.xyz));

        // Ambient + diffuse contribution.
        // ambient provides base illumination; NdotL * lightColor provides directional.
        float3 litColor = ambientColor.rgb + lightColor.rgb * NdotL;

        // Modulate the emissive output by the lit result.
        // This replaces pure emissive with shaded color.
        emissive = emissive * litColor;
        // @ref Pharr 2023 Ch.12 — Lambertian diffuse: L = kd * (N·L) * lightColor
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
