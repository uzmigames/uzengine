/// DBuffer decal vertex shader — transforms a unit cube by the decal's world matrix.
///
/// The decal is rendered as a unit cube [-0.5, 0.5]^3 that is transformed by the
/// decal's world matrix (which encodes position, rotation, and scale/extents) to
/// form an oriented bounding box (OBB) projector in world space.
///
/// The pixel shader will reconstruct world position from depth and project it
/// back into decal local space to determine UV coordinates and clip.
///
/// @ref Valient 2014 "Deferred Rendering in Killzone: Shadow Fall" — DBuffer decals
/// @ref Pesce 2014 "Rendering Decals in Screen Space"

// ─────────────────────────────────────────────────────────────────────────────
// Constant Buffers
// ─────────────────────────────────────────────────────────────────────────────

/// Per-frame CB at b1 — camera viewProj matrix.
cbuffer PerFrameCB : register(b1) {
    row_major float4x4 viewProj;
    float4   cameraPos;
    // Remaining fields not needed by the decal VS.
};

/// Per-object CB at b2 — decal world matrix and inverse.
/// Must match the C++ DecalObjectCB struct layout exactly.
cbuffer DecalObjectCB : register(b2) {
    row_major float4x4 decalWorld;      // Decal world transform (position + rotation + extents)
    row_major float4x4 decalInvWorld;   // Inverse of decalWorld (for world->local projection in PS)
    float    decalOpacity;              // Global opacity [0,1]
    float    decalNormalBlend;          // Normal blend intensity [0,1]
    float    decalRoughness;            // Roughness value/multiplier
    float    decalMetallic;             // Metallic value/multiplier
    float    decalFadeDistance;         // Edge fade distance in cm
    float    decalAngleFade;            // Angle fade start in degrees
    uint     decalAlbedoSrv;            // Albedo texture SRV index (UINT32_MAX = none)
    uint     decalNormalSrv;            // Normal texture SRV index (UINT32_MAX = none)
    uint     decalMaterialSrv;          // Material texture SRV index (UINT32_MAX = none)
    uint     decalDepthSrv;             // GBuffer depth SRV index (for world pos reconstruction)
    uint     decalNormalGbufSrv;        // GBuffer normal SRV index (for angle fade)
    float    _pad0;                     // Pad to 16-byte boundary
};

// ─────────────────────────────────────────────────────────────────────────────
// Unit Cube Vertices
// ─────────────────────────────────────────────────────────────────────────────

/// 36-vertex unit cube (12 triangles, 6 faces).
/// Each face spans [-0.5, 0.5] on two axes, with the third axis at +/-0.5.
static const float3 kCubeVerts[36] = {
    // -X face
    float3(-0.5, -0.5, -0.5), float3(-0.5, -0.5,  0.5), float3(-0.5,  0.5,  0.5),
    float3(-0.5, -0.5, -0.5), float3(-0.5,  0.5,  0.5), float3(-0.5,  0.5, -0.5),
    // +X face
    float3( 0.5, -0.5, -0.5), float3( 0.5,  0.5,  0.5), float3( 0.5, -0.5,  0.5),
    float3( 0.5, -0.5, -0.5), float3( 0.5,  0.5, -0.5), float3( 0.5,  0.5,  0.5),
    // -Y face
    float3(-0.5, -0.5, -0.5), float3( 0.5, -0.5,  0.5), float3(-0.5, -0.5,  0.5),
    float3(-0.5, -0.5, -0.5), float3( 0.5, -0.5, -0.5), float3( 0.5, -0.5,  0.5),
    // +Y face
    float3(-0.5,  0.5, -0.5), float3(-0.5,  0.5,  0.5), float3( 0.5,  0.5,  0.5),
    float3(-0.5,  0.5, -0.5), float3( 0.5,  0.5,  0.5), float3( 0.5,  0.5, -0.5),
    // -Z face
    float3(-0.5, -0.5, -0.5), float3(-0.5,  0.5, -0.5), float3( 0.5,  0.5, -0.5),
    float3(-0.5, -0.5, -0.5), float3( 0.5,  0.5, -0.5), float3( 0.5, -0.5, -0.5),
    // +Z face
    float3(-0.5, -0.5,  0.5), float3( 0.5,  0.5,  0.5), float3(-0.5,  0.5,  0.5),
    float3(-0.5, -0.5,  0.5), float3( 0.5, -0.5,  0.5), float3( 0.5,  0.5,  0.5),
};

// ─────────────────────────────────────────────────────────────────────────────
// Output Structure
// ─────────────────────────────────────────────────────────────────────────────

struct DecalVSOutput {
    float4 positionCS : SV_Position;   ///< Clip-space position for rasterization.
    float4 screenPos  : TEXCOORD0;     ///< Screen-space position for UV reconstruction.
};

// ─────────────────────────────────────────────────────────────────────────────
// Entry Point
// ─────────────────────────────────────────────────────────────────────────────

/// Transforms a unit cube vertex by the decal world matrix and camera viewProj.
///
/// # Parameters
/// - vertexID: 0..35, indexing into the static unit cube vertex array.
///
/// # Returns
/// Clip-space position and screen position for the pixel shader.
DecalVSOutput main(uint vertexID : SV_VertexID) {
    DecalVSOutput output;

    float3 localPos = kCubeVerts[vertexID];
    float4 worldPos = mul(float4(localPos, 1.0), decalWorld);
    output.positionCS = mul(worldPos, viewProj);
    output.screenPos  = output.positionCS;

    return output;
}
