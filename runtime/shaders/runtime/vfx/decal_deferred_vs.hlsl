/// Deferred decal vertex shader — transforms unit-cube vertices to clip space.
///
/// Each decal is rendered as an oriented bounding box (OBB) in world space.
/// The vertex shader takes a unit cube in [-1, +1] object space and
/// transforms it by the decal's world matrix (which encodes position,
/// rotation, and scale of the decal volume).
///
/// The rasterizer should use front-face culling so that fragments are only
/// generated where the camera is inside the decal box, preventing popping
/// when the near plane intersects the volume.
///
/// # Pipeline State
/// - Input: unit cube vertices (POSITION float3) via Input Assembler
/// - Rasterizer: CullMode = FRONT (render back faces only)
/// - DepthStencil: DepthWrite = OFF, DepthFunc = GREATER_EQUAL (reversed-Z)
/// - Blend: per-RT alpha blending on G-Buffer targets
///
/// # Constant Buffers
/// - b0: cbDecal — per-decal transforms (decalWorldMatrix, viewProjection)

// ─────────────────────────────────────────────────────────────────────────────
// Constant Buffers
// ─────────────────────────────────────────────────────────────────────────────

/// Per-decal transform constants.
///
/// decalWorldMatrix: scales the [-1,+1] unit cube to the decal's OBB in
///   world space. Encodes position (translation), orientation (rotation),
///   and half-extents (scale) of the decal volume.
///
/// viewProjection: camera view-projection matrix for clip-space output.
cbuffer cbDecal : register(b0) {
    row_major float4x4 decalWorldMatrix; // Object -> World (unit cube -> decal OBB)
    row_major float4x4 viewProjection;   // World -> Clip
};

// ─────────────────────────────────────────────────────────────────────────────
// Input / Output Structures
// ─────────────────────────────────────────────────────────────────────────────

/// Vertex shader input — unit cube vertex from the Input Assembler.
struct VSInput {
    float3 position : POSITION; ///< Object-space position in [-1, +1].
};

/// Vertex shader output — clip-space position and world-space position
/// for the pixel shader to reconstruct scene depth and project decal UVs.
struct VSOutput {
    float4 positionCS : SV_Position; ///< Clip-space position for rasterization.
    float3 positionWS : TEXCOORD0;   ///< World-space position for PS depth check.
};

// ─────────────────────────────────────────────────────────────────────────────
// Entry Point
// ─────────────────────────────────────────────────────────────────────────────

/// Transforms a unit-cube vertex through the decal world matrix and the
/// camera's view-projection matrix.
///
/// # Parameters
/// - input: unit cube vertex with position in [-1, +1] range.
///
/// # Returns
/// Clip-space position and world-space position interpolant.
VSOutput main(VSInput input) {
    VSOutput output;

    // Transform unit cube vertex to world space via the decal's OBB matrix
    float4 worldPos = mul(float4(input.position, 1.0), decalWorldMatrix);
    output.positionWS = worldPos.xyz;

    // Transform to clip space for rasterization
    output.positionCS = mul(worldPos, viewProjection);

    return output;
}
