/// @ref Microsoft 2020 "DirectX Raytracing 1.1", inline ray queries.
/// @ref Wright et al. 2022 "Lumen", final gather backed by a radiance cache.
/// Only compiled into the opt-in gather permutations. The legacy SDF shaders
/// contain no ray-query instructions and remain usable without DXR.
#ifndef UZ_DXR_DIFFUSE_GATHER_HLSLI
#define UZ_DXR_DIFFUSE_GATHER_HLSLI

#if defined(UZ_GI_HIT_PERMESH) && !defined(UZ_GI_HIT_SPECULAR)
#error "UZ_GI_HIT_PERMESH (r.GI.HitMaterial.PerMesh) is a superset of UZ_GI_HIT_SPECULAR: the per-mesh hit material shades through the hit-vertex lobe, so define both"
#endif

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
#ifdef UZ_GI_HIT_PERMESH
    // r.GI.HitMaterial.PerMesh (doc 08 section 5.2): today's values - the scene-constant roughness and a
    // dielectric - for the mode-2 control and the no-raw-attributes fallback; a table hit overwrites them.
    float hitRoughness = scene.hitSpecularRoughness;
    float hitMetallic = 0.0;
#endif
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
#ifdef UZ_GI_HIT_PERMESH
                // The mesh's own roughness / metallic at the triangle UV (the sibling table), floored by
                // r.GI.HitMaterial.PerMesh.MinRoughness (the variance floor, doc 08 section 7.1).
                float meshRoughness, meshMetallic;
                if (GITrace_HitRoughnessMetallic(query.CommittedInstanceID(), query.CommittedPrimitiveIndex(),
                                                 query.CommittedTriangleBarycentrics(), g_gatherMaterialSrv,
                                                 scene.hitMaterialExtSrv, meshRoughness, meshMetallic)) {
                    hitRoughness = max(meshRoughness, scene.hitMinRoughness);
                    hitMetallic = meshMetallic;
                }
#endif
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
#ifdef UZ_GI_HIT_SPECULAR
#ifdef UZ_GI_HIT_PERMESH
    // r.GI.HitMaterial.PerMesh (doc 08 section 6): the path tracer's EvaluatePBR structure at the hit -
    // F0 = lerp(0.04, baseColor, metallic), the diffuse x (1 - metallic), the sun lobe at the hit's own
    // roughness, and the probe-irradiance term coupled by the same LUT albedo
    // ((diffuseAlbedo * w + E_spec) * E/pi), so a metal in shadow reflects the probe irradiance instead
    // of going black. The SDF-material fallback and the mode-2 control reach this with the scene constant
    // and metallic 0 (F0 = 0.04, diffuseAlbedo = albedo): their only change is the probe-term coupling.
    const float3 hitF0 = ComputeF0(albedo, hitMetallic);
    const float3 diffuseAlbedo = albedo * (1.0 - hitMetallic);
    const GiHitMaterialSpecular hs = EvalGiHitMaterialSpecular(normal, -direction, scene.sunDirection, ndl,
                                                               hitRoughness, hitF0, scene.hitSpecularLutSrv,
                                                               UZ_SDF_REFLECTION_SAMPLER);
    const float3 sunShaded = diffuseAlbedo * (scene.sunColor * (ndl * visibility * M_1_PI)) * hs.diffuseWeight
                           + scene.sunColor * (visibility * hs.specularCos);
    const float3 probeShaded = (diffuseAlbedo * hs.diffuseWeight + hs.specularAlbedo)
                             * SdfReflectionIrradiance(scene, position, normal, hitT);
    return emission + sunShaded + probeShaded;
#else
    // r.GI.FinalGather.HitSpecular (doc 06 sections 4, 5.3): the sun Lambert weighted by the LUT's
    // (1 - E_spec) and the sun's GGX lobe toward the ray origin (V = -direction); `normal` is unit and
    // faces the ray. Emission and the probe-irradiance Lambert are unchanged.
    const float3 sunLambert = albedo * (scene.sunColor * (ndl * visibility * M_1_PI));
    const GiHitSunSpecular hs = EvalGiHitSunSpecular(normal, -direction, scene.sunDirection, ndl,
                                                     scene.hitSpecularRoughness, kGiHitSpecularF0,
                                                     scene.hitSpecularLutSrv, UZ_SDF_REFLECTION_SAMPLER);
    return emission + sunLambert * hs.diffuseWeight + scene.sunColor * (visibility * hs.specularCos)
        + albedo * SdfReflectionIrradiance(scene, position, normal, hitT);
#endif
#else
    return emission + albedo * (scene.sunColor * (ndl * visibility * M_1_PI)
        + SdfReflectionIrradiance(scene, position, normal, hitT));
#endif
}
#endif
