/// @ref Microsoft 2020 "DirectX Raytracing 1.1", inline ray queries.
/// @ref Wright et al. 2022 "Lumen", final gather backed by a radiance cache.
/// Only compiled into the opt-in gather permutations. The legacy SDF shaders
/// contain no ray-query instructions and remain usable without DXR.
#ifndef UZ_DXR_DIFFUSE_GATHER_HLSLI
#define UZ_DXR_DIFFUSE_GATHER_HLSLI

static const uint kGITraceSentinelSrv = 0xffffffffu;
#define linearClamp UZ_SDF_REFLECTION_SAMPLER
#include "gi_hit_material.hlsli"
#undef linearClamp

/// @constants Centimetres: 0.05 surface offset and 0.01 minimum distance.
/// These are numerical self-intersection tolerances, not voxel expansion.
static const float kGatherDxrBias = 0.05;
static const float kGatherDxrTMin = 0.01;

float GatherDxrSunVisibility(SdfReflectionScene scene, float3 position, float3 normal) {
    RaytracingAccelerationStructure tlas = ResourceDescriptorHeap[g_gatherTlasSrv];
    RayDesc ray;
    ray.Origin = position + normal * kGatherDxrBias;
    ray.Direction = scene.sunDirection;
    ray.TMin = kGatherDxrTMin;
    ray.TMax = scene.traceDistance;
    RayQuery<RAY_FLAG_FORCE_OPAQUE | RAY_FLAG_SKIP_PROCEDURAL_PRIMITIVES |
             RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH> query;
    query.TraceRayInline(tlas, RAY_FLAG_NONE, 0xff, ray);
    query.Proceed();
    return query.CommittedStatus() == COMMITTED_TRIANGLE_HIT ? 0.0 : 1.0;
}

float3 TraceDxrGatherRadiance(SdfReflectionScene scene, float3 origin, float3 direction,
                             float3 geometricNormal, out float hitT) {
    hitT = -2.0;
    if (dot(geometricNormal, direction) <= 0.0) return 0.0;
    float3 position, normal, albedo, emission = 0.0;
    if (g_gatherDxrMode == 2u) {
        // Control: retain the SDF hit search and material, replace only its
        // sun visibility. An expanded hit is not an exact triangle position.
        hitT = TraceSdfRay(scene, origin, direction, scene.traceDistance, 0u, geometricNormal);
        if (hitT >= 0.0) {
            position = origin + direction * hitT;
            normal = SdfReflectionNormal(scene, position);
            if (dot(normal, -direction) <= 0.0) return 0.0;
            albedo = saturate(SampleSdfReflectionSurface(scene, position).gba);
        }
    } else {
        RaytracingAccelerationStructure tlas = ResourceDescriptorHeap[g_gatherTlasSrv];
        RayDesc ray;
        ray.Origin = origin + geometricNormal * kGatherDxrBias;
        ray.Direction = direction;
        ray.TMin = kGatherDxrTMin;
        ray.TMax = scene.traceDistance;
        RayQuery<RAY_FLAG_FORCE_OPAQUE | RAY_FLAG_SKIP_PROCEDURAL_PRIMITIVES> query;
        query.TraceRayInline(tlas, RAY_FLAG_NONE, 0xff, ray);
        query.Proceed();
        hitT = -1.0;
        if (query.CommittedStatus() == COMMITTED_TRIANGLE_HIT) {
            hitT = query.CommittedRayT();
            position = ray.Origin + direction * hitT;
            float3 objectNormal;
            if (!GITrace_HitSurface(query.CommittedInstanceID(), query.CommittedPrimitiveIndex(),
                                   query.CommittedTriangleBarycentrics(), g_gatherMaterialSrv,
                                   albedo, objectNormal)) {
                // Geometry without raw attributes still blocks the ray. Never
                // turn an unshadeable hit into an escaping sky ray.
                normal = SdfReflectionNormal(scene, position);
                albedo = saturate(SampleSdfReflectionSurface(scene, position).gba);
            } else {
                normal = mul(objectNormal, (float3x3)query.CommittedWorldToObject3x4());
            }
            if (dot(normal, normal) <= 1e-12) return 0.0;
            normal = normalize(normal);
            if (dot(normal, direction) > 0.0) normal = -normal;
            emission = GITrace_HitEmissive(query.CommittedInstanceID(), query.CommittedPrimitiveIndex(),
                                          query.CommittedTriangleBarycentrics(), g_gatherMaterialSrv);
        }
    }
    if (hitT < 0.0) {
        if (hitT == -1.0 && scene.skySrv != 0xffffffffu) {
            TextureCube<float4> sky = ResourceDescriptorHeap[scene.skySrv];
            return sky.SampleLevel(UZ_SDF_REFLECTION_SAMPLER,
                float3(direction.x, direction.z, direction.y), 0).rgb * scene.skyMultiplier;
        }
        return 0.0;
    }
    float ndl = saturate(dot(normal, scene.sunDirection));
    float visibility = ndl > 0.0 && any(scene.sunColor > 0.0)
        ? GatherDxrSunVisibility(scene, position, normal) : 0.0;
    return emission + albedo * (scene.sunColor * (ndl * visibility * M_1_PI)
        + SdfReflectionIrradiance(scene, position, normal));
}
#endif
