/// @ref Wright et al. 2022 "Lumen", surface final gather with world radiance cache fallback.
#ifndef UZ_SSGI_WORLD_GATHER_HLSLI
#define UZ_SSGI_WORLD_GATHER_HLSLI
#include "ssgi_surface.hlsli"
#include "../gi/sdf_diffuse_gather.hlsli"
/// Phase D stage 3: the including shader names its surface-cache binding (a root DWORD holding the
/// StructuredBuffer<SurfaceCacheParams> SRV) before this include; unnamed = no cache, the gather's
/// hit shading is unchanged.
#ifndef UZ_SSGI_SURFACE_CACHE_PARAMS_SRV
#define UZ_SSGI_SURFACE_CACHE_PARAMS_SRV 0xffffffffu
#endif
/// @constants Four clipmaps and the shared sample count; intensity is applied once to E/pi.
SdfReflectionScene BuildSurfaceScene() {
    SdfReflectionScene scene;
    [unroll] for (uint c=0;c<4;++c) scene.clipmaps[c] = sdfClipmaps[c];
    scene.clipmapSrv = sdfClipmapSrv;
    scene.rawClipmapSrv = sdfRawClipmapSrv;
    scene.skySrv = sdfSkySrv;
    scene.probeSrv = rcProbeSrv;
    scene.probesPerAxis = rcProbesPerAxis;
    scene.probeOrigin = rcOrigin;
    scene.probeExtent = rcExtent;
    scene.voxelSize = sdfVoxelSize;
    scene.traceDistance = sdfTraceDistance;
    scene.traceSteps = sdfTraceSteps;
    scene.sunDirection = normalize(lightDir.xyz);
    scene.sunColor = lightColor.xyz;
    scene.skyMultiplier = float3(skyLightColorR,skyLightColorG,skyLightColorB) * skyLightIntensity;
    scene.plainMean = 0u; // diffuse gather does not use the reflection estimator's weight
    scene.surfaceCacheParamsSrv = UZ_SSGI_SURFACE_CACHE_PARAMS_SRV;
    return scene;
}

float3 GatherSurfaceLighting(float3 position, float3 normal, float3 geometricNormal, uint2 pixel, uint frame) {
    return GatherSdfDiffuse(BuildSurfaceScene(),position,normal,geometricNormal,pixel,frame,
                            sdfDiffuseSamples,sdfSampleRotation) * rcIntensity;
}

/// The same gather, reporting what it sampled. Diagnostics only; the arithmetic is identical.
float3 GatherSurfaceLightingIdentified(float3 position, float3 normal, float3 geometricNormal,
                                       uint2 pixel, uint frame, out float3 origin, out uint seed,
                                       out float3 firstDirection, out bool resolved,
                                       out float4 rays[UZ_GATHER_MAX_REPORTED_RAYS],
                                       out uint reportedRays, out float2 rotationUsed,
                                       out float4 cascadeStats, out float4 clipmap0) {
    SdfReflectionScene scene = BuildSurfaceScene();
    clipmap0 = scene.clipmaps[0];
    return GatherSdfDiffuseIdentified(scene,position,normal,geometricNormal,pixel,frame,
                                      sdfDiffuseSamples,sdfSampleRotation,origin,seed,firstDirection,
                                      resolved,rays,reportedRays,rotationUsed,cascadeStats) * rcIntensity;
}

/// A probe of the same scene data placed out of the jitter's reach: `position` snapped to a fixed
/// world grid, traced along a fixed world axis. Neither its origin nor its direction moves when the
/// sub-pixel jitter does, so it separates content that alternates from selection that alternates.
float3 ProbeSurfaceLightingSnapped(float3 position, float cell, out float3 snapped, out bool hit) {
    SdfReflectionScene scene = BuildSurfaceScene();
    snapped = round(position / cell) * cell;
    float t = TraceSdfRay(scene,snapped,float3(0,1,0),scene.traceDistance,0u,float3(0,1,0));
    hit = t >= 0.0;
    return TraceSdfSurfaceRadiance(scene,snapped,float3(0,1,0),0u,float3(0,1,0)) * rcIntensity;
}

/// The probe volume read directly at a fixed position — the resource whose SRV alternates, sampled
/// without a trace in the way, so "does its content alternate" is answered rather than assumed.
float3 ProbeVolumeAtSnapped(float3 snapped, float3 normal) {
    return SdfReflectionIrradiance(BuildSurfaceScene(),snapped,normal);
}
#endif
