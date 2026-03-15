/// Shadow depth vertex shader — depth-only pass for cascaded shadow maps.
///
/// Transforms vertex positions from object space to light clip space using
/// the per-object world matrix and a per-shadow-pass light view-projection.
///
/// No pixel shader is bound — the hardware writes depth automatically.
/// This shader is invoked once per shadow-casting mesh per cascade, so it
/// is kept minimal: position-only input, SV_Position-only output.
///
/// # Constant Buffers
/// - `PerObjectCB` (b2): world matrix (root param 2, same as GBuffer/PBR passes).
/// - `ShadowCB`    (b4): light view-projection for the current cascade (root param 3).
///
/// # Bindless Path (UZ_BINDLESS / SM6.6)
/// Fetches position from a StructuredBuffer via ResourceDescriptorHeap.
/// Only the first 12 bytes (float3 position) of each vertex are read;
/// the remaining attributes (normal, texcoord, tangent) are skipped.
///
/// # Instanced Path (UZ_INSTANCED, requires UZ_BINDLESS)
/// Reads the world matrix from StructuredBuffer<InstanceData>[SV_InstanceID]
/// instead of PerObjectCB. Used with DrawIndexedInstanced for batching.
///
/// # SM5 Path
/// Receives position through the Input Assembler (POSITION semantic).
/// The full vertex buffer is bound but only the position element is declared.

// ─────────────────────────────────────────────────────────────────────────────
// Constant Buffers
// ─────────────────────────────────────────────────────────────────────────────

/// Per-object transform — only the world matrix is used here.
/// Scalars first to match C++ PerObjectCB layout (std140 compatibility).
cbuffer PerObjectCB : register(b2) {
    uint     albedoIndex;    // unused in shadow pass
    uint     normalIndex;    // unused
    uint     roughnessIndex; // unused
    uint     metallicIndex;  // unused
    uint     aoIndex;        // unused
    float3   _pad;
    row_major float4x4 world;
    row_major float4x4 normalMatrix; // unused
};

/// Light view-projection matrix for the current shadow cascade.
cbuffer ShadowCB : register(b4) {
    row_major float4x4 shadowViewProj;
};

// ─────────────────────────────────────────────────────────────────────────────
// Bindless vertex fetch (SM6.6)
// ─────────────────────────────────────────────────────────────────────────────

#ifdef UZ_BINDLESS

/// Vertex layout matching the full mesh vertex buffer.
/// Only `position` is read; other fields exist for correct stride.
struct Vertex {
    float3 position;
    float3 normal;
    float2 texcoord;
    float4 tangent;
};

/// Root constants — descriptor indices for bindless resources.
struct PushConstants {
    uint vertexBufferIndex;
#ifdef UZ_INSTANCED
    uint instanceBufferIndex;   // SRV index for StructuredBuffer<InstanceData>
#endif
};
ConstantBuffer<PushConstants> push : register(b0);

#ifdef UZ_INSTANCED
/// Per-instance transform data. Must match C++ uz::InstanceData (128 bytes).
struct InstanceData {
    row_major float4x4 instWorld;
    row_major float4x4 instNormal; // unused in shadow pass
};
#endif

#if defined(UZ_INSTANCED)
float4 main(uint vertexID : SV_VertexID, uint instanceID : SV_InstanceID) : SV_Position {
    StructuredBuffer<Vertex> vertices = ResourceDescriptorHeap[push.vertexBufferIndex];
    float3 posOS = vertices[vertexID].position;

    StructuredBuffer<InstanceData> instances = ResourceDescriptorHeap[push.instanceBufferIndex];
    float4x4 instModel = instances[instanceID].instWorld;
#else
float4 main(uint vertexID : SV_VertexID) : SV_Position {
    StructuredBuffer<Vertex> vertices = ResourceDescriptorHeap[push.vertexBufferIndex];
    float3 posOS = vertices[vertexID].position;

    float4x4 instModel = world;
#endif

#else

// ─────────────────────────────────────────────────────────────────────────────
// Input Assembler path (SM5)
// ─────────────────────────────────────────────────────────────────────────────

struct VSInput {
    float3 position : POSITION;
};

float4 main(VSInput input) : SV_Position {
    float3 posOS = input.position;
    float4x4 instModel = world;

#endif

    // Object space → Light clip space (single combined multiply)
    float4 posWS = mul(float4(posOS, 1.0), instModel);
    return mul(posWS, shadowViewProj);
}
