#ifndef UZ_PATH_LAYERED_BSDF
#define UZ_PATH_LAYERED_BSDF

#ifdef __cplusplus
#include "pathtracer_math.hlsli"
namespace uz {
#endif

// Real-time clearcoat approximation, not refracted base/stack transport.
// Thickness is the normal-incidence round-trip optical distance (centimeters).
// Snell gives cos(theta_t) = sqrt(1 - (1-cos(theta)^2)/IOR^2).
inline float PathCoatPathLength(float thickness, float NoV, float NoL, float ior) {
    // Rearranged to avoid cancellation at IOR 1 and grazing incidence.
    float cosV = sqrt((ior*ior-1.0f+NoV*NoV)/(ior*ior));
    float cosL = sqrt((ior*ior-1.0f+NoL*NoL)/(ior*ior));
    // Only the matched-air grazing limit has a zero refracted cosine.
    cosV = cosV < 1e-6f ? 1e-6f : cosV;
    cosL = cosL < 1e-6f ? 1e-6f : cosL;
    return thickness*0.5f*(1.0f/cosV+1.0f/cosL);
}

inline float PathCoatAbsorption(float sigmaA, float pathLength) {
    if (sigmaA == 0.0f || pathLength == 0.0f) return 1.0f;
    return exp(-sigmaA*pathLength);
}

#ifdef __cplusplus
} // namespace uz
#else
// Included after pathtracer_shared: extend, never replace, its base BRDF.
float3 PathLayerTransmission(MaterialData mat, float3 N, float3 V, float3 L) {
    float transmission = PathCoatTransmission(mat.coatWeight,saturate(dot(N,V)),
                                              saturate(dot(N,L)),mat.coatIOR);
    if (mat.coatWeight == 0.0 || mat.coatThickness == 0.0 || all(mat.coatSigmaA == 0.0))
        return transmission;
    float pathLength = PathCoatPathLength(mat.coatThickness,saturate(dot(N,V)),
                                         saturate(dot(N,L)),mat.coatIOR);
    return transmission*float3(PathCoatAbsorption(mat.coatSigmaA.x,pathLength),
        PathCoatAbsorption(mat.coatSigmaA.y,pathLength),
        PathCoatAbsorption(mat.coatSigmaA.z,pathLength));
}

float3 EvaluatePathLayered(float3 N, float3 V, float3 L, float3 albedo,
                           float roughness, float metallic, MaterialData mat) {
    float3 base = EvaluatePBR(N,V,L,albedo,roughness,metallic);
    if (mat.coatWeight == 0.0) return base;
    float3 H = normalize(V+L);
    float coat = PathCoatReflection(mat.coatWeight,mat.coatRoughness,mat.coatIOR,
        saturate(dot(N,V)),saturate(dot(N,L)),saturate(dot(N,H)),saturate(dot(V,H)));
    float3 transmission = PathLayerTransmission(mat,N,V,L);
    return float3(PathCoatCompose(base.x,coat,transmission.x),
                  PathCoatCompose(base.y,coat,transmission.y),
                  PathCoatCompose(base.z,coat,transmission.z));
}

float PathLayeredPDF(float3 N, float3 V, float3 L, float roughness,
                     float metallic, MaterialData mat) {
    float basePDF = PathBRDFPDF(N,V,L,roughness,metallic);
    if (mat.coatWeight == 0.0) return basePDF;
    if (dot(N,L) <= 0.0) return 0.0;
    float3 H = normalize(V+L);
    float NoV = max(dot(N,V),1e-6);
    float alpha = RoughnessToAlpha(mat.coatRoughness);
    float coatPDF = D_GGX(alpha,saturate(dot(N,H)))
                  * G1_SmithGGX(alpha*alpha,NoV)/(4.0*NoV);
    float probability = mat.coatWeight*PathCoatFresnel(saturate(dot(N,V)),mat.coatIOR);
    return PathCoatMixturePDF(probability,coatPDF,basePDF);
}

float3 SamplePathLayered(float3 N, float3 V, float3 albedo, float roughness,
                         float metallic, uint dimension, MaterialData mat,
                         out float3 weight) {
    // Do not consume a random dimension or recompute a weight at zero coat.
    if (mat.coatWeight == 0.0)
        return SamplePathBRDF(N,V,albedo,roughness,metallic,dimension,weight);
    float probability = mat.coatWeight*PathCoatFresnel(saturate(dot(N,V)),mat.coatIOR);
    float3 L;
    if (PathSample(dimension+3u).x < probability) {
        float pdf;
        L = SampleGGX(N,V,mat.coatRoughness,0.0,PathSample(dimension),pdf);
    } else {
        L = SamplePathBRDF(N,V,albedo,roughness,metallic,dimension,weight);
    }
    float pdf = PathLayeredPDF(N,V,L,roughness,metallic,mat);
    weight = EvaluatePathLayered(N,V,L,albedo,roughness,metallic,mat)/max(pdf,1e-8);
    return L;
}
#endif
#endif
