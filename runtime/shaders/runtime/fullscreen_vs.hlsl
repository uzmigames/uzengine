/// Fullscreen triangle vertex shader — no vertex buffer required.
///
/// Generates a single triangle that covers the entire screen using only
/// SV_VertexID (0, 1, 2). This is more efficient than a fullscreen quad
/// because it avoids the diagonal edge where two triangles overlap, which
/// causes redundant pixel shader invocations along the seam.
///
/// # Vertex Layout (counter-clockwise)
/// ```
/// ID  Position (NDC)   UV
///  0  (-1, -1)         (0, 1)
///  1  (-1,  3)         (0, -1)
///  2  ( 3, -1)         (2, 1)
/// ```
///
/// The oversized triangle is clipped by the rasterizer to the viewport.
/// UVs interpolate to [0,0]-[1,1] within the visible screen area.
///
/// # Usage
/// Draw with: `DrawInstanced(3, 1, 0, 0)` — no vertex/index buffer bound.

// ─────────────────────────────────────────────────────────────────────────────
// Output Structure
// ─────────────────────────────────────────────────────────────────────────────

/// Interpolants passed to the fullscreen pixel shader.
struct FSOutput {
    float4 positionCS : SV_Position; ///< Clip-space position for rasterization.
    float2 texcoord   : TEXCOORD0;   ///< Screen UV in [0,1] after rasterizer clipping.
};

// ─────────────────────────────────────────────────────────────────────────────
// Vertex Shader Entry Point
// ─────────────────────────────────────────────────────────────────────────────

/// Generates fullscreen triangle vertices from SV_VertexID.
///
/// # Parameters
/// - vertexID: must be 0, 1, or 2 (from DrawInstanced(3, 1, 0, 0)).
///
/// # Returns
/// Clip-space position and UV for a triangle that fully covers the viewport.
FSOutput main(uint vertexID : SV_VertexID) {
    FSOutput output;

    // Compute UV from vertex ID using bit tricks:
    //   ID 0 -> uv (0, 0)  ->  pos (-1, +1)
    //   ID 1 -> uv (2, 0)  ->  pos (+3, +1)
    //   ID 2 -> uv (0, 2)  ->  pos (-1, -3)
    // After rasterizer clipping, visible UVs span [0,1].
    float2 uv = float2((vertexID << 1) & 2, vertexID & 2);

    // Map UV [0,1] to NDC [-1,+1], with Y flipped so UV (0,0) = top-left
    output.positionCS = float4(uv * 2.0f - 1.0f, 0.0f, 1.0f);

    // Flip V so that UV (0,0) is top-left matching D3D texture convention
    output.texcoord = float2(uv.x, 1.0f - uv.y);

    return output;
}
