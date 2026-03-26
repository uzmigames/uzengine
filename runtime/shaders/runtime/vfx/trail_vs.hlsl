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
/// - Blend: SrcAlpha / InvSrcAlpha on the forward/transparency render target.
///
/// # Constant Buffers
/// - b0: cb_TrailVertexConstants — per-emitter time, scroll rate, view-projection.

#include "trail_common.hlsli"

// ─────────────────────────────────────────────────────────────────────────────
// Constant Buffers
// ─────────────────────────────────────────────────────────────────────────────

/// Per-emitter vertex constants updated once per draw call.
///
/// viewProjection: Combined view-projection matrix transforming world-space
///   positions to clip space. Row-major to match HLSL default mul() convention
///   (vector on the left: v * M).
///
/// time: Elapsed simulation time in seconds. Used to animate UV scrolling
///   so the trail texture flows along the ribbon regardless of frame rate.
///   Valid range: [0, +inf). Wrapping is left to the caller; the scroll
///   offset will loop naturally as UV coordinates are periodic.
///
/// uvScrollRate: Units of UV-space per second applied to the U axis.
///   Positive values scroll texture from head toward tail (downstream flow).
///   Negative values scroll upstream. Zero disables scrolling.
///   Typical range: [-4.0, 4.0].
///
/// padding: Explicit 8-byte pad so the constant buffer is 16-byte aligned.
///   Must remain zero — not read by the shader.
cbuffer cb_TrailVertexConstants : register(b0) {
    row_major float4x4 viewProjection; ///< World -> Clip transform.
    float              time;           ///< Simulation time in seconds.
    float              uvScrollRate;   ///< U-axis scroll speed (UV-units/second).
    float2             padding;        ///< Alignment pad — unused.
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
    output.positionCS = mul(float4(input.position, 1.0f), viewProjection);

    // ── Step 2: UV scroll along trail length ─────────────────────────────────
    // Offset U by accumulated time to animate the texture downstream.
    // V (ribbon width axis) is passed through unmodified.
    output.uv = float2(
        input.uv.x + time * uvScrollRate,  // U: scroll along trail
        input.uv.y                          // V: width, no scroll
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
