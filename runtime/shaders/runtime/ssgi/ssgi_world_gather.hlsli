/// @ref Wright et al. 2022 "Lumen", surface final gather with world radiance cache fallback.
#ifndef UZ_SSGI_WORLD_GATHER_HLSLI
#define UZ_SSGI_WORLD_GATHER_HLSLI
#include "ssgi_surface.hlsli"
#include "../gi/sdf_diffuse_gather.hlsli"
/// @constants Four clipmaps and the shared sample count; intensity is applied once to E/pi.
float3 GatherSurfaceLighting(float3 position, float3 normal, float3 geometricNormal, uint2 pixel, uint frame) {
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
    return GatherSdfDiffuse(scene,position,normal,geometricNormal,pixel,frame,sdfDiffuseSamples) * rcIntensity;
}
#endif
