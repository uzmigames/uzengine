/// @ref Wright 2015 "Dynamic Occlusion with Signed Distance Fields"
/// @ref Ericson 2005 "Real-Time Collision Detection" section 5.1.5
/// @ref Kramer 2023 "Real-Time Sparse Distance Fields for Games", slide 6: unsigned distance = abs(d).
/// @constants 4^3 threads match the CPU dispatch; voxel centers use offset 0.5;
/// IEEE binary16 maximum 65504 bounds the R16F output.
/// @constants Root resolution bit 31 selects unsigned distance; low 31 bits store the volume size.
/// @ref Pharr, Jakob & Humphreys 2023 "Physically Based Rendering", 4th ed., section 7.3.
/// @constants Triangle-count bit 31 enables BVH traversal; low 31 bits locate its nodes.
/// Triangles and preorder nodes share a 64-byte stride; leaves contain at most four triangles.

cbuffer BakeConstants : register(b0) {
    uint outputUav;
    uint trianglesSrv;
    uint triangleCount;
    uint resolutionAndFlags;
    float3 volumeMin;
    float voxelSize;
};

// Triangle/BvhNode layout and the nearest-triangle search, shared with probe_side_cs.hlsl.
#include "mesh_closest_triangle.hlsli"

[numthreads(4,4,4)]
void main(uint3 id : SV_DispatchThreadID) {
    uint resolution = resolutionAndFlags & 0x7fffffffu;
    bool twoSided = (resolutionAndFlags & 0x80000000u) != 0;
    if (any(id >= resolution)) return;
    StructuredBuffer<Triangle> triangles = ResourceDescriptorHeap[trianglesSrv];
    StructuredBuffer<BvhNode> nodes = ResourceDescriptorHeap[trianglesSrv];
    RWTexture3D<float> output = ResourceDescriptorHeap[outputUav];
    float3 p = volumeMin + (float3(id)+0.5)*voxelSize;
    float2 closest = ClosestMeshTriangle(p, triangles, nodes, triangleCount);
    float distance = min(sqrt(closest.x),65504.0);
    output[id] = twoSided ? distance : closest.y * distance;
}
