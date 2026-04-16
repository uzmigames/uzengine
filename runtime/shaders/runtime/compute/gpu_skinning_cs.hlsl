/// UzEngine GPU Skinning compute shader.
///
/// Transforms SkinnedVertex (56 B) inputs into standard Vertex (48 B) outputs
/// by applying a 4-bone linear blend skin using the per-entity bone palette.
///
/// # Layout
/// - Input  (SkinnedVertex, 56 B): pos(12) + norm(12) + uv(8) + tan(16) +
///   boneIndices(4 × uint8) + boneWeights(4 × uint8).
/// - Palette (float4x4 per bone): bone palette[i] = worldBone[i] * inverseBind[i].
/// - Output (Vertex, 48 B): pos(12) + norm(12) + uv(8) + tan(16).
///
/// # Thread group
/// 64 threads per group, 1 vertex per thread. Dispatch ceil(vertexCount / 64) groups.
///
/// # Entry point
/// `main` — UzEngine ShaderManager always compiles with entryPoint = "main".
///
/// @ref Linear blend skinning, Magnenat-Thalmann 1988 "Joint-dependent local
///      deformations for hand animation and object grasping"

struct SkinnedVertex {
    float3 position;
    float3 normal;
    float2 texcoord;
    float4 tangent;
    uint   boneIndices; // 4 × uint8 packed into 32-bit word
    uint   boneWeights; // 4 × uint8 normalized [0,255]
};

struct Vertex {
    float3 position;
    float3 normal;
    float2 texcoord;
    float4 tangent;
};

struct PushConstants {
    uint inputBufferIdx;   ///< SRV bindless index of SkinnedVertex[] input.
    uint paletteBufferIdx; ///< SRV bindless index of float4x4[] bone palette.
    uint outputBufferIdx;  ///< UAV bindless index of Vertex[] output.
    uint vertexCount;      ///< Total vertices in the input buffer.
};
ConstantBuffer<PushConstants> push : register(b0);

[numthreads(64, 1, 1)]
void main(uint3 DTid : SV_DispatchThreadID) {
    const uint vidx = DTid.x;
    if (vidx >= push.vertexCount) return;

    StructuredBuffer<SkinnedVertex>     inputVerts  = ResourceDescriptorHeap[push.inputBufferIdx];
    StructuredBuffer<row_major float4x4> palette    = ResourceDescriptorHeap[push.paletteBufferIdx];
    RWStructuredBuffer<Vertex>          outputVerts = ResourceDescriptorHeap[push.outputBufferIdx];

    SkinnedVertex sv = inputVerts[vidx];

    // Unpack 4-bone indices (uint8 × 4 packed into uint).
    const uint idx0 = (sv.boneIndices >>  0) & 0xFFu;
    const uint idx1 = (sv.boneIndices >>  8) & 0xFFu;
    const uint idx2 = (sv.boneIndices >> 16) & 0xFFu;
    const uint idx3 = (sv.boneIndices >> 24) & 0xFFu;

    // Unpack 4-bone weights and normalize from [0,255] → [0,1].
    const float inv255 = 1.0 / 255.0;
    const float w0 = float((sv.boneWeights >>  0) & 0xFFu) * inv255;
    const float w1 = float((sv.boneWeights >>  8) & 0xFFu) * inv255;
    const float w2 = float((sv.boneWeights >> 16) & 0xFFu) * inv255;
    const float w3 = float((sv.boneWeights >> 24) & 0xFFu) * inv255;

    // Weighted sum of bone skinning matrices (linear blend skinning).
    float4x4 skin = palette[idx0] * w0
                  + palette[idx1] * w1
                  + palette[idx2] * w2
                  + palette[idx3] * w3;

    // Row-vector * row_major matrix convention (matches mesh_vs.hlsl).
    const float4 posS = mul(float4(sv.position, 1.0), skin);
    const float3 nS   = mul(float4(sv.normal,   0.0), skin).xyz;
    const float3 tS   = mul(float4(sv.tangent.xyz, 0.0), skin).xyz;

    Vertex outV;
    outV.position = posS.xyz;
    outV.normal   = normalize(nS);
    outV.tangent  = float4(normalize(tS), sv.tangent.w);
    outV.texcoord = sv.texcoord;

    outputVerts[vidx] = outV;
}
