/// UzEngine PMREM atlas to cubemap conversion.
/// @ref Three.js PMREMGenerator cube_uv_reflection_fragment.glsl.js
///
/// Converts a Three.js CubeUV PMREM atlas (Y-up) into a UzEngine cubemap
/// (Z-up convention, matching sky_cubemap_cs.hlsl).
///
/// Three.js CubeUV face order per LOD band (NOT standard GL/DX order!):
///   Row 0: col 0 = +X (face 0), col 1 = +Y (face 1), col 2 = +Z (face 2)
///   Row 1: col 0 = -X (face 3), col 1 = -Y (face 4), col 2 = -Z (face 5)
///
/// Face UV mapping from Three.js getUV():
///   face 0 (+X): uv = ( dir.z,  dir.y) / |dir.x|
///   face 1 (+Y): uv = (-dir.x, -dir.z) / |dir.y|
///   face 2 (+Z): uv = (-dir.x,  dir.y) / |dir.z|
///   face 3 (-X): uv = (-dir.z,  dir.y) / |dir.x|
///   face 4 (-Y): uv = (-dir.x,  dir.z) / |dir.y|
///   face 5 (-Z): uv = ( dir.x,  dir.y) / |dir.z|
///
/// Root constants at b0: { outputSize, atlasSrvIdx, atlasYOffset, atlasFaceSize,
///                         atlasWidth, atlasHeight, pad, pad }
/// Output: RWTexture2DArray<float4> cubemap at u0 (dispatch Z=6 for DX faces)

#include "../common/constants.hlsli"

cbuffer RootConstants : register(b0) {
    uint outputSize;      ///< Output face resolution (= faceSize for this LOD).
    uint atlasSrvIdx;     ///< SRV index of the PMREM atlas (2D, RGBA16F).
    uint atlasYOffset;    ///< Y pixel offset of this LOD band in the atlas.
    uint atlasFaceSize;   ///< Face size at this LOD level (pixels).
    uint atlasWidth;      ///< Total atlas width (pixels).
    uint atlasHeight;     ///< Total atlas height (pixels).
    uint _pad0;
    uint _pad1;
};

#ifdef UZ_BINDLESS
#else
Texture2D<float4> atlasTexture : register(t0);
#endif

RWTexture2DArray<float4> outputCubemap : register(u0);
SamplerState linearSampler : register(s0);

/// DX cubemap face direction → Z-up world space.
/// Same function as sky_cubemap_cs.hlsl to ensure identical cubemap convention.
float3 CubeFaceDirZUp(uint face, float2 uv) {
    float u = uv.x * 2.0 - 1.0, v = uv.y * 2.0 - 1.0;
    float3 d;
    switch (face) {
        case 0: d = normalize(float3( 1, -v, -u)); break;
        case 1: d = normalize(float3(-1, -v,  u)); break;
        case 2: d = normalize(float3( u,  1,  v)); break;
        case 3: d = normalize(float3( u, -1, -v)); break;
        case 4: d = normalize(float3( u, -v,  1)); break;
        case 5: d = normalize(float3(-u, -v, -1)); break;
        default: d = float3(0, 0, 1); break;
    }
    return float3(d.x, d.z, d.y); // DX Y-up → Z-up world
}

/// Three.js getFace — maps Y-up direction to CubeUV face index.
/// @ref Three.js cube_uv_reflection_fragment.glsl.js getFace()
/// Face order: 0=+X, 1=+Y, 2=+Z, 3=-X, 4=-Y, 5=-Z
uint ThreeJSGetFace(float3 dir) {
    float3 a = abs(dir);
    if (a.x > a.z) {
        if (a.x > a.y) return dir.x > 0.0 ? 0 : 3;
        else           return dir.y > 0.0 ? 1 : 4;
    } else {
        if (a.z > a.y) return dir.z > 0.0 ? 2 : 5;
        else           return dir.y > 0.0 ? 1 : 4;
    }
}

/// Three.js getUV — maps Y-up direction + face to face-local UV [0,1]².
/// @ref Three.js cube_uv_reflection_fragment.glsl.js getUV()
float2 ThreeJSGetUV(float3 dir, uint face) {
    float2 uv;
    float3 a = abs(dir);
    switch (face) {
        case 0: uv = float2( dir.z,  dir.y) / a.x; break; // +X
        case 1: uv = float2(-dir.x, -dir.z) / a.y; break; // +Y
        case 2: uv = float2(-dir.x,  dir.y) / a.z; break; // +Z
        case 3: uv = float2(-dir.z,  dir.y) / a.x; break; // -X
        case 4: uv = float2(-dir.x,  dir.z) / a.y; break; // -Y
        case 5: uv = float2( dir.x,  dir.y) / a.z; break; // -Z
        default: uv = float2(0, 0); break;
    }
    return uv * 0.5 + 0.5;
}

[numthreads(8, 8, 1)]
void main(uint3 DTid : SV_DispatchThreadID) {
    if (DTid.x >= outputSize || DTid.y >= outputSize) return;

    uint outFace = DTid.z; // DX cubemap face 0-5
    float2 outUV = (float2(DTid.xy) + 0.5) / float(outputSize);

    // Step 1: Z-up world direction for this output texel
    float3 worldDir = CubeFaceDirZUp(outFace, outUV);

    // Step 2: Convert to Y-up (Three.js convention): swap Y↔Z
    float3 threejsDir = float3(worldDir.x, worldDir.z, worldDir.y);

    // Step 3: Three.js face + UV from the Y-up direction
    uint srcFace = ThreeJSGetFace(threejsDir);
    float2 srcUV = ThreeJSGetUV(threejsDir, srcFace);

    // Step 4: Map Three.js face to atlas grid position
    // Row 0: faces 0-2 (+X, +Y, +Z), Row 1: faces 3-5 (-X, -Y, -Z)
    uint col = srcFace < 3 ? srcFace : (srcFace - 3);
    uint row = srcFace < 3 ? 0 : 1;

    // Compute atlas pixel position
    float atlasX = (float(col) + srcUV.x) * float(atlasFaceSize);
    float atlasY = float(atlasYOffset) + (float(row) + srcUV.y) * float(atlasFaceSize);
    float2 atlasUV = float2(atlasX / float(atlasWidth), atlasY / float(atlasHeight));

#ifdef UZ_BINDLESS
    Texture2D<float4> atlasTex = ResourceDescriptorHeap[atlasSrvIdx];
    float3 color = atlasTex.SampleLevel(linearSampler, atlasUV, 0).rgb;
#else
    float3 color = atlasTexture.SampleLevel(linearSampler, atlasUV, 0).rgb;
#endif

    outputCubemap[DTid] = float4(color, 1.0);
}
