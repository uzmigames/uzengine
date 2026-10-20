/// UzEngine mesh vertex shader.
/// @ref Standard vertex transform — row-vector * matrix convention (row_major)

struct VSOutput {
    float4 positionCS : SV_Position;
    float3 normalWS   : NORMAL;
    float2 texcoord   : TEXCOORD0;
    float4 tangentWS  : TANGENT;
    float3 positionWS : TEXCOORD1;
    noperspective float2 screenUV : TEXCOORD2;
    float4 color : COLOR0;
};

#ifdef UZ_BINDLESS
struct Vertex { float3 position; float3 normal; float2 texcoord; float4 tangent; };
// CPU int8_t[4] fields occupy one DWORD each, not an HLSL int4 (16 bytes).
struct VertexPacked { float3 position; uint packedNormal; float2 texcoord; uint packedTangent; uint _pad; };
int4 UnpackSignedBytes(uint packed) {
    return int4(int(packed << 24) >> 24, int(packed << 16) >> 24,
                int(packed << 8) >> 24, int(packed) >> 24);
}
// Draw callers initialize all four mesh values; unused root constants are
// not initialized by D3D12. The shared root block remains 24 DWORDs.
struct PushConstants { uint vertexBufferIndex; uint usePackedNormals; uint colorBufferIndex; uint previousVertexBufferIndex; };
ConstantBuffer<PushConstants> push : register(b0);
#else
struct VSInput {
    float3 position : POSITION;
    float3 normal   : NORMAL;
    float2 texcoord : TEXCOORD0;
    float4 tangent  : TANGENT;
};
#endif

cbuffer PerFrameCB : register(b1) {
    row_major float4x4 viewProj;
};

cbuffer PerObjectCB : register(b2) {
    uint  _t0, _t1, _t2, _t3, _t4;
    float3 _pad;
    row_major float4x4 model;
    row_major float4x4 normalMatrix;
};

#ifdef UZ_BINDLESS
VSOutput main(uint vertexID : SV_VertexID) {
    float3 vPosition;
    float3 vNormal;
    float2 vTexcoord;
    float4 vTangent;

    if (push.usePackedNormals != 0) {
        // Packed vertex format (32 bytes) — decode int8 normals/tangents
        StructuredBuffer<VertexPacked> packedVerts = ResourceDescriptorHeap[push.vertexBufferIndex];
        VertexPacked pv = packedVerts[vertexID];
        vPosition = pv.position;
        int4 normal = UnpackSignedBytes(pv.packedNormal);
        int4 tangent = UnpackSignedBytes(pv.packedTangent);
        vNormal   = float3(normal.xyz) / 127.0;
        vTexcoord = pv.texcoord;
        vTangent  = float4(float3(tangent.xyz) / 127.0, (tangent.w >= 0) ? 1.0 : -1.0);
    } else {
        // Standard vertex format (48 bytes)
        StructuredBuffer<Vertex> vertices = ResourceDescriptorHeap[push.vertexBufferIndex];
        Vertex v = vertices[vertexID];
        vPosition = v.position;
        vNormal   = v.normal;
        vTexcoord = v.texcoord;
        vTangent  = v.tangent;
    }

    float4x4 instModel  = model;
    float4x4 instNormal = normalMatrix;
#else
VSOutput main(VSInput v) {
    float3 vPosition = v.position;
    float3 vNormal   = v.normal;
    float2 vTexcoord = v.texcoord;
    float4 vTangent  = v.tangent;
    float4x4 instModel  = model;
    float4x4 instNormal = normalMatrix;
#endif

    VSOutput output;
    float4 worldPos = mul(float4(vPosition, 1.0), instModel);
    output.positionWS = worldPos.xyz;
    output.positionCS = mul(worldPos, viewProj);
    // Render-viewport UV, +y down, including the current projection jitter.
    output.screenUV = (output.positionCS.xy / output.positionCS.w) * float2(0.5, -0.5) + 0.5;
    output.color = float4(1, 1, 1, 1); // SM5 and absent streams use white.
#ifdef UZ_BINDLESS
    if (push.colorBufferIndex != 0) {
        StructuredBuffer<uint> colors = ResourceDescriptorHeap[push.colorBufferIndex];
        uint rgba = colors[vertexID];
        output.color = float4(rgba & 255u, (rgba >> 8) & 255u,
                              (rgba >> 16) & 255u, rgba >> 24) / 255.0;
    }
#endif
    output.normalWS   = normalize(mul(float4(vNormal, 0.0), instNormal).xyz);
    output.tangentWS  = float4(normalize(mul(float4(vTangent.xyz, 0.0), instNormal).xyz), vTangent.w);
    output.texcoord   = vTexcoord;
    return output;
}
