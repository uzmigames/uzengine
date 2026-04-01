/// UzEngine Decal Vertex Shader — transforms a procedural unit cube by the
/// decal's world matrix and view-projection.
///
/// @ref Valient 2014 "Deferred Rendering in Killzone: Shadow Fall" — DBuffer decals
///
/// SV_VertexID [0..35] indexes 12 triangles (6 faces × 2 triangles each).

cbuffer DecalObjectCB : register(b2) {
    row_major float4x4 decalWorld;      // 0
    row_major float4x4 svPosToDecal;    // 64  (unused in VS)
    row_major float4x4 viewProj;        // 128
    float    opacity;          // 192
    float    normalBlend;
    float    roughness;
    float    metallic;
    float    fadeDistance;
    float    angleFade;
    uint     albedoSrv;
    uint     normalSrv;
    uint     materialSrv;
    uint     depthSrv;
    uint     normalGbufSrv;
    uint     opacitySrv;
};

struct VSOutput {
    float4 positionCS : SV_Position;
};

/// Unit cube vertex positions (36 vertices = 12 triangles, no index buffer).
/// @constants [-0.5, 0.5]^3 unit cube, 6 faces × 2 triangles
static const float3 kCubeVerts[36] = {
    // -X face
    float3(-0.5, -0.5, -0.5), float3(-0.5, -0.5,  0.5), float3(-0.5,  0.5,  0.5),
    float3(-0.5, -0.5, -0.5), float3(-0.5,  0.5,  0.5), float3(-0.5,  0.5, -0.5),
    // +X face
    float3( 0.5, -0.5, -0.5), float3( 0.5,  0.5,  0.5), float3( 0.5, -0.5,  0.5),
    float3( 0.5, -0.5, -0.5), float3( 0.5,  0.5, -0.5), float3( 0.5,  0.5,  0.5),
    // -Y face
    float3(-0.5, -0.5, -0.5), float3( 0.5, -0.5, -0.5), float3( 0.5, -0.5,  0.5),
    float3(-0.5, -0.5, -0.5), float3( 0.5, -0.5,  0.5), float3(-0.5, -0.5,  0.5),
    // +Y face
    float3(-0.5,  0.5, -0.5), float3( 0.5,  0.5,  0.5), float3( 0.5,  0.5, -0.5),
    float3(-0.5,  0.5, -0.5), float3(-0.5,  0.5,  0.5), float3( 0.5,  0.5,  0.5),
    // -Z face
    float3(-0.5, -0.5, -0.5), float3( 0.5,  0.5, -0.5), float3( 0.5, -0.5, -0.5),
    float3(-0.5, -0.5, -0.5), float3(-0.5,  0.5, -0.5), float3( 0.5,  0.5, -0.5),
    // +Z face
    float3(-0.5, -0.5,  0.5), float3( 0.5, -0.5,  0.5), float3( 0.5,  0.5,  0.5),
    float3(-0.5, -0.5,  0.5), float3( 0.5,  0.5,  0.5), float3(-0.5,  0.5,  0.5),
};

VSOutput main(uint vertexId : SV_VertexID) {
    VSOutput output;
    float3 localPos = kCubeVerts[vertexId];
    float4 worldPos = mul(float4(localPos, 1.0), decalWorld);
    output.positionCS = mul(worldPos, viewProj);
    return output;
}
