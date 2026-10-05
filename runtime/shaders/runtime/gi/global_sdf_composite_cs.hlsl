/// @ref Wright 2015 "Dynamic Occlusion with Signed Distance Fields", slides 19, 50.
/// @constants 512 meshes * 128 bytes = 65536 bytes, the 64 KiB cbuffer limit.
/// @constants 65504 is the largest finite fp16 distance; 4^3 threads match CPU dispatch.
/// @constants 0.5 locates texel centers; 2 converts half-extents to volume dimensions.
/// @ref Kramer 2023 "Real-Time Sparse Distance Fields for Games", slide 6: two-sided unsigned fields.
/// @constants Half a mesh voxel plus half a global voxel provides finite surface
/// coverage after filtering unsigned distances; thin-wall accuracy is resolution-limited.
cbuffer RootConstants : register(b0) {
    uint outputUav;
    uint meshCount;
    uint accumulate;
    uint resolution;
    float3 clipmapCenter;
    float clipmapExtent;
    uint rawOutputUav;
    uint regionOrigin;   // first window-relative voxel of the region, 9 bits per axis (x | y << 9 | z << 18)
    uint regionSize;     // region size in voxels, 9 bits per axis
    uint windowOrigin;   // window origin mod resolution, 9 bits per axis: toroidal storage (block B.2)
};

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

cbuffer MeshConstants : register(b2) {
    MeshSdf meshes[512];
};
SamplerState linearClamp : register(s2);

float4 MaterialAverage(uint srv, float4 fallback) {
    if (srv == 0 || srv == 0xffffffffu) return fallback;
    Texture2D<float4> texture = ResourceDescriptorHeap[NonUniformResourceIndex(srv)];
    uint width, height, mipCount;
    texture.GetDimensions(0, width, height, mipCount);
    return texture.SampleLevel(linearClamp, float2(0.5, 0.5), mipCount - 1);
}

/// @ref Majercik 2019 "Dynamic Diffuse Global Illumination with Ray-Traced Irradiance Fields", section 5.2.
/// @constants Linear reflectance lies in [0,1]; metallic has no diffuse lobe.
/// Material flag bit 2 selects packed ORM with metallic in B, otherwise R.
float3 DiffuseReflectance(MeshSdf mesh) {
    float3 albedo = MaterialAverage(mesh.albedoSrv, 1.0).rgb;
    float4 metalSample = MaterialAverage(mesh.metallicSrv, 0.0);
    float metallic = (mesh.materialFlags & 4u) != 0 ? metalSample.b : metalSample.r;
    return saturate(albedo * mesh.baseColorTint)
         * (1.0 - saturate(metallic * mesh.metallicMultiplier));
}

float MeshDistance(MeshSdf mesh, float3 localPosition, out float rawDistance) {
    float3 relative = localPosition - mesh.origin;
    float3 sampleExtent = mesh.extent * (1.0 - 1.0 / mesh.resolution);
    float3 clampedPosition = clamp(relative, -sampleExtent, sampleExtent);
    float3 uvw = clampedPosition / (2.0 * mesh.extent) + 0.5;
    Texture3D<float> volume = ResourceDescriptorHeap[NonUniformResourceIndex(mesh.srvIndex)];
    float distance = volume.SampleLevel(linearClamp, uvw, 0);

    // A clamped sample alone stays constant outside the volume. The SDF is
    // 1-Lipschitz, so subtracting the displacement gives a lower bound there.
    float displacement = length(relative - clampedPosition);
    distance -= displacement;
    if (any(abs(relative) > mesh.extent)) {
        float boundsDistance = length(max(abs(relative) - mesh.extent, 0.0));
        distance = max(distance, boundsDistance);
    }
    rawDistance = distance * mesh.distanceScale;
    return (distance - mesh.surfaceBias) * mesh.distanceScale;
}

[numthreads(4, 4, 4)]
void main(uint3 thread : SV_DispatchThreadID) {
    uint3 regionO = uint3(regionOrigin & 0x1ffu, (regionOrigin >> 9) & 0x1ffu, (regionOrigin >> 18) & 0x1ffu);
    uint3 regionS = uint3(regionSize & 0x1ffu, (regionSize >> 9) & 0x1ffu, (regionSize >> 18) & 0x1ffu);
    uint3 windowO = uint3(windowOrigin & 0x1ffu, (windowOrigin >> 9) & 0x1ffu, (windowOrigin >> 18) & 0x1ffu);
    if (any(thread >= regionS)) return;
    uint3 voxel = regionO + thread;                 // window-relative voxel
    if (any(voxel >= resolution)) return;
    // Toroidal storage: world voxel k lives in texel k mod resolution, so a recentre only writes
    // the voxels that entered the window and the readers address by frac(p / window length).
    uint3 texel = (voxel + windowO) % resolution;
    RWTexture3D<float4> output = ResourceDescriptorHeap[outputUav];
    RWTexture3D<float> rawOutput = ResourceDescriptorHeap[rawOutputUav];
    float4 result = accumulate != 0 ? output[texel] : float4(65504.0, 0.0, 0.0, 0.0);
    float rawResult = accumulate != 0 ? rawOutput[texel] : 65504.0;
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
        float rawLowerBound = length(outside) * mesh.distanceScale;
        if (any(outside > 0.0) && lowerBound >= result.r && rawLowerBound >= rawResult) continue;
        float rawDistance;
        float distance = MeshDistance(mesh, localPosition, rawDistance) - globalBias;
        rawResult = min(rawResult, rawDistance);
        if (distance < result.r) {
            result = float4(distance, DiffuseReflectance(mesh));
        }
    }
    result.r = clamp(result.r, -65504.0, 65504.0);
    output[texel] = result;
    rawOutput[texel] = clamp(rawResult, -65504.0, 65504.0);
}
