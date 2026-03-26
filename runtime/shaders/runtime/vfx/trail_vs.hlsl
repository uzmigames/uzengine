/// Trail/ribbon vertex shader — transforms ribbon vertices to clip space
/// and applies UV scrolling for animated trail textures.
///
/// The ribbon geometry is generated entirely on the CPU: the emitter system
/// walks the chain of anchor points and produces a quad strip, expanding each
/// edge perpendicular to the camera look direction. This VS merely projects
/// the final world-space positions to clip space and animates the U coordinate.
///
/// # UV Scroll
/// The U coordinate (along the trail length) is offset by `time * uvScrollRate`
/// each frame. A positive rate scrolls texture detail from head toward tail,
/// giving the illusion of motion along the ribbon. The V coordinate (width)
/// is passed through unchanged.
///
/// # Pipeline State Requirements
/// - Input Assembler: TrailVertex stride vertex buffer (see trail_common.hlsli).
/// - Rasterizer: CullMode = NONE (ribbon is single-sided but may fold).
/// - DepthStencil: DepthWrite = OFF (trails composite on top, no depth write).
/// - Blend: Premultiplied alpha (SrcOne / InvSrcAlpha).
///
/// # Register Layout (Shared Root Signature)
/// - b0: TrailRootConstants (32 bytes root constants — uvScrollRate used here)
/// - b1: PerFrameCB (root CBV — viewProj, timeParams.x = totalTime)

#include "trail_common.hlsli"

// ─────────────────────────────────────────────────────────────────────────────
// Constant Buffers
// ─────────────────────────────────────────────────────────────────────────────

/// Root constants at b0 — per-draw-call trail parameters.
/// Only uvScrollRate is consumed by the VS; the remaining fields are PS-only
/// but must be declared for correct layout (they share root param 0).
ConstantBuffer<TrailRootConstants> trailConstants : register(b0);

/// Per-frame constant buffer at b1 — standard engine layout.
/// The VS reads viewProj (for world->clip transform) and timeParams.x
/// (for UV scroll animation). All fields must be declared for correct
/// memory layout even though only a subset is consumed.
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
// Entry Point
// ─────────────────────────────────────────────────────────────────────────────

/// Transforms a ribbon vertex from world space to clip space and animates UVs.
///
/// # Parameters
/// - input: Per-vertex data from the ribbon quad strip (see TrailVertex).
///
/// # Returns
/// Clip-space position, scrolled UVs, interpolated color, and linear depth
/// for soft-particle depth comparison in the pixel shader.
TrailVSOutput main(TrailVertex input) {
    TrailVSOutput output;

    // ── Step 1: World -> Clip ────────────────────────────────────────────────
    // The ribbon CPU builder already places vertices in world space.
    // Multiply as a row vector (HLSL convention: vector * matrix).
    output.positionCS = mul(float4(input.position, 1.0f), viewProj);

    // ── Step 2: UV scroll along trail length ─────────────────────────────────
    // Offset U by accumulated time to animate the texture downstream.
    // V (ribbon width axis) is passed through unmodified.
    // timeParams.x = totalTime from PerFrameCB.
    output.uv = float2(
        input.uv.x + timeParams.x * trailConstants.uvScrollRate, // U: scroll along trail
        input.uv.y                                                 // V: width, no scroll
    );

    // ── Step 3: Pass-through color ───────────────────────────────────────────
    // CPU encodes lifetime fade and emitter tint in the vertex color.
    // The PS multiplies this against the texture sample, so no processing here.
    output.color = input.color;

    // ── Step 4: Linear depth for soft-particle comparison ───────────────────
    // positionCS.w is the (positive) view-space Z for perspective-correct
    // interpolation. We store it as a scalar interpolant so the PS can compare
    // it against the linearized scene depth without needing the inverse matrices.
    // Note: clip-space W equals view-space Z in a standard projection matrix,
    // which is positive in front of the camera (right-handed view space).
    output.linearDepth = output.positionCS.w;

    return output;
}
