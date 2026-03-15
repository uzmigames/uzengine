/// Standard mesh vertex shader.
///
/// SM6.6 (UZ_BINDLESS): fetches vertex data from StructuredBuffer via
/// ResourceDescriptorHeap[index]. No Input Assembler layout needed.
///
/// SM5 (!UZ_BINDLESS): receives vertex data via traditional Input Assembler.
///
/// UZ_INSTANCED (requires UZ_BINDLESS): reads per-instance world/normal
/// matrices from a StructuredBuffer instead of PerObjectCB. Used with
/// DrawIndexedInstanced to batch identical mesh+material draw calls.
///
/// Transforms position by Model * ViewProj. Passes world-space normal,
/// texcoord, and tangent to the pixel shader for lighting.

struct VSOutput {
    float4 positionCS : SV_Position;
    float3 normalWS   : NORMAL;
    float2 texcoord   : TEXCOORD0;
    float4 tangentWS  : TANGENT;
    float3 positionWS : TEXCOORD1;
};

#ifdef UZ_BINDLESS
struct Vertex {
    float3 position;
    float3 normal;
    float2 texcoord;
    float4 tangent;
};

// Root constants at b0 — descriptor indices for bindless resources
struct PushConstants {
    uint vertexBufferIndex;
#ifdef UZ_INSTANCED
    uint instanceBufferIndex;   // SRV index for StructuredBuffer<InstanceData>
#endif
};
ConstantBuffer<PushConstants> push : register(b0);

#ifdef UZ_INSTANCED
/// Per-instance transform data read from a GPU StructuredBuffer.
/// Must match C++ uz::InstanceData layout exactly (128 bytes).
struct InstanceData {
    row_major float4x4 world;
    row_major float4x4 normalMtx;
};
#endif

#else
struct VSInput {
    float3 position : POSITION;
    float3 normal   : NORMAL;
    float2 texcoord : TEXCOORD0;
    float4 tangent  : TANGENT;
};
#endif

// Per-frame CB at b1 — must match C++ PerFrameCB struct exactly
cbuffer PerFrameCB : register(b1) {
    row_major float4x4 viewProj;
    float4   cameraPos;     // xyz = world position, w = unused
    float4   timeParams;    // x = totalTime, y = deltaTime, z = frameCount, w = unused
    float4   lightDir;      // xyz = direction TOWARD the light (normalized), w = unused
    float4   lightColor;    // xyz = color * intensity (radiance), w = unused
    float4   ambientColor;  // xyz = ambient radiance, w = unused
};

// Per-object CB at b2 — scalars first to avoid SPIRV std140 offset corruption
cbuffer PerObjectCB : register(b2) {
    uint     albedoIndex;
    uint     normalIndex;
    uint     roughnessIndex;
    uint     metallicIndex;
    uint     aoIndex;
    float3   _pad;
    row_major float4x4 model;
    row_major float4x4 normalMatrix;
};

#if defined(UZ_BINDLESS) && defined(UZ_INSTANCED)
VSOutput main(uint vertexID : SV_VertexID, uint instanceID : SV_InstanceID) {
    StructuredBuffer<Vertex> vertices = ResourceDescriptorHeap[push.vertexBufferIndex];
    Vertex v = vertices[vertexID];

    // Read per-instance transforms from the instance buffer
    StructuredBuffer<InstanceData> instances = ResourceDescriptorHeap[push.instanceBufferIndex];
    InstanceData inst = instances[instanceID];
    float4x4 instModel  = inst.world;
    float4x4 instNormal = inst.normalMtx;
#elif defined(UZ_BINDLESS)
VSOutput main(uint vertexID : SV_VertexID) {
    StructuredBuffer<Vertex> vertices = ResourceDescriptorHeap[push.vertexBufferIndex];
    Vertex v = vertices[vertexID];

    float4x4 instModel  = model;
    float4x4 instNormal = normalMatrix;
#else
VSOutput main(VSInput v) {
    float4x4 instModel  = model;
    float4x4 instNormal = normalMatrix;
#endif

    VSOutput output;

    // Object → World
    float4 worldPos = mul(float4(v.position, 1.0), instModel);
    output.positionWS = worldPos.xyz;

    // World → Clip
    output.positionCS = mul(worldPos, viewProj);

    // Transform normal to world space (using normal matrix)
    output.normalWS = normalize(mul(float4(v.normal, 0.0), instNormal).xyz);

    // Transform tangent to world space (normalMatrix for correct non-uniform scale)
    output.tangentWS = float4(normalize(mul(float4(v.tangent.xyz, 0.0), instNormal).xyz), v.tangent.w);

    output.texcoord = v.texcoord;

    return output;
}
