#include "pathtracer_shared.hlsli"
#include "layered_bsdf.hlsli"
#include "../include/restir_di.hlsli"
#include "../include/light_bvh.hlsli"

[shader("closesthit")]
void main(inout PathPayload payload, BuiltInTriangleIntersectionAttributes attr) {
    // InstanceID is meshId in the shared TLAS. InstanceIndex addresses a
    // pass-local table so two instances of one mesh retain distinct materials.
    StructuredBuffer<MaterialData> materials = ResourceDescriptorHeap[materialTableSrv];
    uint materialId = InstanceIndex();
    MaterialData mat = materials[materialId];
    ByteAddressBuffer vertices = ResourceDescriptorHeap[NonUniformResourceIndex(mat.vbSrv)];
    float3 weights = float3(1.0 - attr.barycentrics.x - attr.barycentrics.y, attr.barycentrics);
    float3 position = 0.0;
    float3 normal = 0.0;
    float2 uv = 0.0;
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
    }
    StructuredBuffer<PathDirectionalLight> sceneLights = ResourceDescriptorHeap[directionalLightsSrv];
    uint bvhConfigIndex = sceneLights[0].padding;
    if (restirMode != 0u) {
        RestirSurface surface;
        surface.position = P; surface.valid = 1u;
        surface.normal = N; surface.roughness = roughness;
        surface.view = V; surface.metallic = metallic;
        surface.albedo = albedo; surface.padding = 0.0;
        RWStructuredBuffer<RestirSurface> surfaces = ResourceDescriptorHeap[restirSurfaceUav];
        uint2 pixel = DispatchRaysIndex().xy;
        surfaces[pixel.y * restirWidth + pixel.x] = surface;
        payload.terminated = true;
        return;
    }
    if (restirEnabled != 0u && payload.bounceCount == 0u && bvhConfigIndex == 0u) {
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
        if (payload.bounceCount > 0u && any(emission > 0.0)) {
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
                value *= LightBVHBalanceWeight(float(neeSamples)*lightPdf,
                    PathLayeredPDF(N,V,L,roughness,metallic,mat));
            }
            float3 brdf = EvaluatePathLayered(N,V,L,albedo,roughness,metallic,mat);
            if (any(brdf > 0.0) && distance > .1) {
                ShadowPayload shadow; shadow.hit = true;
                RayDesc ray;
                ray.Origin = P+N*.05; ray.Direction = L;
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
                    value *= PathVolumeShadow(P,L,distance);
                    direct += brdf*value/float(neeSamples);
                    if (nrdEnabled != 0u)
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
            ray.Origin = P + N * 0.05;
            ray.Direction = shadowDirection;
            ray.TMin = 0.01;
            ray.TMax = 1e7;
            TraceRay(tScene, RAY_FLAG_FORCE_OPAQUE | RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH
                     | RAY_FLAG_SKIP_CLOSEST_HIT_SHADER, 0xff, 0, 0, 1, ray, shadow);
            if (!shadow.hit) {
                direct += EvaluatePathLayered(N, V, shadowDirection, albedo, roughness, metallic, mat)
                        * light.radiance * PathVolumeShadow(P,shadowDirection,1e7) / float(neeSamples);
                if (nrdEnabled != 0u)
                    diffuseDirect += NrdDiffuseBRDF(N,V,shadowDirection,albedo,metallic)
                                   * PathLayerTransmission(mat,N,V,shadowDirection)
                                   * light.radiance / float(neeSamples);
            }
        }
    }
    }
    }
    payload.radiance += payload.throughput * (emission + direct);
    if (nrdEnabled != 0u)
        NrdAddDiffuse(payload.bounceCount == 0u ? diffuseDirect
            : NrdFraction() * payload.throughput * (emission + direct));
    payload.bounceCount++;
    payload.terminated = true;
    if (!PathCanContinue(payload.bounceCount, maxBounces)) return;
    float3 weight;
    // @ref O'Neill 2014 "PCG: A Family of Simple Fast Space-Efficient Statistically Good Algorithms for Random Number Generation"
    // Per-sample dimension scrambling makes the BSDF lobe choice independent
    // of its angular variates. Fixed rotations of the same Halton base leave
    // a persistent (non-converging) correlation even with BVH disabled.
    uint dimension = HashPCG((32u + payload.bounceCount * 64u) ^ HashPCG(sampleIndex));
    float3 nextDirection = SamplePathLayered(N, V, albedo, roughness, metallic, dimension, mat, weight);
    if (nrdEnabled != 0u && payload.bounceCount == 1u) {
        RWTexture2D<float4> fraction = ResourceDescriptorHeap[nrdFractionUav];
        float3 full = EvaluatePathLayered(N,V,nextDirection,albedo,roughness,metallic,mat);
        fraction[DispatchRaysIndex().xy] = float4(
            NrdDiffuseBRDF(N,V,nextDirection,albedo,metallic)
                * PathLayerTransmission(mat,N,V,nextDirection) / max(full,1e-20), 1.0);
    }
    payload.previousPosition = P;
    payload.previousNormal = N;
    payload.previousBsdfPdf = PathLayeredPDF(N,V,nextDirection,roughness,metallic,mat);
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
    payload.nextOrigin = P + N * 0.05;
    payload.nextDirection = nextDirection;
    payload.terminated = false;
}
