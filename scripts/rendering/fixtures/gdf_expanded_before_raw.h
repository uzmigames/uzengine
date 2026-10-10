struct MeshSdf {
    float3 origin;
    float distanceScale;
    float3 extent;
    float surfaceBias;
    float4 worldToLocalX;
    float4 worldToLocalY;
    float4 worldToLocalZ;
    uint srvIndex;
    uint resolution;
    uint albedoSrv;
    uint metallicSrv;
    float3 baseColorTint;
    float metallicMultiplier;
    uint materialFlags;
    uint3 padding2;
};
float3 DiffuseReflectance(MeshSdf mesh) { return mesh.baseColorTint; }
float MeshDistance(MeshSdf mesh, float3 localPosition) {
    float3 relative = localPosition - mesh.origin;
    float3 sampleExtent = mesh.extent * (1.0 - 1.0 / mesh.resolution);
    float3 clampedPosition = clamp(relative, -sampleExtent, sampleExtent);
    float3 uvw = clampedPosition / (2.0 * mesh.extent) + 0.5;
    TestVolume volume{mesh.srvIndex};
    float distance = volume.SampleLevel(linearClamp, uvw, 0);

    // A clamped sample alone stays constant outside the volume. The SDF is
    // 1-Lipschitz, so subtracting the displacement gives a lower bound there.
    float displacement = length(relative - clampedPosition);
    distance -= displacement;
    if (any(abs(relative) > mesh.extent)) {
        float boundsDistance = length(max(abs(relative) - mesh.extent, 0.0));
        distance = max(distance, boundsDistance);
    }
    return (distance - mesh.surfaceBias) * mesh.distanceScale;
}
MeshSdf meshes[512];
void ProductionComposite(uint3 voxel) {
    if (any(voxel >= resolution)) return;
    ExpandedOutput output;
    float4 result = accumulate != 0 ? output[voxel] : float4(65504.0, 0.0, 0.0, 0.0);
    float3 worldPosition = clipmapCenter
        + ((float3(voxel) + 0.5) / resolution * 2.0 - 1.0) * clipmapExtent;

    for (uint i = 0; i < meshCount; ++i) {
        MeshSdf mesh = meshes[i];
        float4 position = float4(worldPosition, 1.0);
        float3 localPosition = float3(dot(mesh.worldToLocalX, position),
                                      dot(mesh.worldToLocalY, position),
                                      dot(mesh.worldToLocalZ, position));
        float3 outside = max(abs(localPosition - mesh.origin) - mesh.extent, 0.0);
        float globalBias = mesh.surfaceBias > 0.0 ? clipmapExtent / resolution : 0.0;
        float lowerBound = (length(outside) - mesh.surfaceBias) * mesh.distanceScale - globalBias;
        if (any(outside > 0.0) && lowerBound >= result.r) continue;
        float distance = MeshDistance(mesh, localPosition) - globalBias;
        if (distance < result.r) {
            result = float4(distance, DiffuseReflectance(mesh));
        }
    }
    result.r = clamp(result.r, -65504.0, 65504.0);
    output[voxel] = result;
}