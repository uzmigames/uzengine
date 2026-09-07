/// @ref Wright 2015 "Dynamic Occlusion with Signed Distance Fields"
/// @ref Majercik 2019 "Dynamic Diffuse Global Illumination with Ray-Traced Irradiance Fields"
#ifndef UZ_SDF_REFLECTIONS_HLSLI
#define UZ_SDF_REFLECTIONS_HLSLI

#include "../common/sampling.hlsli"
#include "../lighting/ibl.hlsli"

#ifndef UZ_SDF_REFLECTION_SAMPLER
SamplerState sdfReflectionSampler : register(s2);
#define UZ_SDF_REFLECTION_SAMPLER sdfReflectionSampler
#endif

struct SdfReflectionScene {
    float4 clipmaps[4];
    uint4 clipmapSrv;
    uint4 rawClipmapSrv;
    uint skySrv;
    uint probeSrv;
    uint probesPerAxis;
    uint traceSteps;
    float3 probeOrigin;
    float probeExtent;
    float voxelSize;
    float traceDistance;
    float3 sunDirection;
    float3 sunColor;
    float3 skyMultiplier;
};

/// @constants Four clipmaps; 0xffffffff invalid SRV; 65504 is the largest finite half.
/// Normalized coordinates [-1,1] map to [0,1]; mip zero stores the distance field.
float4 SampleSdfReflectionSurface(SdfReflectionScene scene, float3 position) {
    [unroll] for (uint cascade = 0; cascade < 4; ++cascade) {
        float4 clipmap = scene.clipmaps[cascade];
        if (scene.clipmapSrv[cascade] == 0xffffffffu || clipmap.w <= 0.0) continue;
        float3 local = (position - clipmap.xyz) / clipmap.w;
        if (any(abs(local) > 1.0)) continue;
        Texture3D<float4> volume = ResourceDescriptorHeap[scene.clipmapSrv[cascade]];
        return volume.SampleLevel(UZ_SDF_REFLECTION_SAMPLER, local * 0.5 + 0.5, 0);
    }
    return float4(65504.0, 0.0, 0.0, 0.0);
}

/// @ref Hart 1996 "Sphere Tracing: A Geometric Method for the Antialiased Ray Tracing of Implicit Surfaces"
/// @constants 2 cm minimum tolerance, 5% voxel tolerance, half-tolerance minimum step.
/// Negative returns distinguish an escaped ray (-1) from exhausted work (-2).
float SdfReflectionEpsilon(SdfReflectionScene scene) {
    return max(2.0, scene.voxelSize * 0.05);
}

/// @ref Hart 1996 "Sphere Tracing: A Geometric Method for the Antialiased Ray Tracing of Implicit Surfaces"
/// @ref Wright et al. 2022 "Lumen", SDF Tracing Surface Bias.
/// Raster receivers remain at their geometric surface; expansion belongs to the ray trace.
bool ResolveSdfRayOrigin(SdfReflectionScene scene, float3 position, float3 normal, out float3 origin) {
    origin = position;
    return all(isfinite(position)) && all(isfinite(normal)) && dot(normal, normal) > 0.0;
}

/// @constants x=unexpanded distance, y=expanded distance; missing raw coverage is unknown.
float2 SampleSdfRayDistances(SdfReflectionScene scene, float3 position) {
    [unroll] for (uint cascade = 0; cascade < 4; ++cascade) {
        float4 clipmap = scene.clipmaps[cascade];
        if (scene.clipmapSrv[cascade] == 0xffffffffu || clipmap.w <= 0.0) continue;
        float3 local = (position - clipmap.xyz) / clipmap.w;
        if (any(abs(local) > 1.0)) continue;
        if (scene.rawClipmapSrv[cascade] == 0xffffffffu) return float2(-65504.0,-65504.0);
        Texture3D<float> rawVolume = ResourceDescriptorHeap[scene.rawClipmapSrv[cascade]];
        Texture3D<float4> expandedVolume = ResourceDescriptorHeap[scene.clipmapSrv[cascade]];
        float3 uvw = local * 0.5 + 0.5;
        return float2(rawVolume.SampleLevel(UZ_SDF_REFLECTION_SAMPLER, uvw, 0),
            expandedVolume.SampleLevel(UZ_SDF_REFLECTION_SAMPLER, uvw, 0).r);
    }
    return float2(65504.0,65504.0);
}

/// @ref Wright et al. 2022 "Lumen", SDF Tracing Surface Bias and Surface Bias For Reflections.
/// @constants Ray kinds: 0=diffuse, 1=reflection, 2=shadow. Engine diffuse ramp is 0.5 cm/cm,
/// not a published default. Prior maximum raw clearance caps expansion to avoid self-intersection.
/// Numerical tolerance is max(0.001 cm, 0.01% voxel); two tolerances separate hit and clearance.
/// Raw distance advances the ray; detected threshold crossings are refined in their last interval.
/// @constants Sixteen bisections bound positional error to 1/65536 of that interval.
float TraceSdfRay(SdfReflectionScene scene, float3 origin, float3 direction, float maxDistance,
                  uint rayKind, float3 geometricNormal) {
    float epsilon = max(0.001, scene.voxelSize * 0.0001);
    float normalCosine = dot(geometricNormal, direction);
    bool surfaceRay = dot(geometricNormal, geometricNormal) > 0.0;
    if (surfaceRay && normalCosine <= 0.0) return -2.0;
    float t = epsilon;
    float previousT = 0.0;
    float maximumClearance = 0.0;
    [loop] for (uint step = 0; step < scene.traceSteps; ++step) {
        float2 distances = SampleSdfRayDistances(scene, origin + direction * t);
        if (distances.x <= -65504.0 || !all(isfinite(distances))) return -2.0;
        float expansion = min(max(distances.x - distances.y, 0.0), maximumClearance);
        if (rayKind != 1u) expansion = min(expansion, 0.5 * t);
        float hitTolerance = surfaceRay ? min(epsilon, 0.25 * t * normalCosine) : epsilon;
        if (distances.x - expansion <= hitTolerance) {
            float lower = previousT;
            float upper = t;
            [loop] for (uint refine = 0; refine < 16 && upper - lower > epsilon; ++refine) {
                float middle = 0.5 * (lower + upper);
                float2 candidate = SampleSdfRayDistances(scene, origin + direction * middle);
                if (candidate.x <= -65504.0 || !all(isfinite(candidate))) return -2.0;
                float candidateExpansion = min(max(candidate.x - candidate.y, 0.0), maximumClearance);
                if (rayKind != 1u) candidateExpansion = min(candidateExpansion, 0.5 * middle);
                float candidateTolerance = surfaceRay ? min(epsilon, 0.25 * middle * normalCosine) : epsilon;
                if (candidate.x - candidateExpansion <= candidateTolerance) upper = middle;
                else lower = middle;
            }
            return upper;
        }
        maximumClearance = max(maximumClearance, distances.x - 2.0 * epsilon);
        previousT = t;
        t += max(distances.x, epsilon);
        if (t >= maxDistance) return -1.0;
    }
    return -2.0;
}

float TraceSdfReflection(SdfReflectionScene scene, float3 origin, float3 direction, float maxDistance) {
    return TraceSdfRay(scene, origin, direction, maxDistance, 1u, 0.0);
}

float3 SdfReflectionNormal(SdfReflectionScene scene, float3 position) {
    float e = SdfReflectionEpsilon(scene);
    float3 gradient = float3(
        SampleSdfRayDistances(scene, position + float3(e,0,0)).x - SampleSdfRayDistances(scene, position - float3(e,0,0)).x,
        SampleSdfRayDistances(scene, position + float3(0,e,0)).x - SampleSdfRayDistances(scene, position - float3(0,e,0)).x,
        SampleSdfRayDistances(scene, position + float3(0,0,e)).x - SampleSdfRayDistances(scene, position - float3(0,0,e)).x);
    float magnitude = length(gradient);
    return magnitude > 0.0 ? gradient / magnitude : 0.0;
}

/// @constants Eight trilinear corners; seven float4s encode L2 SH; squared normal weights.
float3 SdfReflectionIrradiance(SdfReflectionScene scene, float3 position, float3 normal) {
    if (scene.probeSrv == 0xffffffffu || scene.probesPerAxis < 2 || scene.probeExtent <= 0.0) return 0.0;
    float spacing = 2.0 * scene.probeExtent / float(scene.probesPerAxis - 1);
    float3 cell = (position - scene.probeOrigin) / spacing;
    if (any(cell < 0.0) || any(cell > float(scene.probesPerAxis - 1))) return 0.0;
    uint3 lower = uint3(floor(cell));
    uint3 upper = min(lower + 1, scene.probesPerAxis - 1);
    float3 fraction = frac(cell);
    StructuredBuffer<float4> probes = ResourceDescriptorHeap[scene.probeSrv];
    float3 irradiance = 0.0;
    float totalWeight = 0.0;
    [unroll] for (uint corner = 0; corner < 8; ++corner) {
        uint3 index = uint3((corner & 1) ? upper.x : lower.x,
                           (corner & 2) ? upper.y : lower.y,
                           (corner & 4) ? upper.z : lower.z);
        float weight = ((corner & 1) ? fraction.x : 1.0 - fraction.x)
                     * ((corner & 2) ? fraction.y : 1.0 - fraction.y)
                     * ((corner & 4) ? fraction.z : 1.0 - fraction.z);
        float3 toProbe = scene.probeOrigin + float3(index) * spacing - position;
        float distance = length(toProbe);
        float normalWeight = distance > 0.0 ? saturate(dot(normal, toProbe / distance)) : 1.0;
        weight *= normalWeight * normalWeight;
        uint base = (index.x + index.y * scene.probesPerAxis + index.z * scene.probesPerAxis * scene.probesPerAxis) * 7;
        if (probes[base + 6].w <= 0.0) continue;
        float4 sh[7];
        [unroll] for (uint k = 0; k < 7; ++k) sh[k] = probes[base + k];
        irradiance += weight * EvalSH_L2(normal, sh);
        totalWeight += weight;
    }
    return totalWeight > 0.0 ? irradiance / totalWeight : 0.0;
}

float3 ShadeSdfReflectionHit(SdfReflectionScene scene, float3 position, float3 rayDirection) {
    float3 normal = SdfReflectionNormal(scene, position);
    if (dot(normal, -rayDirection) <= 0.0) return 0.0;
    float3 origin;
    if (!ResolveSdfRayOrigin(scene, position, normal, origin)) return 0.0;
    float ndl = saturate(dot(normal, scene.sunDirection));
    float visibility = 0.0;
    if (ndl > 0.0 && any(scene.sunColor > 0.0)) {
        visibility = TraceSdfRay(scene, origin, scene.sunDirection, scene.traceDistance, 2u, normal) == -1.0 ? 1.0 : 0.0;
    }
    float3 albedo = saturate(SampleSdfReflectionSurface(scene, position).gba);
    return albedo * (scene.sunColor * (ndl * visibility * M_1_PI)
                   + SdfReflectionIrradiance(scene, origin, normal));
}

float3 TraceSdfSurfaceRadiance(SdfReflectionScene scene, float3 origin, float3 direction,
                              uint rayKind, float3 geometricNormal) {
    float t = TraceSdfRay(scene, origin, direction, scene.traceDistance, rayKind, geometricNormal);
    if (t >= 0.0) return ShadeSdfReflectionHit(scene, origin + direction * t, direction);
    if (t == -1.0 && scene.skySrv != 0xffffffffu) {
        TextureCube<float4> sky = ResourceDescriptorHeap[scene.skySrv];
        return sky.SampleLevel(UZ_SDF_REFLECTION_SAMPLER, float3(direction.x, direction.z, direction.y), 0).rgb * scene.skyMultiplier;
    }
    return 0.0;
}

float3 TraceSdfReflectionRadiance(SdfReflectionScene scene, float3 origin, float3 direction) {
    return TraceSdfSurfaceRadiance(scene, origin, direction, 1u, 0.0);
}

/// @ref Karis 2013 "Real Shading in Unreal Engine 4", prefiltered environment integral.
/// @constants Up to 16 GGX samples; 0.04 mirror cutoff; raster origin is preserved.
/// 0.999 switches the tangent basis away from its pole. Integer hash multipliers
/// 1664525, 1013904223, 747796405 and xor 0x9e3779b9 decorrelate pixel/frame rotations.
float3 GatherSdfReflections(SdfReflectionScene scene, float3 position, float3 normal,
                           float3 geometricNormal, float3 reflection, float roughness, uint2 pixel, uint frame, uint samples) {
    float3 origin;
    if (!ResolveSdfRayOrigin(scene, position, geometricNormal, origin)) return 0.0;
    if (roughness <= 0.04) return TraceSdfSurfaceRadiance(scene, origin, reflection, 1u, geometricNormal);
    float3 up = abs(reflection.z) < 0.999 ? float3(0,0,1) : float3(1,0,0);
    float3 tangent = normalize(cross(up, reflection));
    float3 bitangent = cross(reflection, tangent);
    uint count = clamp(samples, 1u, 16u);
    uint seed = pixel.x * 1664525u + pixel.y * 1013904223u + frame * 747796405u;
    float2 rotation = float2(RadicalInverseVdC(seed), RadicalInverseVdC(seed ^ 0x9e3779b9u));
    float3 radiance = 0.0;
    float totalWeight = 0.0;
    [loop] for (uint i = 0; i < count; ++i) {
        float3 halfTangent = ImportanceSampleGGX(frac(Hammersley(i, count) + rotation), roughness);
        float3 halfVector = tangent * halfTangent.x + bitangent * halfTangent.y + reflection * halfTangent.z;
        float3 direction = reflect(-reflection, halfVector);
        float weight = saturate(dot(reflection, direction));
        if (dot(normal, direction) <= 0.0) weight = 0.0;
        if (weight > 0.0) radiance += weight * TraceSdfSurfaceRadiance(scene, origin, direction, 1u, geometricNormal);
        totalWeight += weight;
    }
    return totalWeight > 0.0 ? radiance / totalWeight : 0.0;
}

#endif
