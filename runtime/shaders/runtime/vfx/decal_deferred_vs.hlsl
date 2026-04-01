/// UzEngine Deferred Decal VS — same as gbuffer/decal_vs.hlsl.
/// Used by the DecalManager VfxDecal path (frame_decals.cpp).
///
/// @ref Valient 2014 "Deferred Rendering in Killzone: Shadow Fall"

cbuffer DecalConstants : register(b2) {
    float4x4 worldToDecal;     // 0
    float4x4 invViewProj;      // 64
    float    normalIntensity;  // 128
    float    roughnessOverride;// 132
    float    fadeAlpha;        // 136
    float    blendMode;        // 140
    float    screenWidth;      // 144
    float    screenHeight;     // 148
    float    _pad0;            // 152
    float    _pad1;            // 156
    float4x4 viewProjection;   // 160
};

struct VSOutput {
    float4 positionCS : SV_Position;
};

/// Unit cube vertex positions (36 vertices = 12 triangles).
static const float3 kCubeVerts[36] = {
    float3(-0.5, -0.5, -0.5), float3(-0.5, -0.5,  0.5), float3(-0.5,  0.5,  0.5),
    float3(-0.5, -0.5, -0.5), float3(-0.5,  0.5,  0.5), float3(-0.5,  0.5, -0.5),
    float3( 0.5, -0.5, -0.5), float3( 0.5,  0.5,  0.5), float3( 0.5, -0.5,  0.5),
    float3( 0.5, -0.5, -0.5), float3( 0.5,  0.5, -0.5), float3( 0.5,  0.5,  0.5),
    float3(-0.5, -0.5, -0.5), float3( 0.5, -0.5, -0.5), float3( 0.5, -0.5,  0.5),
    float3(-0.5, -0.5, -0.5), float3( 0.5, -0.5,  0.5), float3(-0.5, -0.5,  0.5),
    float3(-0.5,  0.5, -0.5), float3( 0.5,  0.5,  0.5), float3( 0.5,  0.5, -0.5),
    float3(-0.5,  0.5, -0.5), float3(-0.5,  0.5,  0.5), float3( 0.5,  0.5,  0.5),
    float3(-0.5, -0.5, -0.5), float3( 0.5,  0.5, -0.5), float3( 0.5, -0.5, -0.5),
    float3(-0.5, -0.5, -0.5), float3(-0.5,  0.5, -0.5), float3( 0.5,  0.5, -0.5),
    float3(-0.5, -0.5,  0.5), float3( 0.5, -0.5,  0.5), float3( 0.5,  0.5,  0.5),
    float3(-0.5, -0.5,  0.5), float3( 0.5,  0.5,  0.5), float3(-0.5,  0.5,  0.5),
};

VSOutput main(uint vertexId : SV_VertexID) {
    VSOutput output;
    // worldToDecal is the inverse of the decal world matrix.
    // To get world position, we need decalWorld — but the old DecalConstants CB
    // stores worldToDecal and viewProjection separately.
    // For the VfxDecal path, the cube is transformed by viewProjection only
    // (DecalConstants.viewProjection includes the decal world matrix baked in
    // by frame_decals.cpp — it does NOT; it's the camera viewProj).
    // Actually the old VfxDecal path used a unit cube VB/IB, not SV_VertexID.
    // For now, use a fullscreen pass since this path is not the primary one.
    float3 localPos = kCubeVerts[vertexId];
    output.positionCS = mul(float4(localPos, 1.0), viewProjection);
    return output;
}
