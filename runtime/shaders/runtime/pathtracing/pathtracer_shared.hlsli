#ifndef UZ_PIPELINE_PATHTRACER_SHARED
#define UZ_PIPELINE_PATHTRACER_SHARED

#include "../material/brdf.hlsli"
#ifdef UZ_PT_FROSTBITE_DIFFUSE
#include "../material/diffuse.hlsli"
#endif
#include "../common/sampling.hlsli"
#include "pathtracer_math.hlsli"
#include "pathtracer_volume.hlsli"
#include "nrd_signal.hlsli"

#ifndef RESTIR_COMPUTE
struct [raypayload] PathPayload {
    float3 radiance : read(caller, closesthit, miss) : write(caller, closesthit, miss);
    float3 throughput : read(caller, closesthit, miss) : write(caller, closesthit);
    uint bounceCount : read(caller, closesthit, miss) : write(caller, closesthit);
    uint hairBounces : read(caller, closesthit) : write(caller, closesthit);
    bool terminated : read(caller) : write(caller, closesthit, miss);
    float3 nextOrigin : read(caller) : write(closesthit);
    float3 nextDirection : read(caller) : write(closesthit);
    float3 previousPosition : read(caller, closesthit, miss) : write(caller, closesthit);
    float3 previousNormal : read(caller, closesthit, miss) : write(caller, closesthit);
    float previousBsdfPdf : read(caller, closesthit) : write(caller, closesthit);
};

struct [raypayload] ShadowPayload {
    bool hit : read(caller) : write(caller, miss);
};

RaytracingAccelerationStructure tScene : register(t0);
RWTexture2D<float4> uRadiance : register(u0);
#endif
SamplerState linearWrap : register(s0);

// Twenty-four DWORDs in the pass-private signature. The dedicated
// RT signature adds t0 without changing the graphics signature or any CB.
cbuffer PathTracerRoot : register(b0) {
    uint materialTableSrv;
    uint envMapSrv;
    uint sampleIndex;
    uint hasGeometry;
    uint directionalLightsSrv;
    uint directionalLightCount;
    uint accumulationUav;
    uint compensationUav;
    uint accumulatedFrames;
    uint maxBounces;
    uint rrEnabled;
    uint rrMinBounce;
    uint restirEnabled;
    uint restirMode;
    uint restirSurfaceUav;
    uint restirInitialUav;
    uint restirCurrentUav;
    uint restirPreviousUav;
    uint restirWidth;
    uint restirHeight;
    uint restirHistory;
    uint neeSamples;
    uint restirTemporal;
    uint restirLightCount;
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
    uint lightSourceIndex;
    float coatWeight;
    float coatRoughness;
    float coatIOR;
    float coatThickness;
    float3 coatSigmaA;
    uint stackDepth;
    float3 stackRoughness;
    float3 stackIOR;
    float3 stackThickness;
    float3 stackSigmaAR;
    float3 stackSigmaAG;
    float3 stackSigmaAB;
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

#ifndef RESTIR_COMPUTE
float2 PathSample(uint dimension) {
    uint2 pixel = DispatchRaysIndex().xy;
    uint seed = pixel.x * 1973u + pixel.y * 9277u + dimension * 26699u;
    seed = (seed ^ (seed >> 16u)) * 0x7feb352du;
    seed = (seed ^ (seed >> 15u)) * 0x846ca68bu;
    float2 rotation = float2(seed & 65535u, seed >> 16u) / 65536.0;
    return frac(float2(Halton(sampleIndex + 1u, 2u),
                       Halton(sampleIndex + 1u, 3u)) + rotation);
}
#endif

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
    float3 f0 = ComputeF0(albedo, metallic);
    float3 F = F_Schlick(f0, saturate(dot(V, H)));
    float alpha = RoughnessToAlpha(roughness);
    float3 specular = D_GGX(alpha, saturate(dot(N, H)))
                    * V_SmithGGXCorrelated(alpha, NoV, NoL) * F;
#ifdef UZ_PT_FROSTBITE_DIFFUSE
    // r.PathTracer.FrostbiteDiffuse: the raster deferred path's diffuse lobe
    // (deferred_lighting_ps.hlsl, diffColor = albedo * (1 - metallic)), so the
    // reference differs from raster in transport only. Every sampler divides
    // this value by its actual mixture PDF, so the estimators stay unbiased.
    return (EvalFrostbiteDiffuse((1.0 - metallic) * albedo, roughness, NoV, NoL,
                                 saturate(dot(V, H))) + specular) * NoL;
#else
    // Reuse the IBL LUT: its F0*A+B integrates this same correlated GGX lobe.
    // Clamp to texel centers because the existing material sampler wraps.
    Texture2D<float2> brdfLut = ResourceDescriptorHeap[brdfLutSrvIndex];
    uint lutWidth, lutHeight;
    brdfLut.GetDimensions(lutWidth, lutHeight);
    float2 halfTexel = 0.5 / float2(lutWidth, lutHeight);
    float2 ab = brdfLut.SampleLevel(linearWrap,
        clamp(float2(NoV, roughness), halfTexel, 1.0 - halfTexel), 0);
    float3 specularAlbedo = f0 * ab.x + ab.y;
    float3 diffuseEnergy = float3(PathDiffuseEnergy(specularAlbedo.r, NoV),
        PathDiffuseEnergy(specularAlbedo.g, NoV), PathDiffuseEnergy(specularAlbedo.b, NoV));
    return (diffuseEnergy * (1.0 - metallic) * albedo / PI + specular) * NoL;
#endif
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

// @ref Veach 1997 "Robust Monte Carlo Methods for Light Transport Simulation"
// @constants 0.5 = equal diffuse/specular mixture for dielectrics, 1 for metals.
// Evaluate BOTH densities at the sampled direction, not only the selected lobe.
#ifndef RESTIR_COMPUTE
float PathBRDFPDF(float3 N, float3 V, float3 L, float roughness, float metallic) {
    float NoL = saturate(dot(N,L));
    if (NoL <= 0.0) return 0.0;
    float3 H = normalize(V+L);
    float alpha = RoughnessToAlpha(roughness);
    float NoV = max(dot(N,V),1e-6);
    float specularPdf = D_GGX(alpha,saturate(dot(N,H)))
                      * G1_SmithGGX(alpha*alpha,NoV)/(4.0*NoV);
    float specularProbability = lerp(0.5,1.0,metallic);
    return specularProbability*specularPdf+(1.0-specularProbability)*NoL/PI;
}

float3 SamplePathBRDF(float3 N, float3 V, float3 albedo, float roughness,
                      float metallic, uint dimension, out float3 weight) {
    float specularProbability = lerp(0.5, 1.0, metallic);
    float2 u = PathSample(dimension);
    float pdf;
    float3 L;
    if (PathSample(dimension + 1u).x < specularProbability) {
        L = SampleGGX(N, V, roughness, metallic, u, pdf);
    } else {
        float3 T, B;
        PathBasis(N, T, B);
        float3 local = SampleCosineHemisphere(u, pdf);
        L = T * local.x + B * local.y + N * local.z;
    }
    float NoL = saturate(dot(N, L));
    if (NoL <= 0.0) { weight = 0.0; return L; }
    float3 H = normalize(V + L);
    float alpha = RoughnessToAlpha(roughness);
    float NoV = max(dot(N, V), 1e-6);
    float specularPdf = D_GGX(alpha, saturate(dot(N, H)))
                      * G1_SmithGGX(alpha * alpha, NoV) / (4.0 * NoV);
    pdf = specularProbability * specularPdf + (1.0 - specularProbability) * NoL / PI;
    weight = EvaluatePBR(N, V, L, albedo, roughness, metallic) / max(pdf, 1e-8);
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
#endif
