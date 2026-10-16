// Deliberately wrong: scalar arrays in a cbuffer occupy one 16-byte row per element.
// The production shader is currently an empty kernel; this is a packing negative
// control against GpuCullConstants, not evidence that GPU culling is implemented.
cbuffer CullConstants : register(b1) {
    row_major float4x4 viewProjection;
    row_major float4x4 viewMatrix;
    row_major float4x4 viewToClip;
    uint instanceCount;
    uint hizEnabled;
    uint hizWidth;
    uint hizHeight;
    uint hizMipCount;
    uint3 _pad0;
    uint g_HiZMipSrvIndices[12];
    uint4 _pad1;
};
