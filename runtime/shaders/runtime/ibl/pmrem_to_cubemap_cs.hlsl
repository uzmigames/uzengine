/// UzEngine PMREM atlas to cubemap extraction.
/// @ref Three.js PMREMGenerator — CubeUV atlas layout
///
/// Extracts all 6 cubemap faces at a single LOD from a Three.js CubeUV PMREM
/// atlas texture. Dispatch with Z=6 — DTid.z selects the face.
///
/// Atlas layout per LOD band (faceSize x faceSize per face):
///   Row 0: col 0 = +X, col 1 = -X, col 2 = +Y
///   Row 1: col 0 = -Y, col 1 = +Z, col 2 = -Z
///
/// Root constants at b0: { outputSize, atlasSrvIdx, atlasYOffset, atlasFaceSize,
///                         atlasWidth, atlasHeight, pad, pad }
/// Output: RWTexture2DArray<float4> cubemap at u0 (dispatch Z=6 for faces)

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

[numthreads(8, 8, 1)]
void main(uint3 DTid : SV_DispatchThreadID) {
    if (DTid.x >= outputSize || DTid.y >= outputSize) return;

    uint face = DTid.z; // 0-5: +X, -X, +Y, -Y, +Z, -Z

    // Map face index to atlas grid position:
    //   face 0 (+X): row 0, col 0
    //   face 1 (-X): row 0, col 1
    //   face 2 (+Y): row 0, col 2
    //   face 3 (-Y): row 1, col 0
    //   face 4 (+Z): row 1, col 1
    //   face 5 (-Z): row 1, col 2
    uint col = face % 3;
    uint row = face / 3;

    // Pixel coordinates within the atlas
    float atlasX = float(col * atlasFaceSize + DTid.x) + 0.5;
    float atlasY = float(atlasYOffset + row * atlasFaceSize + DTid.y) + 0.5;

    // Normalize to UV
    float2 atlasUV = float2(atlasX / float(atlasWidth), atlasY / float(atlasHeight));

#ifdef UZ_BINDLESS
    Texture2D<float4> atlasTex = ResourceDescriptorHeap[atlasSrvIdx];
    float3 color = atlasTex.SampleLevel(linearSampler, atlasUV, 0).rgb;
#else
    float3 color = atlasTexture.SampleLevel(linearSampler, atlasUV, 0).rgb;
#endif

    outputCubemap[DTid] = float4(color, 1.0);
}
