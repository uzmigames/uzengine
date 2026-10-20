#ifndef UZ_PATH_LAYERED_STACK
#define UZ_PATH_LAYERED_STACK

#ifdef __cplusplus
#include "pathtracer_math.hlsli"
namespace uz {
#endif

// Walter et al. 2007: a rough dielectric boundary between ADJACENT media.
// Negative transmitted cosine denotes TIR, not an evanescent transmitted ray.
inline float PathStackCosT(float cosI, float etaI, float etaT) {
    float eta = etaI / etaT;
    float c2 = 1.0f - eta*eta*(1.0f-cosI*cosI);
    return c2 <= 0.0f ? -1.0f : sqrt(c2);
}

inline float PathStackFresnel(float cosI, float etaI, float etaT) {
    if (etaI == etaT) return 0.0f;
    float cosT = PathStackCosT(cosI,etaI,etaT);
    if (cosT < 0.0f) return 1.0f;
    float rs = (etaI*cosI-etaT*cosT)/(etaI*cosI+etaT*cosT);
    float rp = (etaT*cosI-etaI*cosT)/(etaT*cosI+etaI*cosT);
    return 0.5f*(rs*rs+rp*rp);
}

// d omega_t / d omega_i at a FIXED facet. Together with the radiance
// eta^2 factor this preserves projected etendue n^2 cos(theta) d omega.
inline float PathStackDirectionJacobian(float cosI, float cosT, float etaI, float etaT) {
    float eta = etaI/etaT;
    return eta*eta*cosI/cosT;
}

// d omega_h / d omega_t for a sampled rough facet. VoH > 0, LoH < 0.
inline float PathStackHalfJacobian(float VoH, float LoH, float etaI, float etaT) {
    float denominator = etaI*VoH+etaT*LoH;
    return etaT*etaT*(-LoH)/(denominator*denominator);
}

inline float PathStackG1(float alpha, float cosine) {
    if (cosine <= 0.0f) return 0.0f;
    return 2.0f*cosine/(cosine+sqrt(alpha*alpha+(1.0f-alpha*alpha)*cosine*cosine));
}

inline float PathStackD(float alpha, float NoH) {
    float d = (NoH*alpha*alpha-NoH)*NoH+1.0f;
    return alpha*alpha/(3.14159265359f*d*d);
}

// f*cos / p for VNDF sampling, including Fresnel branch probability.
// Separable Smith masking makes the non-eta part <= 1. Over an external
// reflection path ALL eta^2 factors telescope to 1, even after TIR loops.
inline float PathStackEventWeight(float alpha, float cosO, float etaI, float etaT,
                                   bool transmission) {
    float eta = transmission ? etaI/etaT : 1.0f;
    return PathStackG1(alpha,cosO)*eta*eta;
}

inline bool PathStackEnabled(unsigned int depth, float weight) {
    // Deliberate compatibility model: depth 0/1 is the shipped stage-1/2 coat.
    return depth >= 2u && weight > 0.0f;
}

#ifdef __cplusplus
} // namespace uz
#else

// Position-free plane-parallel random walk, not independent air/coat lobes.
// All orders of internal reflection are retained with unbiased roulette.
// The random walk has a path-space PDF, NOT an analytic marginal direction
// PDF. Analytic-light NEE uses the last-exit estimator below. Area emitters
// and environment use continuation only, without a fabricated MIS density.
bool PathHasStack(MaterialData mat) {
    return PathStackEnabled(mat.stackDepth,mat.coatWeight)
        && ((mat.materialFlags >> 24u) & 15u) != 6u;
}

float PathStackIOR(MaterialData mat, int medium) {
    if (medium == 0) return 1.0;
    return medium == 1 ? mat.coatIOR : mat.stackIOR[medium-2];
}
float PathStackRoughness(MaterialData mat, int layer) {
    return layer == 0 ? mat.coatRoughness : mat.stackRoughness[layer-1];
}
float3 PathStackAttenuation(MaterialData mat, int layer, float cosine) {
    float thickness = layer == 0 ? mat.coatThickness : mat.stackThickness[layer-1];
    float3 sigma = layer == 0 ? mat.coatSigmaA
        : float3(mat.stackSigmaAR[layer-1],mat.stackSigmaAG[layer-1],mat.stackSigmaAB[layer-1]);
    return exp(-sigma*(0.5*thickness/max(abs(cosine),1e-20)));
}

float PathStackRandom(inout uint rng) {
    rng = HashPCG(rng);
    return float(rng >> 8u)/16777216.0;
}

float3 PathStackSampleBoundary(float3 N, float3 V, float roughness,
    float etaI, float etaT, inout uint rng, out float weight) {
    weight = 1.0;
    if (etaI == etaT) return -V; // matched medium is a delta identity, even if rough
    float3 T,B;
    PathBasis(N,T,B);
    float alpha = RoughnessToAlpha(roughness);
    float2 u = float2(PathStackRandom(rng),PathStackRandom(rng));
    float3 localV = float3(dot(V,T),dot(V,B),dot(V,N));
    float3 localH = SampleGGX_VNDF(alpha,localV,u);
    float3 H = T*localH.x+B*localH.y+N*localH.z;
    float VoH = dot(V,H);
    float F = PathStackFresnel(VoH,etaI,etaT);
    bool transmit = PathStackRandom(rng) >= F;
    float3 L;
    if (transmit) {
        float eta = etaI/etaT;
        float cosT = PathStackCosT(VoH,etaI,etaT);
        L = -eta*V+(eta*VoH-cosT)*H;
    } else {
        L = reflect(-V,H);
    }
    float cosine = dot(N,L)*(transmit ? -1.0 : 1.0);
    if (cosine <= 0.0) { weight = 0.0; return L; }
    // p_h = D*G1(V)*VoH/NoV; p_L = p_h*F/(4 VoH) for R,
    // p_h*(1-F)*PathStackHalfJacobian(...) for T.
    weight = PathStackEventWeight(alpha,cosine,etaI,etaT,transmit);
    return L;
}

float PathStackBoundaryValue(float3 N, float3 V, float3 L, float roughness,
                             float etaI, float etaT) {
    if (etaI == etaT) return 0.0; // handled by skipping identity interfaces
    float NoV = dot(N,V), NoL = dot(N,L);
    if (NoV <= 0.0 || NoL == 0.0) return 0.0;
    bool transmit = NoL < 0.0;
    float3 sum = transmit ? etaI*V+etaT*L : V+L;
    if (dot(sum,sum) == 0.0) return 0.0;
    float3 H = normalize(sum);
    if (dot(H,N) < 0.0) H = -H;
    float VoH = dot(V,H), LoH = dot(L,H);
    if (VoH <= 0.0 || (transmit ? LoH >= 0.0 : LoH <= 0.0)) return 0.0;
    float alpha = RoughnessToAlpha(roughness);
    float F = PathStackFresnel(VoH,etaI,etaT);
    float pdfH = PathStackD(alpha,dot(N,H))*PathStackG1(alpha,NoV)*VoH/NoV;
    float pdf = transmit ? pdfH*(1.0-F)*PathStackHalfJacobian(VoH,LoH,etaI,etaT)
                         : pdfH*F/(4.0*VoH);
    return pdf*PathStackEventWeight(alpha,abs(NoL),etaI,etaT,transmit);
}

float3 PathStackSampleBase(float3 N, float3 V, float3 albedo, float roughness,
    float metallic, inout uint rng, out float3 weight) {
    float2 u = float2(PathStackRandom(rng),PathStackRandom(rng));
    float pdf;
    float3 L;
    if (PathStackRandom(rng) < lerp(0.5,1.0,metallic)) {
        L = SampleGGX(N,V,roughness,metallic,u,pdf);
    } else {
        float3 T,B; PathBasis(N,T,B);
        float3 local = SampleCosineHemisphere(u,pdf);
        L = T*local.x+B*local.y+N*local.z;
    }
    pdf = PathBRDFPDF(N,V,L,roughness,metallic);
    weight = pdf > 0.0 ? EvaluatePBR(N,V,L,albedo,roughness,metallic)/pdf : 0.0;
    return L;
}

// evaluation=true: unbiased last-exit NEE for a prescribed external L.
// evaluation=false: sample one complete path and return its importance weight.
float3 PathStackWalk(float3 N, float3 V, float3 L, float3 albedo,
    float roughness, float metallic, MaterialData mat, inout uint rng,
    bool evaluation, out float3 weight) {
    int count = int(mat.stackDepth);
    int top = 0;
    // Matched-air films before the first real boundary carry only absorption.
    while (top < count && PathStackIOR(mat,top) == PathStackIOR(mat,top+1)) top++;
    float3 beta = 1.0;
    float3 exitAttenuation = 1.0;
    for (int layer = 0; layer < top; ++layer) {
        beta *= PathStackAttenuation(mat,layer,dot(N,V));
        if (evaluation) exitAttenuation *= PathStackAttenuation(mat,layer,dot(N,L));
    }
    float3 value = 0.0;
    if (evaluation) {
        if (dot(N,L) <= 0.0) { weight = 0.0; return 0.0; }
        value = top == count ? beta*EvaluatePBR(N,V,L,albedo,roughness,metallic)
            : beta*PathStackBoundaryValue(N,V,L,PathStackRoughness(mat,top),
                PathStackIOR(mat,top),PathStackIOR(mat,top+1));
        value *= exitAttenuation;
    }
    int vertex = top;
    float3 direction = -V;
    uint steps = 0u;
    [loop]
    while (true) {
        if (evaluation && vertex == top && dot(N,direction) > 0.0 && top < count) {
            value += beta*exitAttenuation*PathStackBoundaryValue(-N,-direction,L,
                PathStackRoughness(mat,top),PathStackIOR(mat,top+1),PathStackIOR(mat,top));
        }
        float3 eventWeight;
        if (vertex == count) {
            direction = PathStackSampleBase(N,-direction,albedo,roughness,metallic,rng,eventWeight);
        } else {
            bool down = dot(N,direction) < 0.0;
            float etaI = PathStackIOR(mat,vertex+(down ? 0 : 1));
            float etaT = PathStackIOR(mat,vertex+(down ? 1 : 0));
            float w;
            direction = PathStackSampleBoundary(down ? N : -N,-direction,
                PathStackRoughness(mat,vertex),etaI,etaT,rng,w);
            eventWeight = w;
        }
        beta *= eventWeight;
        if (!any(beta > 0.0)) break;
        bool down = dot(N,direction) < 0.0;
        if (!down && vertex == top) {
            if (!evaluation) {
                for (int layer = 0; layer < top; ++layer)
                    beta *= PathStackAttenuation(mat,layer,dot(N,direction));
                weight = beta;
                return direction;
            }
            break;
        }
        int layer = down ? vertex : vertex-1;
        beta *= PathStackAttenuation(mat,layer,dot(N,direction));
        vertex += down ? 1 : -1;
        // No fixed scattering-order cutoff: roulette estimates the infinite
        // Neumann series, including trapped/TIR paths, without truncation bias.
        if (++steps >= 8u) {
            if (PathStackRandom(rng) >= 0.75) break;
            beta /= 0.75;
        }
    }
    weight = 0.0;
    return evaluation ? value : N;
}

uint PathStackSeed(uint dimension) {
    uint2 pixel = DispatchRaysIndex().xy;
    return HashPCG(pixel.x+1973u*pixel.y+HashPCG(sampleIndex+1u)+dimension);
}

float3 EvaluatePathStack(float3 N, float3 V, float3 L, float3 albedo,
    float roughness, float metallic, MaterialData mat) {
    uint rng = PathStackSeed(0x7e35a9bdu);
    float3 unused;
    float3 value = PathStackWalk(N,V,L,albedo,roughness,metallic,mat,rng,true,unused);
    return mat.coatWeight*value+(1.0-mat.coatWeight)*EvaluatePBR(N,V,L,albedo,roughness,metallic);
}

float3 SamplePathStack(float3 N, float3 V, float3 albedo, float roughness,
    float metallic, uint dimension, MaterialData mat, out float3 weight) {
    uint rng = PathStackSeed(dimension);
    if (PathStackRandom(rng) >= mat.coatWeight)
        return PathStackSampleBase(N,V,albedo,roughness,metallic,rng,weight);
    return PathStackWalk(N,V,N,albedo,roughness,metallic,mat,rng,false,weight);
}
#endif
#endif
