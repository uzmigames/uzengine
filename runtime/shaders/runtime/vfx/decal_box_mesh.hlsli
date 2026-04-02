/// Unit-cube vertex and index data for deferred decal box rendering.
///
/// The cube spans [-1, +1] on all axes. The decal world matrix scales and
/// orients this cube to form the decal's oriented bounding box (OBB) in
/// world space. Twelve triangles (36 indices) define the six faces.
///
/// # Usage
/// Bind as a vertex/index buffer, or reference directly for procedural
/// vertex fetch via SV_VertexID and SV_PrimitiveID.

#ifndef DECAL_BOX_MESH_HLSLI
#define DECAL_BOX_MESH_HLSLI

// ─────────────────────────────────────────────────────────────────────────────
// Unit Cube Vertices — 8 corners at all sign permutations of (1, 1, 1)
// ─────────────────────────────────────────────────────────────────────────────

/// Unit cube corner positions in [-1, +1] range.
///
/// Winding order when viewed from outside with the vertex layout below:
///
/// ```
///      6 ---- 7
///     /|     /|
///    2 ---- 3 |
///    | 4 ---| 5
///    |/     |/
///    0 ---- 1
/// ```
static const float3 kDecalBoxVertices[8] = {
    float3(-1.0, -1.0, -1.0), // 0: left  bottom back
    float3( 1.0, -1.0, -1.0), // 1: right bottom back
    float3(-1.0,  1.0, -1.0), // 2: left  top    back
    float3( 1.0,  1.0, -1.0), // 3: right top    back
    float3(-1.0, -1.0,  1.0), // 4: left  bottom front
    float3( 1.0, -1.0,  1.0), // 5: right bottom front
    float3(-1.0,  1.0,  1.0), // 6: left  top    front
    float3( 1.0,  1.0,  1.0), // 7: right top    front
};

// ─────────────────────────────────────────────────────────────────────────────
// Unit Cube Indices — 12 triangles, front-face = CW when viewed from outside
// ─────────────────────────────────────────────────────────────────────────────

/// 36 indices forming 12 triangles for the six faces of the unit cube.
/// Winding is clockwise when viewed from outside the cube (standard
/// front-face for back-face culling when the camera is inside the box).
///
/// For deferred decals the rasterizer uses front-face culling so pixels
/// are only generated where the camera is inside the decal volume.
static const uint kDecalBoxIndices[36] = {
    // -Z face (back)
    0, 2, 1,
    1, 2, 3,
    // +Z face (front)
    4, 5, 6,
    5, 7, 6,
    // -X face (left)
    0, 4, 2,
    2, 4, 6,
    // +X face (right)
    1, 3, 5,
    3, 7, 5,
    // -Y face (bottom)
    0, 1, 4,
    1, 5, 4,
    // +Y face (top)
    2, 6, 3,
    3, 6, 7,
};

/// Total number of indices in the decal box mesh.
static const uint kDecalBoxIndexCount = 36;

/// Total number of vertices in the decal box mesh.
static const uint kDecalBoxVertexCount = 8;

#endif // DECAL_BOX_MESH_HLSLI
