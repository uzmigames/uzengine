/// UzEngine Mesh SDF Bake — per-mesh unsigned distance field (compute).
///
/// One thread per voxel of the mesh's local-space R16F volume. Each voxel
/// stores the distance (cm, mesh-local units) to the closest point on the
/// mesh surface, computed brute-force over all triangles. Unsigned distance
/// is sufficient for the GDF consumers: the sphere-trace treats
/// `d < epsilon` as a surface hit and never needs inside/outside sign.
///
/// Replaces the empty stub that shipped with the original GI scaffolding —
/// the stub left every mesh SDF volume zeroed, which reads as "surface at
/// every point in space" and silently killed the whole SDF GI chain
/// (GDF composite → radiance cache trace).
///
/// Bindings (matches MeshSdfBaker::BakeMeshGPU):
///   b0 root constants (8 DWORDs): outputUav, trianglesSrv, triangleCount,
///                                 resolution, volumeMin.xyz, voxelSize
///   Triangle data: bindless StructuredBuffer<GPUTriangle> (64 B stride).
///
/// @ref Ericson 2005 "Real-Time Collision Detection" §5.1.5 — closest point
///      on triangle
/// @ref Wright/Crassin 2018 "Distance Field Ray Tracing" — per-mesh SDF
///      volumes as the software-RT primitive

cbuffer BakeRootCB : register(b0) {
    uint  g_outputUav;      // UAV index of the R16F 3D volume.
    uint  g_trianglesSrv;   // SRV index of the triangle StructuredBuffer.
    uint  g_triangleCount;  // Number of triangles.
    uint  g_resolution;     // Voxels per axis.
    float g_volumeMinX;     // Local-space minimum corner of the volume.
    float g_volumeMinY;
    float g_volumeMinZ;
    float g_voxelSize;      // Voxel edge length (cm).
};

/// Mirrors MeshSdfBaker's GPUTriangle (4 × float4 = 64 bytes).
struct GPUTriangle {
    float3 v0; float _pad0;
    float3 v1; float _pad1;
    float3 v2; float _pad2;
    float3 n;  float _pad3;
};

/// Distance from point `p` to triangle (a, b, c).
/// @ref Ericson 2005 §5.1.5 — Voronoi-region closest point, branch form.
float DistancePointTriangle(float3 p, float3 a, float3 b, float3 c) {
    float3 ab = b - a;
    float3 ac = c - a;
    float3 ap = p - a;

    float d1 = dot(ab, ap);
    float d2 = dot(ac, ap);
    if (d1 <= 0.0 && d2 <= 0.0) return length(p - a);      // vertex A

    float3 bp = p - b;
    float d3 = dot(ab, bp);
    float d4 = dot(ac, bp);
    if (d3 >= 0.0 && d4 <= d3) return length(p - b);       // vertex B

    float vc = d1 * d4 - d3 * d2;
    if (vc <= 0.0 && d1 >= 0.0 && d3 <= 0.0) {             // edge AB
        float v = d1 / (d1 - d3);
        return length(p - (a + v * ab));
    }

    float3 cp = p - c;
    float d5 = dot(ab, cp);
    float d6 = dot(ac, cp);
    if (d6 >= 0.0 && d5 <= d6) return length(p - c);       // vertex C

    float vb = d5 * d2 - d1 * d6;
    if (vb <= 0.0 && d2 >= 0.0 && d6 <= 0.0) {             // edge AC
        float w = d2 / (d2 - d6);
        return length(p - (a + w * ac));
    }

    float va = d3 * d6 - d5 * d4;
    if (va <= 0.0 && (d4 - d3) >= 0.0 && (d5 - d6) >= 0.0) { // edge BC
        float w = (d4 - d3) / ((d4 - d3) + (d5 - d6));
        return length(p - (b + w * (c - b)));
    }

    // Interior — project onto the triangle plane.
    float denom = 1.0 / (va + vb + vc);
    float v = vb * denom;
    float w = vc * denom;
    return length(p - (a + ab * v + ac * w));
}

[numthreads(4, 4, 4)]
void main(uint3 DTid : SV_DispatchThreadID) {
    if (any(DTid >= g_resolution)) return;

    RWTexture3D<float> outVolume = ResourceDescriptorHeap[g_outputUav];
    StructuredBuffer<GPUTriangle> tris = ResourceDescriptorHeap[g_trianglesSrv];

    float3 volumeMin = float3(g_volumeMinX, g_volumeMinY, g_volumeMinZ);
    float3 p = volumeMin + (float3(DTid) + 0.5) * g_voxelSize;

    // Brute-force minimum over all triangles. One-time bake per mesh —
    // 32^3 voxels x a few thousand triangles is milliseconds on GPU.
    float minDist = 1e9;
    [loop]
    for (uint t = 0; t < g_triangleCount; ++t) {
        GPUTriangle tri = tris[t];
        minDist = min(minDist, DistancePointTriangle(p, tri.v0, tri.v1, tri.v2));
    }

    // R16F max is 65504 — clamp so empty volumes stay finite.
    outVolume[DTid] = min(minDist, 60000.0);
}
