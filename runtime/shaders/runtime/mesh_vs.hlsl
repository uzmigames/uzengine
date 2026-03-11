/// Standard mesh vertex shader — SM6.6 bindless vertex fetch.
///
/// Fetches vertex data from a StructuredBuffer via ResourceDescriptorHeap[index].
/// Transforms position by Model * ViewProj. Passes world-space normal,
/// texcoord, and tangent to the pixel shader for lighting.

struct VSOutput {
    float4 positionCS : SV_Position;
    float3 normalWS   : NORMAL;
    float2 texcoord   : TEXCOORD0;
    float4 tangentWS  : TANGENT;
    float3 positionWS : TEXCOORD1;
};

struct Vertex {
    float3 position;
    float3 normal;
    float2 texcoord;
    float4 tangent;
};

// Root constants at b0 — descriptor index for the vertex buffer SRV
struct PushConstants {
    uint vertexBufferIndex;
};
ConstantBuffer<PushConstants> push : register(b0);

// Per-frame CB at b1
cbuffer PerFrameCB : register(b1) {
    row_major float4x4 viewProj;
    float4   cameraPos;
    float4   timeParams;
};

// Per-object CB at b2
cbuffer PerObjectCB : register(b2) {
    row_major float4x4 model;
    row_major float4x4 normalMatrix;
    uint     materialIndex;
};

VSOutput main(uint vertexID : SV_VertexID) {
    // SM6.6 bindless vertex fetch
    StructuredBuffer<Vertex> vertices = ResourceDescriptorHeap[push.vertexBufferIndex];
    Vertex v = vertices[vertexID];

    VSOutput output;

    // Object → World
    float4 worldPos = mul(float4(v.position, 1.0), model);
    output.positionWS = worldPos.xyz;

    // World → Clip
    output.positionCS = mul(worldPos, viewProj);

    // Transform normal to world space (using normal matrix)
    output.normalWS = normalize(mul(float4(v.normal, 0.0), normalMatrix).xyz);

    // Transform tangent to world space
    output.tangentWS = float4(normalize(mul(float4(v.tangent.xyz, 0.0), model).xyz), v.tangent.w);

    output.texcoord = v.texcoord;

    return output;
}
