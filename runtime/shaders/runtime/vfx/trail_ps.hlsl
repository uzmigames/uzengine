/// Trail/ribbon pixel shader — samples the trail texture, applies per-vertex
/// color, alpha-tests thin ribbons, and optionally soft-fades against scene
/// depth to avoid hard intersections with opaque geometry.
///
/// # Soft Particle Depth Fade
/// When `softDepthRange > 0`, the shader reads the scene depth buffer at the
/// current screen pixel, linearizes it via the camera near/far planes (stored
/// in PerFrameCB at b1), and computes the world-space distance between
/// the ribbon fragment and the underlying opaque surface. Fragments closer than
/// `softDepthRange` metres fade out smoothly, eliminating the harsh cutoff line
/// where the ribbon intersects opaque meshes.
///
/// # Alpha Test
/// Fragments whose final alpha falls below `alphaClipThreshold` are discarded.
/// The threshold must be > 0 to activate testing; set it to 0 to allow full
/// transparency (useful for additive blend passes where black is transparent).
///
/// # Output
/// A single float4 colour with premultiplied alpha so the render target blend
/// equation (SrcOne / InvSrcAlpha) composites correctly over opaque geometry.
///
/// # Pipeline State Requirements
/// - DepthStencil: DepthWrite = OFF, DepthFunc = GREATER (reversed-Z).
/// - Blend: Premultiplied (SrcOne / InvSrcAlpha).
/// - Rasterizer: CullMode = NONE.
///
/// # Register Layout (Shared Root Signature)
/// - b0: TrailRootConstants (32 bytes root constants — softDepthRange,
///        alphaClipThreshold, trailTexSrvIndex, depthBufferSrvIndex)
/// - b1: PerFrameCB (root CBV — nearPlane, farPlane for depth linearization)
///
/// # Texture Inputs (SM5 fallback only)
/// - t0: trailTexture — trail colour/alpha map (RGBA8_SRGB recommended).
/// - t1: sceneDepth   — reversed-Z scene depth buffer (R32_FLOAT or R24_UNORM).
/// - s0: linearSampler — bilinear wrap for the trail texture (static sampler).

#include "../include/depth_common.hlsli"
#include "trail_common.hlsli"

// ─────────────────────────────────────────────────────────────────────────────
// Constant Buffers
// ─────────────────────────────────────────────────────────────────────────────

/// Root constants at b0 — per-draw-call trail parameters.
/// The PS reads softDepthRange, alphaClipThreshold, and bindless SRV indices.
/// Expanded inline for SM5.0 compatibility (cbuffer syntax required by DX11).
/// Layout MUST match TrailRootConstants in trail_common.hlsli exactly.
cbuffer TrailRootCB : register(b0) {
    float uvScrollRate;          ///< U-axis scroll speed (VS-only, declared for layout).
    float softDepthRange;        ///< Soft-fade radius in metres. 0 = disabled.
    float alphaClipThreshold;    ///< Alpha discard threshold [0, 1].
    uint  trailTexSrvIndex;      ///< Bindless SRV index for trail texture (0xFFFFFFFF = none).
    uint  depthBufferSrvIndex;   ///< Bindless SRV index for scene depth buffer.
    uint  vertexBufferSrvIndex;  ///< Bindless SRV index for trail VB (VS-only, declared for layout).
    uint  indexBufferSrvIndex;   ///< Bindless SRV index for trail IB (VS-only, declared for layout).
    uint  _cbPad0;
};

/// Per-frame constant buffer at b1 — standard engine layout.
/// The PS reads nearPlane and farPlane for depth linearization.
/// All fields must be declared for correct memory layout.
cbuffer PerFrameCB : register(b1) {
    row_major float4x4 viewProj;    ///< View * Projection matrix.
    float4   cameraPos;             ///< xyz = camera world position, w = unused.
    float4   timeParams;            ///< x = totalTime, y = deltaTime, z = frameCount, w = unused.
    float4   lightDir;              ///< xyz = direction toward light, w = unused.
    float4   lightColor;            ///< xyz = color * intensity, w = unused.
    float4   ambientColor;          ///< xyz = ambient radiance, w = unused.
    row_major float4x4 invViewProj; ///< Inverse view-projection (declared for layout).
    float    nearPlane;             ///< Camera near plane distance.
    float    farPlane;              ///< Camera far plane distance.
    uint     _perFramePad0;
    uint     _perFramePad1;
    uint     _perFramePad2;
    uint     _perFramePad3;
    float    _perFramePad4;
    float    _perFramePad5;
};

// ─────────────────────────────────────────────────────────────────────────────
// Texture / Sampler Inputs
// ─────────────────────────────────────────────────────────────────────────────

#ifndef UZ_BINDLESS
/// Trail colour and alpha texture (SM5 fallback path).
/// Alpha encodes trail shape/feathering.
Texture2D<float4> trailTexture  : register(t0);

/// Scene depth buffer (SM5 fallback path) — reversed-Z.
/// Bound only when soft depth fade is active (softDepthRange > 0).
Texture2D<float>  sceneDepth    : register(t1);
#endif

/// Bilinear wrap sampler for trail texture. Static sampler s0 from root signature.
SamplerState linearSampler : register(s0);

// ─────────────────────────────────────────────────────────────────────────────
// Helper Functions
// ─────────────────────────────────────────────────────────────────────────────

/// Computes a soft-particle depth fade factor for the current fragment.
///
/// Reads the reversed-Z scene depth at the current screen pixel, converts both
/// the scene depth and the ribbon fragment depth to linear view-space distances,
/// and returns a fade weight that is 0 at the intersection surface and reaches
/// 1 at `fadeRange` metres in front of it.
///
/// # Arguments
/// - screenPos:     Screen-space pixel coordinate from SV_Position (xy used).
/// - fragmentDepth: Linear view-space depth of the ribbon fragment in metres
///                  (positive in front of the camera). Interpolated from the VS.
/// - fadeRange:     World-space fade radius in metres. Must be > 0.
/// - depthTex:      Scene depth texture to sample at the current pixel.
///
/// # Returns
/// Fade multiplier in [0, 1]. 0 = at or behind the opaque surface,
/// 1 = `fadeRange` or more metres in front of it.
float ComputeSoftDepthFade(float4 screenPos, float fragmentDepth, float fadeRange,
                           Texture2D<float> depthTex) {
    // Read the raw reversed-Z depth at this pixel (integer texel fetch)
    int2 pixelCoord = int2(screenPos.xy);
    float rawSceneDepth = depthTex.Load(int3(pixelCoord, 0));

    // Sky pixels have no opaque surface — no fading needed
    if (IsSkyDepth(rawSceneDepth)) {
        return 1.0f;
    }

    // Linearize scene depth from reversed-Z to view-space metres
    float sceneLinearDepth = LinearizeDepthReversedZ(rawSceneDepth, nearPlane, farPlane);

    // Depth difference: positive when the fragment is in front of the surface.
    // A negative difference means the ribbon is occluded (should not happen
    // for properly sorted transparent geometry, but clamp guards against it).
    float depthDiff = sceneLinearDepth - fragmentDepth;

    // Remap [0, fadeRange] -> [0, 1], clamped so occluded fragments = 0
    return saturate(depthDiff / (fadeRange + kSoftDepthEpsilon));
}

// ─────────────────────────────────────────────────────────────────────────────
// Entry Point
// ─────────────────────────────────────────────────────────────────────────────

/// Samples the trail texture, composites with vertex color, and optionally
/// applies a soft-particle fade before discarding thin or transparent fragments.
///
/// # Algorithm
/// 1. Sample trailTexture at the scrolled UV from the vertex shader.
/// 2. Multiply the texture sample by the interpolated vertex colour (RGBA).
/// 3. Optionally compute and apply soft-particle depth fade (if softDepthRange > 0).
/// 4. Discard fragments below alphaClipThreshold.
/// 5. Convert to premultiplied alpha and output.
///
/// # Parameters
/// - input: Interpolated data from TrailVSOutput (see trail_common.hlsli).
///
/// # Returns
/// Premultiplied RGBA for the transparency composite render target.
float4 main(TrailVSOutput input) : SV_Target0 {

    // ── Step 1: Sample trail texture ─────────────────────────────────────────
    // When trailTexSrvIndex == 0xFFFFFFFF, no texture is bound — use white
    // so vertex color provides all tinting and alpha.
    float4 texSample;

#ifdef UZ_BINDLESS
    if (trailTexSrvIndex != 0xFFFFFFFF) {
        Texture2D<float4> trailTex = ResourceDescriptorHeap[trailTexSrvIndex];
        texSample = trailTex.Sample(linearSampler, input.uv);
    } else {
        texSample = float4(1.0, 1.0, 1.0, 1.0);
    }
#else
    texSample = trailTexture.Sample(linearSampler, input.uv);
#endif

    // ── Step 2: Combine with per-vertex tint and lifetime alpha ───────────────
    // Vertex color encodes: RGB = emitter colour-over-lifetime tint,
    // A = lifetime fade (CPU drives this from 1 -> 0 as the segment ages).
    float4 finalColor = texSample * input.color;

    // ── Step 3: Soft-particle depth fade (optional) ───────────────────────────
    // Branch on the root constant value — constant across the draw call so
    // this is a uniform branch (no divergence across warps/waves in practice).
    if (softDepthRange > 0.0f) {
#ifdef UZ_BINDLESS
        Texture2D<float> depthTex = ResourceDescriptorHeap[depthBufferSrvIndex];
#else
        Texture2D<float> depthTex = sceneDepth;
#endif
        float softFade = ComputeSoftDepthFade(input.positionCS, input.linearDepth,
                                              softDepthRange, depthTex);
        finalColor.a *= softFade;
    }

    // ── Step 4: Alpha test ────────────────────────────────────────────────────
    // Discard fully transparent fragments early to avoid unnecessary blending
    // and to preserve the depth pre-pass benefit for opaque regions of the trail.
    // The threshold is intentionally tiny (default ~1/255) to catch numerical
    // near-zeros from texture border sampling without cutting visible trail edges.
    if (finalColor.a < alphaClipThreshold) {
        discard;
    }

    // ── Step 5: Premultiplied alpha conversion ────────────────────────────────
    // Premultiplied alpha allows correct blending over multiple layers without
    // double-darkening at partially-transparent edges. The blend equation is
    // SrcOne / InvSrcAlpha: result = finalColor.rgb + dest.rgb * (1 - finalColor.a).
    finalColor.rgb *= finalColor.a;

    return finalColor;
}
