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

struct Triangle {
    float3 a; uint originalIndex;
    float3 b; float pad1;
    float3 c; float pad2;
    float3 normal; float pad3;
};

struct BvhNode {
    float3 boundsMin; uint escapeIndex;
    float3 boundsMax; uint firstTriangle;
    uint triangleCount;
    uint3 padding0;
    uint4 padding1;
};

float3 ClosestSegment(float3 p, float3 a, float3 b) {
    float3 edge = b - a;
    float lengthSquared = dot(edge, edge);
    return a + edge * (lengthSquared > 0.0 ? saturate(dot(p-a, edge)/lengthSquared) : 0.0);
}

float3 ClosestTriangle(float3 p, Triangle tri) {
    float3 ab = tri.b-tri.a;
    float3 ac = tri.c-tri.a;
    float3 ap = p-tri.a;
    float3 n = cross(ab, ac);
    if (dot(n, n) == 0.0) {
        float3 q0 = ClosestSegment(p, tri.a, tri.b);
        float3 q1 = ClosestSegment(p, tri.b, tri.c);
        float3 q2 = ClosestSegment(p, tri.c, tri.a);
        float3 q = dot(p-q0,p-q0) < dot(p-q1,p-q1) ? q0 : q1;
        return dot(p-q,p-q) < dot(p-q2,p-q2) ? q : q2;
    }
    float d1 = dot(ab,ap), d2 = dot(ac,ap);
    if (d1 <= 0.0 && d2 <= 0.0) return tri.a;
    float3 bp = p-tri.b;
    float d3 = dot(ab,bp), d4 = dot(ac,bp);
    if (d3 >= 0.0 && d4 <= d3) return tri.b;
    float vc = d1*d4-d3*d2;
    if (vc <= 0.0 && d1 >= 0.0 && d3 <= 0.0) return tri.a + ab*(d1/(d1-d3));
    float3 cp = p-tri.c;
    float d5 = dot(ab,cp), d6 = dot(ac,cp);
    if (d6 >= 0.0 && d5 <= d6) return tri.c;
    float vb = d5*d2-d1*d6;
    if (vb <= 0.0 && d2 >= 0.0 && d6 <= 0.0) return tri.a + ac*(d2/(d2-d6));
    float va = d3*d6-d5*d4;
    if (va <= 0.0 && d4-d3 >= 0.0 && d5-d6 >= 0.0)
        return tri.b + (tri.c-tri.b)*((d4-d3)/((d4-d3)+(d5-d6)));
    float invSum = 1.0/(va+vb+vc);
    return tri.a + ab*(vb*invSum) + ac*(vc*invSum);
}

void UpdateMeshClosest(float3 p, Triangle tri, uint originalIndex,
                       inout float closestSquared, inout float orientation, inout uint closestIndex) {
    float3 lower = min(tri.a, min(tri.b, tri.c));
    float3 upper = max(tri.a, max(tri.b, tri.c));
    float3 outside = max(max(lower-p, p-upper), 0.0);
    if (dot(outside,outside) > closestSquared) return;
    float3 delta = p-ClosestTriangle(p,tri);
    float distanceSquared = dot(delta,delta);
    if (distanceSquared < closestSquared ||
        (distanceSquared == closestSquared && closestIndex != 0xffffffffu && originalIndex < closestIndex)) {
        closestSquared = distanceSquared;
        closestIndex = originalIndex;
        orientation = dot(delta,tri.normal) < 0.0 ? -1.0 : 1.0;
    }
}

float2 ClosestMeshTriangle(float3 p, StructuredBuffer<Triangle> triangles,
                           StructuredBuffer<BvhNode> nodes, uint countAndFlags) {
    float closestSquared = 65504.0*65504.0;
    float orientation = 1.0;
    uint closestIndex = 0xffffffffu;
    uint count = countAndFlags & 0x7fffffffu;
    if ((countAndFlags & 0x80000000u) != 0 && count != 0) {
        uint seedIndex = 0;
        BvhNode seedNode = nodes[count];
        [loop] while (seedNode.triangleCount == 0) {
            uint leftIndex = seedIndex + 1;
            BvhNode left = nodes[count + leftIndex];
            uint rightIndex = left.escapeIndex;
            BvhNode right = nodes[count + rightIndex];
            float3 leftOutside = max(max(left.boundsMin-p, p-left.boundsMax), 0.0);
            float3 rightOutside = max(max(right.boundsMin-p, p-right.boundsMax), 0.0);
            bool chooseLeft = dot(leftOutside,leftOutside) <= dot(rightOutside,rightOutside);
            seedIndex = chooseLeft ? leftIndex : rightIndex;
            if (chooseLeft) seedNode = left;
            else seedNode = right;
        }
        [loop] for (uint i = 0; i < seedNode.triangleCount; ++i) {
            Triangle tri = triangles[seedNode.firstTriangle + i];
            UpdateMeshClosest(p, tri, tri.originalIndex, closestSquared, orientation, closestIndex);
        }
        uint nodeIndex = 0;
        uint nodeEnd = nodes[count].escapeIndex;
        [loop] while (nodeIndex < nodeEnd) {
            BvhNode node = nodes[count + nodeIndex];
            float3 outside = max(max(node.boundsMin-p, p-node.boundsMax), 0.0);
            if (nodeIndex == seedIndex || dot(outside,outside) > closestSquared) {
                nodeIndex = node.escapeIndex;
                continue;
            }
            if (node.triangleCount == 0) {
                ++nodeIndex;
                continue;
            }
            [loop] for (uint i = 0; i < node.triangleCount; ++i) {
                Triangle tri = triangles[node.firstTriangle + i];
                UpdateMeshClosest(p, tri, tri.originalIndex, closestSquared, orientation, closestIndex);
            }
            nodeIndex = node.escapeIndex;
        }
    } else {
        [loop] for (uint i = 0; i < count; ++i)
            UpdateMeshClosest(p, triangles[i], i, closestSquared, orientation, closestIndex);
    }
    return float2(closestSquared, orientation);
}

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
