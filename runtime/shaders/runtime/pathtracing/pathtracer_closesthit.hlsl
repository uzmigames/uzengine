#include "pathtracer_shared.hlsli"
#include "layered_bsdf.hlsli"
#include "hair_bcsdf.hlsli"
#include "hair_dual_scatter.hlsli"
#include "../include/restir_di.hlsli"
// Compiled in only with r.PathTracer.GIReSTIR at init: the plain libraries
// keep the committed text, so GI-off output is identical by construction.
#ifdef UZ_GI_RESTIR
#include "../include/restir_gi.hlsli"
#endif
#include "../include/light_bvh.hlsli"

[shader("closesthit")]
void main(inout PathPayload payload, BuiltInTriangleIntersectionAttributes attr) {
    // InstanceID is meshId in the shared TLAS. InstanceIndex addresses a
    // pass-local table so two instances of one mesh retain distinct materials.
    StructuredBuffer<MaterialData> materials = ResourceDescriptorHeap[materialTableSrv];
    uint materialId = InstanceIndex();
    MaterialData mat = materials[materialId];
    bool hair = PathIsHair(mat);
    ByteAddressBuffer vertices = ResourceDescriptorHeap[NonUniformResourceIndex(mat.vbSrv)];
    float3 weights = float3(1.0 - attr.barycentrics.x - attr.barycentrics.y, attr.barycentrics);
    float3 position = 0.0;
    float3 normal = 0.0;
    float2 uv = 0.0;
    float3 strandPositions[3];
    float2 strandUVs[3];
    [unroll]
    for (uint corner = 0u; corner < 3u; ++corner) {
        uint index = PrimitiveIndex() * 3u + corner;
        if (mat.ibSrv != 0xffffffffu) {
            ByteAddressBuffer indices = ResourceDescriptorHeap[NonUniformResourceIndex(mat.ibSrv)];
            uint address = index * mat.indexFormat;
            index = indices.Load(address & ~3u);
            if (mat.indexFormat == 2u) index = (index >> ((address & 2u) * 8u)) & 65535u;
        }
        uint vertex = index * mat.vertexStride;
        if (hair) {
            strandPositions[corner] = asfloat(vertices.Load3(vertex));
            strandUVs[corner] = asfloat(vertices.Load2(vertex + mat.uvOffset));
        }
        position += weights[corner] * asfloat(vertices.Load3(vertex));
        uv += weights[corner] * asfloat(vertices.Load2(vertex + mat.uvOffset));
        float3 n;
        if (mat.normalFormat == 1u) {
            uint packed = vertices.Load(vertex + mat.normalOffset);
            n = float3(asint(packed << 24u) >> 24, asint(packed << 16u) >> 24,
                       asint(packed << 8u) >> 24) / 127.0;
        } else {
            n = asfloat(vertices.Load3(vertex + mat.normalOffset));
        }
        normal += weights[corner] * n;
    }
    float3 P = mul(ObjectToWorld3x4(), float4(position, 1.0));
    float3 N = normalize(mul(normal, (float3x3)WorldToObject3x4()));
    float3 V = -WorldRayDirection();
    if (dot(N, V) < 0.0) N = -N;
    float3 strandTangent = 0.0;
    float fiberOffset = 0.0;
    if (hair) {
        // Hair-tagged triangle ribbons use U across the fiber and V root-to-tip.
        // Recover dP/dV from geometry instead of assuming a world-space axis.
        float2 duv1 = strandUVs[1]-strandUVs[0];
        float2 duv2 = strandUVs[2]-strandUVs[0];
        float determinant = duv1.x*duv2.y-duv1.y*duv2.x;
        float3 dPdv = ((strandPositions[2]-strandPositions[0])*duv1.x
                    -(strandPositions[1]-strandPositions[0])*duv2.x)/determinant;
        strandTangent = normalize(mul((float3x3)ObjectToWorld3x4(),dPdv));
        N = normalize(N-dot(N,strandTangent)*strandTangent);
        float3 dPdu = ((strandPositions[1]-strandPositions[0])*duv2.y
                    -(strandPositions[2]-strandPositions[0])*duv1.y)/determinant;
        float3 across = mul((float3x3)ObjectToWorld3x4(),dPdu);
        // Signed near-field offset follows the outgoing tangent frame, including
        // a view from the back of a two-sided ribbon.
        fiberOffset = (2.0*uv.x-1.0)
                    * (dot(across,cross(strandTangent,N)) < 0.0 ? -1.0 : 1.0);
    }
    uv = uv * mat.uvScale + mat.uvOffsetValue;
    float3 albedo = mat.tint;
    if (mat.albedoSrv != 0xffffffffu) {
        Texture2D<float4> tex = ResourceDescriptorHeap[NonUniformResourceIndex(mat.albedoSrv)];
        albedo *= tex.SampleLevel(linearWrap, uv, 0).rgb;
    }
    // Missing textures use the scalar factors directly, like gbuffer_ps.
    float roughness = 1.0;
    float metallic = 1.0;
    if ((mat.materialFlags & 4u) != 0u && mat.roughnessSrv != 0xffffffffu) {
        Texture2D<float4> orm = ResourceDescriptorHeap[NonUniformResourceIndex(mat.roughnessSrv)];
        float3 channels = orm.SampleLevel(linearWrap, uv, 0).rgb;
        roughness = channels.g; metallic = channels.b;
    } else {
        if (mat.roughnessSrv != 0xffffffffu) {
            Texture2D<float4> tex = ResourceDescriptorHeap[NonUniformResourceIndex(mat.roughnessSrv)];
            roughness = tex.SampleLevel(linearWrap, uv, 0).r;
        }
        if (mat.metallicSrv != 0xffffffffu) {
            Texture2D<float4> tex = ResourceDescriptorHeap[NonUniformResourceIndex(mat.metallicSrv)];
            metallic = tex.SampleLevel(linearWrap, uv, 0).r;
        }
    }
    roughness = saturate(roughness * mat.roughnessMultiplier);
    metallic = saturate(metallic * mat.metallicMultiplier);
    float3 emission = mat.emission;
    if (mat.emissiveSrv != 0xffffffffu) {
        Texture2D<float4> tex = ResourceDescriptorHeap[NonUniformResourceIndex(mat.emissiveSrv)];
        emission *= tex.SampleLevel(linearWrap, uv, 0).rgb;
    }
    float3 direct = 0.0;
    float3 diffuseDirect = 0.0;
    if (nrdEnabled != 0u && payload.bounceCount == 0u && restirMode == 0u) {
        RWTexture2D<float4> outputAlbedo = ResourceDescriptorHeap[nrdAlbedoUav];
        outputAlbedo[DispatchRaysIndex().xy] = float4(albedo, 1.0);
        RWTexture2D<float4> modulation = ResourceDescriptorHeap[nrdModulationUav];
        RWTexture2D<float4> virtualPosition = ResourceDescriptorHeap[nrdVirtualPositionUav];
        modulation[DispatchRaysIndex().xy] = float4(ComputeF0(albedo, metallic), roughness);
        virtualPosition[DispatchRaysIndex().xy] = float4(P, 1.0);
        RWTexture2D<float4> guide = ResourceDescriptorHeap[nrdGuideUav];
        float4 clip = mul(float4(P, 1), viewProj);
        guide[DispatchRaysIndex().xy] = float4(OctEncode(N), clip.z / clip.w, roughness);
    }
    if (nrdEnabled != 0u && payload.bounceCount == 1u) {
        // Unfold the reflected segment through the primary tangent plane.
        // This virtual point follows reflection parallax, not surface motion.
        RWTexture2D<float4> virtualPosition = ResourceDescriptorHeap[nrdVirtualPositionUav];
        virtualPosition[DispatchRaysIndex().xy] = float4(NrdVirtualPosition(payload.previousPosition,
            WorldRayDirection(), payload.previousNormal, RayTCurrent()), 1.0);
    }
    StructuredBuffer<PathDirectionalLight> sceneLights = ResourceDescriptorHeap[directionalLightsSrv];
    uint bvhConfigIndex = sceneLights[0].padding;
    if (restirMode != 0u) {
        RestirSurface surface;
        surface.position = P; surface.valid = hair ? 0u : 1u;
        surface.normal = N; surface.roughness = roughness;
        surface.view = V; surface.metallic = metallic;
        surface.albedo = albedo; surface.padding = 0.0;
        RWStructuredBuffer<RestirSurface> surfaces = ResourceDescriptorHeap[restirSurfaceUav];
        uint2 pixel = DispatchRaysIndex().xy;
        surfaces[pixel.y * restirWidth + pixel.x] = surface;
        payload.terminated = true;
        return;
    }
    if (!hair) payload.hairBounces = 0u;
    // Partition BEFORE this vertex's NEE and continuation, not after adding
    // either. Unsupported near-field lighting/volumes retain explicit transport.
    if (HairDualReplace(hair,mat.coatWeight>0.0,int(payload.hairBounces),int(mat.coatRoughness))
        && bvhConfigIndex == 0u && !PathVolumeActive()) {
        uint dimension = HashPCG((4096u+payload.bounceCount*64u)^HashPCG(sampleIndex));
        float3 tail = HairDualTail(P,N,V,strandTangent,
            PathHairMaterial(albedo,roughness,metallic,fiberOffset),mat.coatIOR,dimension);
        payload.radiance += payload.throughput*(emission+tail);
        if (nrdEnabled != 0u) {
            NrdAddDiffuse(NrdFraction()*payload.throughput*(emission+tail));
            NrdAddSpecular((1.0-NrdFraction())*payload.throughput*(emission+tail));
        }
        payload.terminated = true;
        return;
    }
    if (hair) payload.hairBounces++;
#ifdef UZ_GI_RESTIR
    // GI-ReSTIR: a receiving primary surface seeds its canonical reservoir as a
    // null outcome with M = 1, never an absent domain. The continuation ray
    // origin is the surface position every target and visibility query uses.
    uint giIndex = DispatchRaysIndex().y * restirWidth + DispatchRaysIndex().x;
    bool giCandidate = false;
    float3 giLightDirection = 0.0, giLightValue = 0.0, giRawValue = 0.0;
    float giLightPdf = 0.0;
    uint giEmitter = 0xffffffffu;
    float3 giEmission = emission;
    if (giRestirEnabled != 0u && payload.bounceCount == 0u && !hair) {
        RWStructuredBuffer<GIReservoir> reservoirs = ResourceDescriptorHeap[giRestirInitialUav];
        GIReservoir r = (GIReservoir)0;
        RestirSurface receiver;
        receiver.position = P + N * 0.05; receiver.valid = 1u;
        receiver.normal = N; receiver.roughness = roughness;
        receiver.view = V; receiver.metallic = metallic;
        receiver.albedo = albedo; receiver.padding = 0.0;
        GISetSurface(r, receiver);
        r.M = 1u;
        reservoirs[giIndex] = r;
    }
    if (giRestirEnabled != 0u && payload.bounceCount == 1u && !hair) {
        RWStructuredBuffer<GIReservoir> reservoirs = ResourceDescriptorHeap[giRestirInitialUav];
        // Grazing or touching vertices keep the plain estimator for this sample.
        giCandidate = reservoirs[giIndex].M == 1u && payload.previousBsdfPdf > 0.0
                   && dot(N, V) > 1e-4 && RayTCurrent() > 0.1;
    }
#endif
    if (restirEnabled != 0u && payload.bounceCount == 0u && !hair) {
        RWStructuredBuffer<Reservoir> reservoirs = ResourceDescriptorHeap[restirCurrentUav];
        uint2 pixel = DispatchRaysIndex().xy;
        Reservoir r = reservoirs[pixel.y * restirWidth + pixel.x];
        if (r.W > 0.0) {
            ShadowPayload shadow;
            shadow.hit = true;
            RayDesc ray;
            ray.Origin = P + N * 0.05;
            float distance;
            ray.Direction = RestirDirection(P, r.sample, distance);
            ray.TMin = 0.01; ray.TMax = max(distance - 0.05, 0.01);
            if (any(r.sample.normal != 0.0)) {
                float3 delta = r.sample.position - ray.Origin;
                distance = length(delta);
                ray.Direction = delta / distance;
                ray.TMax = max(distance - 0.01, 0.01);
            }
            TraceRay(tScene, RAY_FLAG_FORCE_OPAQUE | RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH
                     | RAY_FLAG_SKIP_CLOSEST_HIT_SHADER, 0xff, 0, 0, 1, ray, shadow);
            if (!shadow.hit) {
                RestirSurface surface;
                surface.position = P; surface.valid = 1u;
                surface.normal = N; surface.roughness = roughness;
                surface.view = V; surface.metallic = metallic;
                surface.albedo = albedo; surface.padding = 0.0;
                direct = RestirContribution(surface, r.sample) * r.W;
                if (nrdEnabled != 0u) {
                    float3 full = EvaluatePBR(N, V, ray.Direction, albedo, roughness, metallic);
                    diffuseDirect = direct * NrdDiffuseBRDF(N, V, ray.Direction, albedo, metallic)
                                  / max(full, 1e-20);
                }
            }
        }
    } else {
    if (bvhConfigIndex != 0u) {
        StructuredBuffer<LightBVHConfig> configurations = ResourceDescriptorHeap[bvhConfigIndex-1u];
        LightBVHConfig config = configurations[0];
        StructuredBuffer<LightBVHNode> nodes = ResourceDescriptorHeap[config.nodesSrv];
        StructuredBuffer<LightBVHLight> emitters = ResourceDescriptorHeap[config.lightsSrv];
        if (payload.bounceCount > 0u && payload.previousBsdfPdf >= 0.0 && any(emission > 0.0)) {
            for (uint id = 0u; id < config.lightCount; ++id) {
                LightBVHLight light = emitters[id];
                if (light.type != 0u || light.sourceIndex != mat.lightSourceIndex
                    || light.padding != PrimitiveIndex()) continue;
                float3 delta = P-payload.previousPosition;
                float d2 = dot(delta,delta);
                float cosine = abs(dot(light.normal,-normalize(delta)));
                if (cosine > 0.0) {
                    float selectPdf = EvaluateLightBVHPDF(payload.previousPosition,
                        payload.previousNormal,id,config,nodes,emitters);
                    float lightPdf = LightBVHSolidAnglePDF(selectPdf,light.area,d2,cosine);
                    emission *= LightBVHBalanceWeight(payload.previousBsdfPdf,
                        float(neeSamples)*lightPdf);
#ifdef UZ_GI_RESTIR
                    giEmitter = id;
#endif
                }
                break;
            }
        }
        for (uint sample = 0u; sample < neeSamples; ++sample) {
            uint dimension = HashPCG((128u + sample*2u + payload.bounceCount*64u)
                ^ HashPCG(sampleIndex));
            float2 u = PathSample(dimension);
            // @ref Halton 1960 "On the efficiency of certain quasi-random sequences of points"
            // @constants 2,3,5 = distinct prime bases for selection and triangle coordinates.
            // PathSample(dimension+1).x reuses base 2: only its pixel rotation
            // may be reused, not its sample-index sequence.
            float triangleV = frac(Halton(sampleIndex+1u,5u)
                + PathSample(dimension+1u).x - Halton(sampleIndex+1u,2u));
            LightBVHSample chosen = SampleLightBVH(P,N,
                float3(u,triangleV),config,nodes,emitters);
            LightBVHLight light = emitters[chosen.lightId];
            // Stack sampling has a path-space density, not a marginal BSDF PDF.
            // Area lights use continuation only; analytic lights retain the
            // unbiased last-exit estimator in EvaluatePathStack.
            if (PathHasStack(mat) && light.type == 0u) continue;
            // @ref Pharr et al. 2023 "Physically Based Rendering", section 13.4
            // Area NEE replaces a BSDF ray to an emitter, so it must obey
            // that ray's depth limit. Analytic NEE has no additional surface.
            if (light.type == 0u && !PathCanContinue(payload.bounceCount+1u,maxBounces)) continue;
            float3 L;
            float distance = 1e7;
            float3 value = light.emission / chosen.selectionPdf;
            if (light.type == 1u) {
                L = normalize(light.v0);
                float3 T,B; PathBasis(L,T,B);
                float2 disk = PathSample(dimension+1u);
                float radius = sqrt(disk.x)*tan(light.angularRadius);
                float phi = 2.0*PI*disk.y;
                L = normalize(L+radius*(cos(phi)*T+sin(phi)*B));
#ifdef UZ_GI_RESTIR
                giRawValue = value;
#endif
            } else {
                float3 delta = chosen.samplePos-P;
                float d2 = dot(delta,delta);
                distance = sqrt(d2);
                L = delta/max(distance,1e-10);
                value = light.emission * abs(dot(light.normal,-L))
                      / max(d2*chosen.pdf,1e-20);
                if (light.emissiveSrv != 0xffffffffu) {
                    Texture2D<float4> tex = ResourceDescriptorHeap[NonUniformResourceIndex(light.emissiveSrv)];
                    value *= tex.SampleLevel(linearWrap,chosen.uv,0).rgb;
                }
                float cosine = abs(dot(light.normal,-L));
                if (cosine <= 0.0) continue;
                float lightPdf = LightBVHSolidAnglePDF(chosen.selectionPdf,light.area,d2,cosine);
#ifdef UZ_GI_RESTIR
                // The receiver re-applies this balance with its own view direction.
                giRawValue = value;
                giLightPdf = lightPdf;
#endif
                value *= LightBVHBalanceWeight(float(neeSamples)*lightPdf,
                    PathSurfacePDF(N,V,L,roughness,metallic,mat,albedo,strandTangent,fiberOffset));
            }
            float3 brdf = EvaluatePathSurface(N,V,L,albedo,roughness,metallic,mat,strandTangent,fiberOffset);
            if (any(brdf > 0.0) && distance > .1) {
                ShadowPayload shadow; shadow.hit = true;
                RayDesc ray;
                ray.Origin = P+N*(hair && dot(N,L) < 0.0 ? -.05 : .05); ray.Direction = L;
                ray.TMin = .01; ray.TMax = max(distance-.1,.01);
                if (light.type == 0u) {
                    // The endpoint must be measured from the offset origin.
                    // Reusing L from P hits the emitter before TMax at grazing
                    // angles, falsely treating the sampled light as a blocker.
                    float3 shadowDelta = chosen.samplePos-ray.Origin;
                    float shadowDistance = length(shadowDelta);
                    ray.Direction = shadowDelta/shadowDistance;
                    ray.TMax = max(shadowDistance-.01,.01);
                }
                TraceRay(tScene,RAY_FLAG_FORCE_OPAQUE|RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH
                    |RAY_FLAG_SKIP_CLOSEST_HIT_SHADER,0xff,0,0,1,ray,shadow);
                if (!shadow.hit) {
                    value *= PathVolumeShadow(P,L,distance,
                        PathVolumeSeed(sampleIndex,DispatchRaysIndex().xy,20480u+sample+payload.bounceCount*128u));
#ifdef UZ_GI_RESTIR
                    giLightDirection = L;
                    giLightValue = giRawValue;
#endif
                    direct += brdf*value/float(neeSamples);
                    if (nrdEnabled != 0u && !hair)
                        diffuseDirect += NrdDiffuseBRDF(N,V,L,albedo,metallic)
                            * PathLayerTransmission(mat,N,V,L)*value/float(neeSamples);
                }
            }
        }
    } else {
    // Next-event estimation at every vertex; scene lights store integrated
    // directional irradiance, so normalized source sampling averages visibility.
    StructuredBuffer<PathDirectionalLight> lights = ResourceDescriptorHeap[directionalLightsSrv];
    for (uint lightIndex = 0u; lightIndex < directionalLightCount; ++lightIndex) {
        PathDirectionalLight light = lights[lightIndex];
        float3 L = normalize(light.direction);
        float3 T, B;
        PathBasis(L, T, B);
        for (uint sample = 0u; sample < neeSamples; ++sample) {
            float2 u = PathSample(sample + 1u + lightIndex * 4u + payload.bounceCount * 64u);
            float radius = sqrt(u.x) * tan(light.angularRadius);
            float phi = 2.0 * PI * u.y;
            float3 shadowDirection = normalize(L + radius * (cos(phi) * T + sin(phi) * B));
            ShadowPayload shadow;
            shadow.hit = true;
            RayDesc ray;
            ray.Origin = P + N * (hair && dot(N,shadowDirection) < 0.0 ? -0.05 : 0.05);
            ray.Direction = shadowDirection;
            ray.TMin = 0.01;
            ray.TMax = 1e7;
            TraceRay(tScene, RAY_FLAG_FORCE_OPAQUE | RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH
                     | RAY_FLAG_SKIP_CLOSEST_HIT_SHADER, 0xff, 0, 0, 1, ray, shadow);
            if (!shadow.hit) {
#ifdef UZ_GI_RESTIR
                giLightDirection = shadowDirection;
                giLightValue = light.radiance;
#endif
                direct += EvaluatePathSurface(N, V, shadowDirection, albedo, roughness, metallic, mat,
                                              strandTangent, fiberOffset)
                        * light.radiance * PathVolumeShadow(P,shadowDirection,1e7,
                            PathVolumeSeed(sampleIndex,DispatchRaysIndex().xy,
                                24576u+sample+lightIndex*32u+payload.bounceCount*1024u)) / float(neeSamples);
                if (nrdEnabled != 0u && !hair)
                    diffuseDirect += NrdDiffuseBRDF(N,V,shadowDirection,albedo,metallic)
                                   * PathLayerTransmission(mat,N,V,shadowDirection)
                                   * light.radiance / float(neeSamples);
            }
        }
    }
    }
    }
#ifdef UZ_GI_RESTIR
    if (giCandidate) {
        // The one-bounce term becomes the canonical reservoir sample instead
        // of radiance: W = 1/p_A, the generating density in area measure.
        RWStructuredBuffer<GIReservoir> reservoirs = ResourceDescriptorHeap[giRestirInitialUav];
        GIReservoir r = reservoirs[giIndex];
        r.sample.position = P; r.sample.lightIndex = giEmitter;
        r.sample.normal = N; r.sample.roughness = roughness;
        r.sample.albedo = albedo; r.sample.metallic = metallic;
        r.sample.emission = giEmission; r.sample.lightPdf = giLightPdf;
        r.sample.lightDirection = giLightDirection;
        r.sample.lightValue = giLightValue;
        float distance = RayTCurrent();
        r.W = distance * distance / (payload.previousBsdfPdf * dot(N, V));
        r.visibility = 1.0;
        reservoirs[giIndex] = r;
    } else
#endif
    payload.radiance += payload.throughput * (emission + direct);
    if (nrdEnabled != 0u) {
        NrdAddDiffuse(payload.bounceCount == 0u ? diffuseDirect
            : NrdFraction() * payload.throughput * (emission + direct));
        NrdAddSpecular(payload.bounceCount == 0u ? direct - diffuseDirect
            : (1.0 - NrdFraction()) * payload.throughput * (emission + direct));
    }
    payload.bounceCount++;
    payload.terminated = true;
    if (!PathCanContinue(payload.bounceCount, maxBounces)) return;
    float3 weight;
    // @ref O'Neill 2014 "PCG: A Family of Simple Fast Space-Efficient Statistically Good Algorithms for Random Number Generation"
    // Per-sample dimension scrambling makes the BSDF lobe choice independent
    // of its angular variates. Fixed rotations of the same Halton base leave
    // a persistent (non-converging) correlation even with BVH disabled.
    uint dimension = HashPCG((32u + payload.bounceCount * 64u) ^ HashPCG(sampleIndex));
    float3 nextDirection = SamplePathSurface(N, V, albedo, roughness, metallic, dimension, mat,
                                            strandTangent, fiberOffset, weight);
    if (nrdEnabled != 0u && payload.bounceCount == 1u) {
        RWTexture2D<float4> fraction = ResourceDescriptorHeap[nrdFractionUav];
        float3 full = EvaluatePathSurface(N,V,nextDirection,albedo,roughness,metallic,mat,
                                         strandTangent,fiberOffset);
        fraction[DispatchRaysIndex().xy] = float4(
            (hair ? float3(0,0,0) : NrdDiffuseBRDF(N,V,nextDirection,albedo,metallic))
                * PathLayerTransmission(mat,N,V,nextDirection) / max(full,1e-20), 1.0);
    }
    payload.previousPosition = P;
    payload.previousNormal = N;
    payload.previousBsdfPdf = PathSurfacePDF(N,V,nextDirection,roughness,metallic,mat,
                                           albedo,strandTangent,fiberOffset);
    payload.throughput *= weight;
    if (!all(isfinite(payload.throughput)) || max(payload.throughput.x,
        max(payload.throughput.y, payload.throughput.z)) <= 0.0) return;
    // @ref Veach 1997 "Robust Monte Carlo Methods for Light Transport Simulation"
    // @constants 0.05 = lower survival bound, 1 = no artificial termination.
    if (rrEnabled != 0u && payload.bounceCount >= rrMinBounce) {
        float survival = PathSurvivalProbability(max(payload.throughput.x,
                               max(payload.throughput.y, payload.throughput.z)));
        if (PathSample(dimension + 2u).x >= survival) return;
        payload.throughput /= survival;
    }
    payload.nextOrigin = P + N * (hair && dot(N,nextDirection) < 0.0 ? -0.05 : 0.05);
    payload.nextDirection = nextDirection;
    payload.terminated = false;
}
