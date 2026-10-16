/// VFX Motion Vector Utilities
///
/// Shared functions for computing screen-space motion vectors from per-particle
/// previous-frame clip-space positions. Used by billboard and mesh particle
/// shaders to output velocity for TAA integration.
///
/// # Motion Vector Convention
/// Output is current minus previous in render-resolution pixels.
/// Positions use unjittered matrices. Positive X = rightward
/// motion, positive Y = downward motion (DX UV convention with Y flipped).
///
/// # Integration
/// The motion vector PS writes to an RG16F render target that the TAA resolve
/// pass samples to reject stale history for fast-moving particles.
///
/// @ref Karis 2014 "High Quality Temporal Supersampling" — TAA motion vectors
/// @ref Jimenez 2016 "Filmic SMAA" — per-object velocity buffer
/// @constants None
/// @deviations NONE

#ifndef VFX_MOTION_VECTORS_HLSLI
#define VFX_MOTION_VECTORS_HLSLI

// ─────────────────────────────────────────────────────────────────────────────
// ComputeMotionVector — screen-space velocity from clip-space positions
// ─────────────────────────────────────────────────────────────────────────────

/// Computes screen-space motion vector from current and previous clip-space positions.
///
/// Performs perspective divide on both positions to get NDC coordinates, then
/// converts to UV space [0,1] with Y flipped for DirectX convention (V increases
/// downward). The result is the per-pixel velocity that TAA uses to reproject
/// the previous frame's color.
///
/// @ref Karis 2014 "High Quality Temporal Supersampling" — NDC-to-UV velocity
/// @constants NDC to UV transform: uv = ndc * (0.5, -0.5) + 0.5
///
/// # Parameters
/// @param currentCS Current frame clip-space position (from VS output or SV_Position
///                  reconstructed to clip space). Must have valid w for perspective divide.
/// @param prevCS    Previous frame clip-space position (computed in VS via prevViewProj).
///                  Must have valid w for perspective divide.
/// @param renderSize Render-resolution dimensions in pixels.
///
/// # Returns
/// Screen-space velocity in render pixels: (currentUV - prevUV) * renderSize.
/// Zero when the projected particle position is unchanged.
///
/// # Edge Cases
/// - If prevCS.w <= 1e-6 (behind the camera or degenerate), returns float2(0, 0)
///   to avoid inf/NaN propagation into the velocity buffer.
/// - If currentCS.w is zero, the vertex would not be rasterized, so this case
///   does not arise in practice.
float2 ComputeMotionVector(float4 currentCS, float4 prevCS, float2 renderSize) {
    // Guard against behind-camera or degenerate previous-frame projection.
    // This can happen on the first frame when prevViewProj is identity and
    // the particle is at the origin, or for particles behind the previous camera.
    if (prevCS.w <= 1e-6f) {
        return float2(0.0f, 0.0f);
    }

    // Perspective divide to normalized device coordinates [-1, +1].
    float2 currentNDC = currentCS.xy / currentCS.w;
    float2 prevNDC    = prevCS.xy / prevCS.w;

    // NDC to UV space: [-1,+1] -> [0,1].
    // Y is flipped because DirectX UV convention has V increasing downward,
    // while NDC Y increases upward.
    float2 currentUV = currentNDC * float2(0.5f, -0.5f) + 0.5f;
    float2 prevUV    = prevNDC    * float2(0.5f, -0.5f) + 0.5f;

    return (currentUV - prevUV) * renderSize;
}

#endif // VFX_MOTION_VECTORS_HLSLI
