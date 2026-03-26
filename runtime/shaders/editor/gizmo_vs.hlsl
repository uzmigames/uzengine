/// Gizmo vertex shader — transforms gizmo vertices with constant screen-space scale.
///
/// Uses SV_VertexID to index into a structured buffer of GizmoVertex data.
/// The bindless root signature does not support IA input layouts, so all
/// geometry is accessed via descriptor heap (SM6.6) or register-bound SRV (SM5).
/// Passes per-vertex normals to pixel shader for N-dot-L diffuse shading.

cbuffer GizmoCB : register(b1) {
    row_major float4x4 viewProj;
    float3 gizmoWorldPos;
    float  screenScale;
    float3 axisColor;
    float  alpha;
    uint   vertexBufferSrv;  // SRV index for structured buffer of GizmoVertex
    uint   _pad0;
    uint   _pad1;
    uint   _pad2;
};

struct GizmoVertex {
    float3 position;
    float3 normal;
    float3 color;
};

#ifndef UZ_BINDLESS
StructuredBuffer<GizmoVertex> vertexBuffer : register(t0);
#endif

struct VSOutput {
    float4 position : SV_Position;
    float3 normal   : NORMAL;
    float3 color    : COLOR;
    float  alpha    : TEXCOORD0;
};

VSOutput main(uint vertexID : SV_VertexID) {
    VSOutput output;

#ifdef UZ_BINDLESS
    StructuredBuffer<GizmoVertex> vertexBuffer = ResourceDescriptorHeap[vertexBufferSrv];
#endif
    GizmoVertex v = vertexBuffer[vertexID];

    // Scale vertex to maintain constant screen size, then translate to world pos
    float3 worldPos = v.position * screenScale + gizmoWorldPos;
    output.position = mul(float4(worldPos, 1.0), viewProj);

    // Pass normal as-is (gizmo is axis-aligned in world space, no rotation needed)
    output.normal = v.normal;

    // Use per-vertex color if non-zero, else use axis color from CB
    float colorLen = dot(v.color, v.color);
    output.color = colorLen > 0.0001 ? v.color : axisColor;
    output.alpha = alpha;

    return output;
}
