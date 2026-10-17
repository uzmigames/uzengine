#include "pathtracer_shared.hlsli"

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
    // Every scene directional light gets its own shadow rays. No surface
    // bounce recursion in stage 1.
    StructuredBuffer<PathDirectionalLight> lights = ResourceDescriptorHeap[directionalLightsSrv];
    for (uint lightIndex = 0u; lightIndex < directionalLightCount; ++lightIndex) {
        PathDirectionalLight light = lights[lightIndex];
        float3 L = normalize(light.direction);
        float3 T, B;
        PathBasis(L, T, B);
        [unroll]
        for (uint sample = 0u; sample < 4u; ++sample) {
            float2 u = PathSample(sample + 1u + lightIndex * 4u);
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
            if (!shadow.hit)
                direct += EvaluatePBR(N, V, shadowDirection, albedo, roughness, metallic)
                        * light.radiance * 0.25;
        }
    }
    payload.radiance += payload.throughput * (emission + direct);
    float pdf;
    float3 nextDirection = SampleGGX(N, V, roughness, metallic, PathSample(5u), pdf);
    float3 brdfWeight = EvaluatePBR(N, V, nextDirection, albedo, roughness, metallic);
    payload.throughput *= brdfWeight / max(pdf, 1e-8);
    payload.bounceCount++;
    payload.terminated = true; // Next surface TraceRay belongs to stage 4.
}
