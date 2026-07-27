/// UzEngine Global SDF Composite — merge per-mesh SDFs into a clipmap cascade.
///
/// One thread per voxel of one 64³ R16F clipmap. Each voxel stores the
/// minimum world-space distance (cm) to any registered mesh surface:
///   - Transform the voxel's world position into each mesh's local space
///     (world→local 3x4 from the mesh entry).
///   - Inside the mesh's SDF volume: trilinear-sample the baked distance.
///   - Outside: the distance to the volume's AABB is a safe LOWER bound of
///     the distance to any surface inside it (sphere-trace conservative).
///   - Take the minimum across meshes, scaled back to world units.
///
/// Replaces the empty stub that left every clipmap zeroed — distance 0
/// everywhere reads as "surface at every point", which made every GDF
/// sphere-trace hit at the first step and silently killed SDF GI.
///
/// Bindings (matches GlobalDistanceField::CompositeFromMeshSDFs):
///   b0 root constants (8 DWORDs): outputUav, meshCount, clipmapIndex,
///                                 resolution, clipmapCenter.xyz, clipmapExtent
///   b2 cbuffer: GdfMeshSdfGPU meshes[256] (96 B each, CPU-packed)
///
/// @ref Wright/Crassin 2018 "Distance Field Ray Tracing" — global clipmap
///      composited from per-mesh SDF volumes
/// @ref Ericson 2005 "Real-Time Collision Detection" — point-AABB distance

cbuffer CompositeRootCB : register(b0) {
    uint  g_outputUav;       // UAV index of the target clipmap 3D texture.
    uint  g_meshCount;       // Number of mesh SDF entries.
    uint  g_clipmapIndex;    // Cascade index (0 = finest) — informational.
    uint  g_resolution;      // Voxels per axis (64).
    float g_clipCenterX;     // Clipmap center, world cm.
    float g_clipCenterY;
    float g_clipCenterZ;
    float g_clipExtent;      // Clipmap half-extent, world cm.
};

/// Mirrors GdfMeshSdfGPU (96 bytes = 6 × float4).
struct GdfMeshSdf {
    float4 originPad;   // xyz = local-space volume CENTER, w = pad
    float4 extentPad;   // xyz = local-space volume HALF-extent, w = pad
    float4 invRow0;     // world→local 3x4, row 0
    float4 invRow1;     // row 1
    float4 invRow2;     // row 2
    uint4  srvResPad;   // x = SDF SRV index, y = resolution, zw = pad
};

cbuffer MeshDataCB : register(b2) {
    GdfMeshSdf g_meshes[256];
};

static const float kFarDistance = 60000.0; // R16F-safe "no surface" value.

[numthreads(4, 4, 4)]
void main(uint3 DTid : SV_DispatchThreadID) {
    if (any(DTid >= g_resolution)) return;

    RWTexture3D<float> outClipmap = ResourceDescriptorHeap[g_outputUav];
    SamplerState linearClamp = SamplerDescriptorHeap[1];

    // Voxel center in world space. Clipmap spans [center-extent, center+extent]
    // — the same mapping SampleGdfClipmap uses on the read side.
    float3 clipCenter = float3(g_clipCenterX, g_clipCenterY, g_clipCenterZ);
    float3 uvw01      = (float3(DTid) + 0.5) / float(g_resolution);
    float3 worldPos   = clipCenter + (uvw01 * 2.0 - 1.0) * g_clipExtent;

    float minDist = kFarDistance;

    uint meshCount = min(g_meshCount, 256u);
    [loop]
    for (uint m = 0; m < meshCount; ++m) {
        GdfMeshSdf mesh = g_meshes[m];
        if (mesh.srvResPad.x == 0xFFFFFFFFu) continue;

        // World → mesh-local (3x4 rows, position transform).
        float4 wp = float4(worldPos, 1.0);
        float3 localPos = float3(dot(mesh.invRow0, wp),
                                 dot(mesh.invRow1, wp),
                                 dot(mesh.invRow2, wp));

        // Local distances scale back to world by the inverse of the
        // world→local row scale (assumes uniform scale — standard for
        // static level geometry).
        float invScale   = length(mesh.invRow0.xyz);
        float localToWorld = (invScale > 1e-6) ? (1.0 / invScale) : 1.0;

        float3 rel        = localPos - mesh.originPad.xyz;
        float3 halfExtent = mesh.extentPad.xyz;

        // Distance from the point to the volume AABB (0 when inside).
        float3 outside  = max(abs(rel) - halfExtent, 0.0);
        float  aabbDist = length(outside);

        float dWorld;
        if (aabbDist <= 0.0) {
            // Inside the SDF volume — trilinear sample the baked distance.
            Texture3D<float> sdfTex = ResourceDescriptorHeap[mesh.srvResPad.x];
            float3 uvw = saturate(rel / (2.0 * halfExtent) + 0.5);
            dWorld = sdfTex.SampleLevel(linearClamp, uvw, 0).r * localToWorld;
        } else {
            // Outside — AABB distance is a conservative lower bound of the
            // distance to any surface contained in the volume.
            dWorld = aabbDist * localToWorld;
        }

        minDist = min(minDist, dWorld);
    }

    outClipmap[DTid] = minDist;
}
