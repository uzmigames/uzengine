#ifndef UZ_PIPELINE_PATHTRACER_SHARED
#define UZ_PIPELINE_PATHTRACER_SHARED

#include "../material/brdf.hlsli"
#include "../common/sampling.hlsli"

struct [raypayload] PathPayload {
    float3 radiance : read(caller, closesthit, miss) : write(caller, closesthit, miss);
    float3 throughput : read(caller, closesthit, miss) : write(caller, closesthit);
    uint bounceCount : read(caller, closesthit) : write(caller, closesthit);
    bool terminated : read(caller) : write(caller, closesthit, miss);
};

struct [raypayload] ShadowPayload {
    bool hit : read(caller) : write(caller, miss);
};

RaytracingAccelerationStructure tScene : register(t0);
RWTexture2D<float4> uRadiance : register(u0);
SamplerState linearWrap : register(s0);

// Six DWORDs, below the shared graphics/RT ceiling of 24. The dedicated
// RT signature adds t0 without changing the graphics signature or any CB.
cbuffer PathTracerRoot : register(b0) {
    uint materialTableSrv;
    uint envMapSrv;
    uint sampleIndex;
    uint hasGeometry;
    uint directionalLightsSrv;
    uint directionalLightCount;
};

// Prefix of the existing PerFrameCB, including its inverse camera matrix.
cbuffer PerFrameCB : register(b1) {
    row_major float4x4 viewProj;
    float4 cameraPos;
    float4 timeParams;
    float4 lightDir;
    float4 lightColor;
    float4 ambientColor;
    row_major float4x4 invViewProj;
    float nearPlane;
    float farPlane;
    uint irradianceSrvIndex;
    uint prefilteredSrvIndex;
    uint brdfLutSrvIndex;
    uint prefilteredMipLevels;
    float skyLightIntensity;
    float skyLightColorR;
    float skyLightColorG;
    float skyLightColorB;
};

// Mirrors PipelinePathMaterial in pipeline_path_tracer_pass.h.
struct MaterialData {
    float3 tint; uint albedoSrv;
    uint vbSrv; uint ibSrv; uint vertexStride; uint uvOffset;
    uint normalOffset; uint indexFormat; uint normalFormat; uint roughnessSrv;
    uint metallicSrv; uint materialFlags; float roughnessMultiplier; float metallicMultiplier;
    float2 uvScale; float2 uvOffsetValue;
    float3 emission; uint emissiveSrv;
};

struct PathDirectionalLight {
    float3 direction; float angularRadius;
    float3 radiance; uint padding;
};

float Halton(uint index, uint base) {
    float result = 0.0;
    float scale = 1.0;
    while (index != 0u) {
        scale /= base;
        result += scale * (index % base);
        index /= base;
    }
    return result;
}

float2 PathSample(uint dimension) {
    uint2 pixel = DispatchRaysIndex().xy;
    uint seed = pixel.x * 1973u + pixel.y * 9277u + dimension * 26699u;
    seed = (seed ^ (seed >> 16u)) * 0x7feb352du;
    seed = (seed ^ (seed >> 15u)) * 0x846ca68bu;
    float2 rotation = float2(seed & 65535u, seed >> 16u) / 65536.0;
    return frac(float2(Halton(sampleIndex + 1u, 2u),
                       Halton(sampleIndex + 1u, 3u)) + rotation);
}

void PathBasis(float3 N, out float3 T, out float3 B) {
    T = normalize(cross(abs(N.z) < 0.999 ? float3(0, 0, 1) : float3(0, 1, 0), N));
    B = cross(N, T);
}

// The engine exposes the components in brdf.hlsli rather than a function
// named EvaluatePBR. Return f * cos(theta) for direct light and path weights.
float3 EvaluatePBR(float3 N, float3 V, float3 L, float3 albedo,
                   float roughness, float metallic) {
    float NoL = saturate(dot(N, L));
    float NoV = saturate(dot(N, V));
    if (NoL <= 0.0 || NoV <= 0.0) return 0.0;
    float3 H = normalize(V + L);
    float3 F = F_Schlick(ComputeF0(albedo, metallic), saturate(dot(V, H)));
    float alpha = RoughnessToAlpha(roughness);
    float3 specular = D_GGX(alpha, saturate(dot(N, H)))
                    * V_SmithGGXCorrelated(alpha, NoV, NoL) * F;
    return ((1.0 - F) * (1.0 - metallic) * albedo / PI + specular) * NoL;
}

// Adapter to the engine's SampleGGX_VNDF (common/sampling.hlsli); the PDF
// is the VNDF reflection PDF. Metallic affects the BRDF, not its GGX NDF.
float3 SampleGGX(float3 N, float3 V, float roughness, float metallic,
                 float2 u, out float pdf) {
    float3 T, B;
    PathBasis(N, T, B);
    float3 localV = float3(dot(V, T), dot(V, B), max(dot(V, N), 1e-6));
    float alpha = RoughnessToAlpha(roughness);
    float3 localH = SampleGGX_VNDF(alpha, localV, u);
    float3 H = normalize(T * localH.x + B * localH.y + N * localH.z);
    float3 L = reflect(-V, H);
    pdf = D_GGX(alpha, saturate(dot(N, H)))
        * G1_SmithGGX(alpha * alpha, localV.z) / (4.0 * localV.z);
    return L;
}

float3 PathEnvironment(float3 direction) {
    float3 tint = float3(skyLightColorR, skyLightColorG, skyLightColorB) * skyLightIntensity;
    if (envMapSrv == 0xffffffffu) return tint;
    TextureCube<float4> tEnvMap = ResourceDescriptorHeap[envMapSrv];
    // Engine world is Z up; its cubemaps use Y up.
    return tEnvMap.SampleLevel(linearWrap, direction.xzy, 0).rgb * tint;
}
#endif
