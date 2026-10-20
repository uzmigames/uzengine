#include "pathtracer_shared.hlsli"
#include "../include/restir_di.hlsli"
// Compiled in only with r.PathTracer.GIReSTIR at init: the plain libraries
// keep the committed text, so GI-off output is identical by construction.
#ifdef UZ_GI_RESTIR
#include "../include/restir_gi.hlsli"
#endif
#include "../include/light_bvh.hlsli"

float3 PathVolumeDirect(float3 P, float3 incoming, uint bounce) {
    float3 direct = 0.0;
    StructuredBuffer<PathDirectionalLight> lights = ResourceDescriptorHeap[directionalLightsSrv];
    uint configIndex = lights[0].padding;
    if (configIndex != 0u) {
        StructuredBuffer<LightBVHConfig> configurations = ResourceDescriptorHeap[configIndex-1u];
        LightBVHConfig config = configurations[0];
        StructuredBuffer<LightBVHNode> nodes = ResourceDescriptorHeap[config.nodesSrv];
        StructuredBuffer<LightBVHLight> emitters = ResourceDescriptorHeap[config.lightsSrv];
        for (uint sample = 0u; sample < neeSamples; ++sample) {
            uint dimension = HashPCG((512u+sample*4u+bounce*128u)^HashPCG(sampleIndex));
            float2 u = PathSample(dimension);
            float triangleV = frac(Halton(sampleIndex+1u,5u)
                +PathSample(dimension+1u).x-Halton(sampleIndex+1u,2u));
            // A medium has no surface hemisphere. Zero normal preserves
            // support over the complete sphere, including behind the ray.
            LightBVHSample chosen = SampleLightBVH(P,0.0,float3(u,triangleV),config,nodes,emitters);
            LightBVHLight light = emitters[chosen.lightId];
            if (light.type == 0u && !PathCanContinue(bounce+1u,maxBounces)) continue;
            float distance = 1e7;
            float3 L, value = light.emission/chosen.selectionPdf;
            if (light.type == 1u) {
                L = normalize(light.v0);
                float3 T,B; PathBasis(L,T,B);
                float2 disk = PathSample(dimension+2u);
                float radius = sqrt(disk.x)*tan(light.angularRadius);
                float phi = 2.0*PI*disk.y;
                L = normalize(L+radius*(cos(phi)*T+sin(phi)*B));
            } else {
                float3 delta = chosen.samplePos-P;
                float d2 = dot(delta,delta);
                distance = sqrt(d2);
                L = delta/max(distance,1e-10);
                float cosine = abs(dot(light.normal,-L));
                if (cosine <= 0.0 || distance <= .01) continue;
                float lightPdf = LightBVHSolidAnglePDF(chosen.selectionPdf,light.area,d2,cosine);
                value = light.emission*cosine/max(d2*chosen.pdf,1e-20);
                value *= LightBVHBalanceWeight(float(neeSamples)*lightPdf,
                    PathVolumePhase(dot(incoming,L),volumeG));
                if (light.emissiveSrv != 0xffffffffu) {
                    Texture2D<float4> tex = ResourceDescriptorHeap[NonUniformResourceIndex(light.emissiveSrv)];
                    value *= tex.SampleLevel(linearWrap,chosen.uv,0).rgb;
                }
            }
            RayDesc shadowRay;
            shadowRay.Origin = P; shadowRay.Direction = L;
            shadowRay.TMin = .001; shadowRay.TMax = max(distance-.01,.001);
            ShadowPayload shadow; shadow.hit = false;
            if (hasGeometry != 0u) {
                shadow.hit = true;
                TraceRay(tScene,RAY_FLAG_FORCE_OPAQUE|RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH
                    |RAY_FLAG_SKIP_CLOSEST_HIT_SHADER,0xff,0,0,1,shadowRay,shadow);
            }
            if (!shadow.hit)
                direct += value*PathVolumePhase(dot(incoming,L),volumeG)
                    *PathVolumeShadow(P,L,distance,PathVolumeSeed(sampleIndex,DispatchRaysIndex().xy,
                        4096u+sample+bounce*128u))/float(neeSamples);
        }
    } else {
        for (uint id = 0u; id < directionalLightCount; ++id) {
            PathDirectionalLight light = lights[id];
            float3 L = normalize(light.direction);
            float3 T,B; PathBasis(L,T,B);
            for (uint sample = 0u; sample < neeSamples; ++sample) {
                uint dimension = HashPCG((768u+id*64u+sample*2u+bounce*128u)^HashPCG(sampleIndex));
                float2 u = PathSample(dimension);
                float radius = sqrt(u.x)*tan(light.angularRadius);
                float phi = 2.0*PI*u.y;
                float3 direction = normalize(L+radius*(cos(phi)*T+sin(phi)*B));
                RayDesc shadowRay;
                shadowRay.Origin = P; shadowRay.Direction = direction;
                shadowRay.TMin = .001; shadowRay.TMax = 1e7;
                ShadowPayload shadow; shadow.hit = false;
                if (hasGeometry != 0u) {
                    shadow.hit = true;
                    TraceRay(tScene,RAY_FLAG_FORCE_OPAQUE|RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH
                        |RAY_FLAG_SKIP_CLOSEST_HIT_SHADER,0xff,0,0,1,shadowRay,shadow);
                }
                if (!shadow.hit)
                    direct += light.radiance*PathVolumePhase(dot(incoming,direction),volumeG)
                        *PathVolumeShadow(P,direction,1e7,PathVolumeSeed(sampleIndex,DispatchRaysIndex().xy,
                            8192u+sample+id*32u+bounce*1024u))/float(neeSamples);
            }
        }
    }
    return direct;
}

[shader("raygeneration")]
void main() {
    uint2 pixel = DispatchRaysIndex().xy;
    if (nrdEnabled != 0u && restirMode == 0u) {
        RWTexture2D<float4> diffuse = ResourceDescriptorHeap[nrdDiffuseUav];
        RWTexture2D<float4> fraction = ResourceDescriptorHeap[nrdFractionUav];
        RWTexture2D<float4> albedo = ResourceDescriptorHeap[nrdAlbedoUav];
        diffuse[pixel] = 0.0;
        fraction[pixel] = 0.0;
        albedo[pixel] = 1.0;
        RWTexture2D<float4> specular = ResourceDescriptorHeap[nrdSpecularUav];
        RWTexture2D<float4> modulation = ResourceDescriptorHeap[nrdModulationUav];
        RWTexture2D<float4> virtualPosition = ResourceDescriptorHeap[nrdVirtualPositionUav];
        specular[pixel] = 0.0;
        modulation[pixel] = float4(1, 1, 1, 1);
        virtualPosition[pixel] = 0.0;
        RWTexture2D<float4> guide = ResourceDescriptorHeap[nrdGuideUav];
        guide[pixel] = 0.0;
    }
    if (restirMode != 0u) {
        RWStructuredBuffer<RestirSurface> surfaces = ResourceDescriptorHeap[restirSurfaceUav];
        surfaces[pixel.y * restirWidth + pixel.x] = (RestirSurface)0;
    }
#ifdef UZ_GI_RESTIR
    if (giRestirEnabled != 0u) {
        // M = 0 until the closest-hit records a receiving primary surface.
        RWStructuredBuffer<GIReservoir> reservoirs = ResourceDescriptorHeap[giRestirInitialUav];
        reservoirs[pixel.y * restirWidth + pixel.x] = (GIReservoir)0;
    }
#endif
    float2 jitter = float2(Halton(sampleIndex + 1u, 2u),
                           Halton(sampleIndex + 1u, 3u)) - 0.5;
    float2 uv = (float2(pixel) + 0.5 + jitter) / float2(DispatchRaysDimensions().xy);
    float2 ndc = float2(uv.x * 2.0 - 1.0, 1.0 - uv.y * 2.0);
    float4 target = mul(float4(ndc, 0.5, 1.0), invViewProj);
    RayDesc ray;
    ray.Origin = cameraPos.xyz;
    ray.Direction = normalize(target.xyz / target.w - ray.Origin);
    ray.TMin = 0.01;
    ray.TMax = 1e7;
    PathPayload payload;
    payload.radiance = 0.0;
    payload.throughput = 1.0;
    payload.bounceCount = 0u;
    payload.hairBounces = 0u;
    payload.terminated = false;
    payload.previousPosition = 0.0;
    payload.previousNormal = 0.0;
    payload.previousBsdfPdf = 0.0;
    if (hasGeometry != 0u || PathVolumeActive()) {
        // Iteration keeps the DXR stack at primary + nested shadow regardless of N.
        // @ref Kajiya 1986 "The Rendering Equation"
        // @constants maxBounces is the configured number of surface vertices.
        for (uint bounce = 0u; bounce < maxBounces; ++bounce) {
            if (PathVolumeActive() && restirMode == 0u) {
                float surfaceDistance = ray.TMax;
                if (hasGeometry != 0u) {
                    RayQuery<RAY_FLAG_FORCE_OPAQUE> query;
                    query.TraceRayInline(tScene,RAY_FLAG_NONE,0xff,ray);
                    while (query.Proceed()) {}
                    if (query.CommittedStatus() == COMMITTED_TRIANGLE_HIT)
                        surfaceDistance = query.CommittedRayT();
                }
                float2 interval = PathVolumeInterval(ray.Origin,ray.Direction,surfaceDistance);
                float lengthInMedium = interval.y-interval.x;
                float sigmaT = volumeSigmaA+volumeSigmaS;
                if (volumeSigmaS == 0.0) {
                    // Absorption-only: ratio tracking, or exact Beer-Lambert
                    // without RNG in the constant-density limit.
                    payload.throughput *= PathVolumeShadow(ray.Origin,ray.Direction,surfaceDistance,
                        PathVolumeSeed(sampleIndex,pixel,12288u+bounce));
                } else if (lengthInMedium > 0.0) {
                    uint dimension = HashPCG((1024u+bounce*128u)^HashPCG(sampleIndex));
                    float distance = PathVolumeDelta(sigmaT,volumeVariation,
                        PathVolumeCoordinate(ray.Origin+ray.Direction*interval.x),
                        PathVolumeSlope(ray.Direction),lengthInMedium,PathSample(dimension).x,
                        PathVolumeSeed(sampleIndex,pixel,16384u+bounce));
                    if (distance < lengthInMedium) {
                        float3 P = ray.Origin+ray.Direction*(interval.x+distance);
                        // Tr*sigmaS / (sigmaT*Tr) = scattering albedo.
                        // Surface survival similarly has weight Tr/Tr = 1.
                        payload.throughput *= volumeSigmaS/sigmaT;
                        payload.radiance += payload.throughput*PathVolumeDirect(P,ray.Direction,bounce);
                        payload.bounceCount++;
                        payload.hairBounces = 0u;
                        if (!PathCanContinue(payload.bounceCount,maxBounces)) break;
                        float2 phaseU = PathSample(dimension+1u);
                        float cosine = clamp(PathVolumePhaseCosine(phaseU.x,volumeG),-1.0,1.0);
                        float sine = sqrt(max(0.0,1.0-cosine*cosine));
                        float phi = 2.0*PI*phaseU.y;
                        float3 T,B; PathBasis(ray.Direction,T,B);
                        float3 direction = normalize(cosine*ray.Direction+sine*(cos(phi)*T+sin(phi)*B));
                        payload.previousPosition = P;
                        payload.previousNormal = 0.0;
                        payload.previousBsdfPdf = PathVolumePhase(cosine,volumeG);
                        ray.Origin = P; ray.Direction = direction; ray.TMin = .001;
                        if (rrEnabled != 0u && payload.bounceCount >= rrMinBounce) {
                            float survival = PathSurvivalProbability(max(payload.throughput.x,
                                max(payload.throughput.y,payload.throughput.z)));
                            if (PathSample(dimension+2u).x >= survival) break;
                            payload.throughput /= survival;
                        }
                        continue;
                    }
                }
            }
            if (hasGeometry == 0u) {
                payload.radiance += payload.throughput*PathEnvironment(ray.Direction);
                break;
            }
            TraceRay(tScene, RAY_FLAG_FORCE_OPAQUE, 0xff, 0, 0, 0, ray, payload);
            if (restirMode != 0u) return;
            if (payload.terminated) break;
            ray.Origin = payload.nextOrigin;
            ray.Direction = payload.nextDirection;
        }
    } else {
        payload.radiance = PathEnvironment(ray.Direction);
        payload.terminated = true;
    }
    if (restirMode != 0u) return;
#ifdef UZ_GI_RESTIR
    if (giRestirEnabled != 0u) {
        // The one-bounce term is resampled and resolved by restir_gi_resolve_cs.
        RWTexture2D<float4> partial = ResourceDescriptorHeap[giRestirPartialUav];
        partial[pixel] = float4(payload.radiance, 1.0);
        return;
    }
#endif
    // Fused resolve: no extra dispatch, half-float history, or sample intermediate.
    // @ref Kahan 1965 "Pracniques: Further Remarks on Reducing Truncation Errors"
    // @constants 1 = one new sample; zero history is overwritten without reading it.
    RWTexture2D<float4> accumulated = ResourceDescriptorHeap[accumulationUav];
    RWTexture2D<float4> compensation = ResourceDescriptorHeap[compensationUav];
    precise float3 mean = payload.radiance;
    float3 correction = 0.0;
    if (accumulatedFrames != 0u) {
        float3 previous = accumulated[pixel].rgb;
        correction = compensation[pixel].rgb;
        mean.x = PathAccumulate(payload.radiance.x, previous.x, correction.x, accumulatedFrames);
        mean.y = PathAccumulate(payload.radiance.y, previous.y, correction.y, accumulatedFrames);
        mean.z = PathAccumulate(payload.radiance.z, previous.z, correction.z, accumulatedFrames);
    }
    accumulated[pixel] = float4(mean, 1.0);
    compensation[pixel] = float4(correction, 0.0);
    uRadiance[pixel] = float4(nrdEnabled != 0u ? payload.radiance : mean, 1.0);
}
