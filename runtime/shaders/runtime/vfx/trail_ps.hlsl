/// Trail/ribbon pixel shader — samples the trail texture, applies per-vertex
/// color, alpha-tests thin ribbons, and optionally soft-fades against scene
/// depth to avoid hard intersections with opaque geometry.
///
/// # Soft Particle Depth Fade
/// When `softDepthRange > 0`, the shader reads the scene depth buffer at the
/// current screen pixel, linearizes it via the camera near/far planes (stored
/// in cb_TrailPixelConstants), and computes the world-space distance between
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
/// - DepthStencil: DepthWrite = OFF, DepthFunc = GREATER_EQUAL (reversed-Z).
/// - Blend: Premultiplied (SrcOne / InvSrcAlpha) or standard alpha
///          (SrcAlpha / InvSrcAlpha) — match the emitter blend mode setting.
/// - Rasterizer: CullMode = NONE.
///
/// # Constant Buffers
/// - b1: cb_TrailPixelConstants — soft depth range, alpha clip, screen size,
///        camera near/far planes.
///
/// # Texture Inputs
/// - t0: trailTexture — trail colour/alpha map (RGBA8_SRGB recommended).
/// - t1: sceneDepth   — reversed-Z scene depth buffer (R32_FLOAT or R24_UNORM).
///                      Only sampled when softDepthRange > 0.
/// - s0: linearSampler — bilinear wrap for the trail texture.

#include "../include/depth_common.hlsli"
#include "trail_common.hlsli"

// ─────────────────────────────────────────────────────────────────────────────
// Constant Buffers
// ─────────────────────────────────────────────────────────────────────────────

/// Per-emitter pixel constants, updated once per draw call.
///
/// softDepthRange: World-space distance in metres over which soft-particle
///   fading is applied. Fragments within this distance of an opaque surface
///   linearly fade to zero alpha. Set to 0.0 to disable the feature entirely
///   (avoids the depth texture read and linearization cost).
///   Valid range: [0, +inf). Typical value: 0.5 to 2.0 metres.
///
/// alphaClipThreshold: Fragments with final alpha below this value are
///   discarded. Set to 0.0 to disable alpha testing. Setting this too high
///   will clip thin or faded trail tails prematurely.
///   Valid range: [0, 1]. Typical value: 1/255 ≈ 0.0039.
///
/// screenDimensions: Viewport width and height in pixels. Used to convert
///   the SV_Position screen-space pixel coordinate to a [0,1] UV for
///   sampling the scene depth buffer at the current pixel.
///
/// cameraNear: Camera near plane distance in metres (positive). Required
///   for depth linearization. Must match the projection matrix.
///
/// cameraFar: Camera far plane distance in metres (positive). Required
///   for depth linearization. Must match the projection matrix.
///
/// _pad: Explicit 8-byte pad for 16-byte cbuffer alignment. Unused.
cbuffer cb_TrailPixelConstants : register(b1) {
    float  softDepthRange;      ///< Soft-fade radius in metres. 0 = disabled.
    float  alphaClipThreshold;  ///< Alpha discard threshold in [0, 1].
    float2 screenDimensions;    ///< Viewport (width, height) in pixels.
    float  cameraNear;          ///< Camera near plane in metres.
    float  cameraFar;           ///< Camera far plane in metres.
    float2 _pad;                ///< Alignment pad — unused.
};

// ─────────────────────────────────────────────────────────────────────────────
// Texture / Sampler Inputs
// ─────────────────────────────────────────────────────────────────────────────

/// Trail colour and alpha texture. Alpha encodes trail shape/feathering.
/// Typically authored as a horizontal strip animated via UV scroll.
Texture2D<float4> trailTexture  : register(t0);

/// Scene depth buffer — reversed-Z, matches the depth_common.hlsli convention.
/// Bound only when soft depth fade is active (softDepthRange > 0).
/// Unbound when the feature is disabled; Load() is guarded by the branch.
Texture2D<float>  sceneDepth    : register(t1);

/// Bilinear clamp sampler for trail texture. Wrap mode on U allows tiling for
/// repeating texture patterns driven by UV scroll (set wrap on U in the PSO).
SamplerState      linearSampler : register(s0);

// ─────────────────────────────────────────────────────────────────────────────
// Helper Functions
// ─────────────────────────────────────────────────────────────────────────────

/// Computes a soft-particle depth fade factor for the current fragment.
///
/// Reads the reversed-Z scene depth at the current screen pixel, converts both
/// the scene depth and the ribbon fragment depth to linear view-space distances,
/// and returns a fade weight that is 0 at the intersection surface and reaches
/// 1 at `softDepthRange` metres in front of it.
///
/// # Arguments
/// - screenPos:     Screen-space pixel coordinate from SV_Position (xy used).
/// - fragmentDepth: Linear view-space depth of the ribbon fragment in metres
///                  (positive in front of the camera). Interpolated from the VS.
/// - fadeRange:     World-space fade radius in metres. Must be > 0.
///
/// # Returns
/// Fade multiplier in [0, 1]. 0 = at or behind the opaque surface,
/// 1 = `fadeRange` or more metres in front of it.
float ComputeSoftDepthFade(float4 screenPos, float fragmentDepth, float fadeRange) {
    // Read the raw reversed-Z depth at this pixel (integer texel fetch)
    int2 pixelCoord = int2(screenPos.xy);
    float rawSceneDepth = sceneDepth.Load(int3(pixelCoord, 0));

    // Sky pixels have no opaque surface — no fading needed
    if (IsSkyDepth(rawSceneDepth)) {
        return 1.0f;
    }

    // Linearize scene depth from reversed-Z to view-space metres
    float sceneLinearDepth = LinearizeDepthReversedZ(rawSceneDepth, cameraNear, cameraFar);

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
    float4 texSample = trailTexture.Sample(linearSampler, input.uv);

    // ── Step 2: Combine with per-vertex tint and lifetime alpha ───────────────
    // Vertex color encodes: RGB = emitter colour-over-lifetime tint,
    // A = lifetime fade (CPU drives this from 1 -> 0 as the segment ages).
    float4 finalColor = texSample * input.color;

    // ── Step 3: Soft-particle depth fade (optional) ───────────────────────────
    // Branch on the cbuffer value — constant across the draw call so this is
    // a uniform branch (no divergence across warps/waves in practice).
    if (softDepthRange > 0.0f) {
        float softFade = ComputeSoftDepthFade(input.positionCS, input.linearDepth, softDepthRange);
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
