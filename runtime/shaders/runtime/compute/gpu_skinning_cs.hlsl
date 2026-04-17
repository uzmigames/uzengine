/// UzEngine GPU Skinning compute shader.
///
/// Transforms SkinnedVertex (56 B) inputs into standard Vertex (48 B) outputs
/// by applying a 4-bone linear blend skin using the per-entity bone palette.
///
/// # Layout
/// - Input  (SkinnedVertex, 56 B): pos(12) + norm(12) + uv(8) + tan(16) +
///   boneIndices(4 × uint8) + boneWeights(4 × uint8).
/// - Palette (float4x4 per bone): bone palette[i] = inverseBindWorld[i] * worldBone[i].
///   The palette buffer is sized for 256 bone slots (`kMaxBones` in
///   `skeletal_mesh_subsystem.cpp`); slots above the skeleton's bone count are
///   pre-seeded with identity so any out-of-range bone index a vertex might
///   carry maps to a no-op skin matrix.
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

/// SkinnedVertex layout (CPU-packed 56 B, see runtime/engine/renderer/mesh/vertex.h):
///   offset 0..11  : float3 position
///   offset 12..23 : float3 normal
///   offset 24..31 : float2 texcoord
///   offset 32..47 : float4 tangent
///   offset 48..51 : uint8[4] boneIndices  (packed into a single uint)
///   offset 52..55 : uint8[4] boneWeights  (packed into a single uint)
///
/// HLSL `StructuredBuffer<T>` applies D3D12 vector-packing rules (a 16-byte
/// boundary cannot be straddled), which silently injects padding between a
/// `float3` and a following `float3`/`float2`. That makes the runtime GPU
/// stride 64 B while the CPU struct is 56 B — every second vertex reads
/// garbage. Using `ByteAddressBuffer` with explicit offsets pins the layout
/// to the CPU one.
static const uint kSkinnedStride    = 56u;
static const uint kOffPosition      = 0u;
static const uint kOffNormal        = 12u;
static const uint kOffTexcoord      = 24u;
static const uint kOffTangent       = 32u;
static const uint kOffBoneIndices   = 48u;
static const uint kOffBoneWeights   = 52u;

/// Vertex output layout (matches CPU `struct Vertex` — 48 B). The output
/// UAV is still a structured buffer (same SRV is reused by the GBuffer VS
/// as `StructuredBuffer<Vertex>`), so we write through `RWStructuredBuffer`.
struct Vertex {
    float3 position;
    float3 normal;
    float2 texcoord;
    float4 tangent;
};

struct PushConstants {
    uint inputBufferIdx;   ///< ByteAddressBuffer bindless index (SkinnedVertex[] input, 56 B raw).
    uint paletteBufferIdx; ///< ByteAddressBuffer bindless index (Mat4[] palette, 64 B raw).
    uint outputBufferIdx;  ///< RWStructuredBuffer<Vertex> bindless index (48 B stride).
    uint vertexCount;      ///< Total vertices in the input buffer.
};
ConstantBuffer<PushConstants> push : register(b0);

/// Loads a single bone palette entry from a raw byte-address buffer.
/// CPU `Mat4` is stored row-major (`XMFLOAT4X4`), 64 B per matrix:
///   floats[0..3]   = row 0
///   floats[4..7]   = row 1
///   floats[8..11]  = row 2
///   floats[12..15] = row 3   (translation for row-vector matrices)
///
/// HLSL `float4x4(r0, r1, r2, r3)` initialises the matrix by rows, so the
/// loaded matrix has `M[i] = rowi` — i.e., `M[i][j] = rowi[j] = CPU Mat4(i,j)`.
/// This matches the CPU layout element-for-element without any transposition.
float4x4 LoadPaletteRowMajor(ByteAddressBuffer buf, uint boneIndex) {
    const uint base = boneIndex * 64u;
    const float4 r0 = asfloat(buf.Load4(base +  0u));
    const float4 r1 = asfloat(buf.Load4(base + 16u));
    const float4 r2 = asfloat(buf.Load4(base + 32u));
    const float4 r3 = asfloat(buf.Load4(base + 48u));
    return float4x4(r0, r1, r2, r3);
}

[numthreads(64, 1, 1)]
void main(uint3 DTid : SV_DispatchThreadID) {
    const uint vidx = DTid.x;
    if (vidx >= push.vertexCount) return;

    ByteAddressBuffer           inputBytes   = ResourceDescriptorHeap[push.inputBufferIdx];
    ByteAddressBuffer           paletteBytes = ResourceDescriptorHeap[push.paletteBufferIdx];
    RWStructuredBuffer<Vertex>  outputVerts  = ResourceDescriptorHeap[push.outputBufferIdx];

    const uint base = vidx * kSkinnedStride;
    const float3 position     = asfloat(inputBytes.Load3(base + kOffPosition));
    const float3 normal       = asfloat(inputBytes.Load3(base + kOffNormal));
    const float2 texcoord     = asfloat(inputBytes.Load2(base + kOffTexcoord));
    const float4 tangent      = asfloat(inputBytes.Load4(base + kOffTangent));
    const uint   boneIndices  = inputBytes.Load(base + kOffBoneIndices);
    const uint   boneWeights  = inputBytes.Load(base + kOffBoneWeights);

    // Unpack 4-bone indices (uint8 × 4 packed into uint).
    const uint idx0 = (boneIndices >>  0) & 0xFFu;
    const uint idx1 = (boneIndices >>  8) & 0xFFu;
    const uint idx2 = (boneIndices >> 16) & 0xFFu;
    const uint idx3 = (boneIndices >> 24) & 0xFFu;

    // Unpack 4-bone weights and normalize from [0,255] → [0,1].
    const float inv255 = 1.0 / 255.0;
    const float w0 = float((boneWeights >>  0) & 0xFFu) * inv255;
    const float w1 = float((boneWeights >>  8) & 0xFFu) * inv255;
    const float w2 = float((boneWeights >> 16) & 0xFFu) * inv255;
    const float w3 = float((boneWeights >> 24) & 0xFFu) * inv255;

    // Weighted sum of bone skinning matrices. Palettes are loaded as
    // row-major from a raw byte-address buffer, matching CPU `Mat4`
    // storage. Matrix layout M[i][j] = CPU Mat4(i, j).
    const float4x4 P0 = LoadPaletteRowMajor(paletteBytes, idx0);
    const float4x4 P1 = LoadPaletteRowMajor(paletteBytes, idx1);
    const float4x4 P2 = LoadPaletteRowMajor(paletteBytes, idx2);
    const float4x4 P3 = LoadPaletteRowMajor(paletteBytes, idx3);
    const float4x4 skin = P0 * w0 + P1 * w1 + P2 * w2 + P3 * w3;

    // Row-vector convention (matches mesh_vs / CPU palette math):
    // `result = v * M` ⇔ `result[i] = sum_j v[j] * M[j][i]`.
    const float4 posS = mul(float4(position, 1.0), skin);
    const float3 nS   = mul(float4(normal,   0.0), skin).xyz;
    const float3 tS   = mul(float4(tangent.xyz, 0.0), skin).xyz;

    Vertex outV;
    outV.position = posS.xyz;
    outV.normal   = normalize(nS);
    outV.tangent  = float4(normalize(tS), tangent.w);
    outV.texcoord = texcoord;
    outputVerts[vidx] = outV;
}
