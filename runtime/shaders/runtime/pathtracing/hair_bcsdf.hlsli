#ifndef UZ_PATH_HAIR_BCSDF
#define UZ_PATH_HAIR_BCSDF

// Chiang et al. 2016, "A Practical and Controllable Hair and Fur Model for
// Production Path Tracing", sections 3-4. Densities use d(sin theta) d(phi),
// i.e. solid angle about the fiber axis; no surface-normal cosine belongs here.
#ifdef __cplusplus
#include <algorithm>
#include <cmath>
namespace uz {
using std::abs;
using std::acos;
using std::asin;
using std::atan2;
using std::cos;
using std::exp;
using std::floor;
using std::log;
using std::max;
using std::min;
using std::pow;
using std::sin;
using std::sqrt;
#endif

inline float HairVariance(float beta, int p) {
    beta = max(beta, 0.05f);
    float v = 0.726f*beta + 0.812f*beta*beta + 3.7f*pow(beta,20.0f);
    v *= v;
    return p == 1 ? 0.25f*v : (p == 2 ? 4.0f*v : v);
}

inline float HairLogI0(float x) {
    // Abramowitz & Stegun 9.8: scaled I0 avoids overflow in narrow lobes.
    if (x > 12.0f)
        return x - 0.5f*log(6.28318530718f*x)
             + log(1.0f + 0.125f/x + 0.0703125f/(x*x)
                          + 0.0732421875f/(x*x*x));
    float sum = 1.0f, term = 1.0f;
    for (int k = 1; k <= 24; ++k) {
        term *= x*x/(4.0f*float(k*k));
        sum += term;
    }
    return log(sum);
}

inline float HairMp(float sinI, float cosI, float sinO, float cosO, float v) {
    float a = cosI*cosO/v;
    float b = sinI*sinO/v;
    return exp(HairLogI0(a)-b-1.0f/v)
         / (v*(1.0f-exp(-2.0f/v)));
}

inline float HairSampleSinTheta(float sinO, float cosO, float v, float u, float w) {
    float c = 1.0f + v*log(max(u+(1.0f-u)*exp(-2.0f/v),1e-30f));
    return max(-1.0f,min(1.0f,-c*sinO
         + sqrt(max(0.0f,1.0f-c*c))*cos(6.28318530718f*w)*cosO));
}

inline float HairLogisticScale(float beta) {
    return 0.62665706866f*(0.265f*beta + 1.194f*beta*beta
                                       + 5.372f*pow(beta,22.0f));
}

inline float HairLogisticCDF(float x, float s) {
    return 1.0f/(1.0f+exp(-x/s));
}

inline float HairPhi(int p, float gammaO, float gammaT) {
    return 2.0f*float(p)*gammaT-2.0f*gammaO+float(p)*3.14159265359f;
}

inline float HairNp(float phi, int p, float s, float gammaO, float gammaT) {
    float x = phi-HairPhi(p,gammaO,gammaT);
    x -= 6.28318530718f*floor((x+3.14159265359f)/6.28318530718f);
    float e = exp(-abs(x)/s);
    float normalization = HairLogisticCDF(3.14159265359f,s)
                        - HairLogisticCDF(-3.14159265359f,s);
    return e/(s*(1.0f+e)*(1.0f+e)*normalization);
}

inline float HairLogisticSample(float u, float s) {
    float lo = HairLogisticCDF(-3.14159265359f,s);
    float hi = HairLogisticCDF(3.14159265359f,s);
    float c = lo+u*(hi-lo);
    return max(-3.14159265359f,min(3.14159265359f,
        s*log(max(c,1e-30f)/max(1.0f-c,1e-30f))));
}

inline float HairSigmaA(float color, float betaN) {
    // Chiang reflectance -> absorption fit (unit fiber radius).
    float b2 = betaN*betaN;
    float denominator = 5.969f-0.215f*betaN+2.532f*b2
                      -10.73f*b2*betaN+5.574f*b2*b2+0.245f*b2*b2*betaN;
    float x = log(max(color,1e-4f))/denominator;
    return x*x;
}

inline float HairFresnel(float cosine, float eta) {
    float ct = sqrt(max(0.0f,1.0f-(1.0f-cosine*cosine)/(eta*eta)));
    float rs = (cosine-eta*ct)/(cosine+eta*ct);
    float rp = (eta*cosine-ct)/(eta*cosine+ct);
    return 0.5f*(rs*rs+rp*rp);
}

inline float HairAp(int p, float fresnel, float transmission) {
    if (p == 0) return fresnel;
    float tt = (1.0f-fresnel)*(1.0f-fresnel)*transmission;
    return p == 1 ? tt : tt*fresnel*transmission;
}

inline float HairShift(int p, float alpha) {
    // Stage-1 rulebook convention: longitudinal peak shifts R=-a,
    // TT=a/2, TRT=-3a/2. Rotate outgoing theta by minus that shift.
    return p == 0 ? -alpha : (p == 1 ? 0.5f*alpha : -1.5f*alpha);
}

#ifdef __cplusplus
} // namespace uz
#else
struct HairMaterial {
    float3 sigmaA;
    float betaM;
    float betaN;
    float eta;
    float alpha;
    float h;
};

HairMaterial PathHairMaterial(float3 color, float betaM, float betaN, float h) {
    HairMaterial hair;
    hair.betaM = clamp(betaM,0.05,1.0);
    hair.betaN = clamp(betaN,0.05,1.0);
    hair.sigmaA = float3(HairSigmaA(color.r,hair.betaN),
                        HairSigmaA(color.g,hair.betaN),
                        HairSigmaA(color.b,hair.betaN));
    hair.eta = 1.55;
    hair.alpha = 0.03490658504; // two-degree cuticle tilt
    hair.h = clamp(h,-0.999,0.999);
    return hair;
}

void HairAttenuation(float sinO, float cosO, HairMaterial hair,
                     out float3 attenuation[3], out float3 probabilities,
                     out float gammaO, out float gammaT) {
    gammaO = asin(hair.h);
    float etaPrime = sqrt(hair.eta*hair.eta-sinO*sinO)/max(cosO,1e-6);
    gammaT = asin(hair.h/etaPrime);
    float cosThetaT = sqrt(1.0-sinO*sinO/(hair.eta*hair.eta));
    float3 transmission = exp(-hair.sigmaA*(2.0*cos(gammaT)/cosThetaT));
    float f = HairFresnel(cosO*cos(gammaO),hair.eta);
    [unroll] for (int p = 0; p < 3; ++p)
        attenuation[p] = float3(HairAp(p,f,transmission.r),
            HairAp(p,f,transmission.g),HairAp(p,f,transmission.b));
    probabilities = float3(dot(attenuation[0],float3(.2126,.7152,.0722)),
        dot(attenuation[1],float3(.2126,.7152,.0722)),
        dot(attenuation[2],float3(.2126,.7152,.0722)));
    probabilities /= max(dot(probabilities,1.0),1e-20);
}

void HairRotateOutgoing(float sinO, float cosO, int p, float alpha,
                         out float shiftedSin, out float shiftedCos) {
    float shift = HairShift(p,alpha);
    shiftedSin = sinO*cos(shift)-cosO*sin(shift);
    shiftedCos = abs(cosO*cos(shift)+sinO*sin(shift));
}

float3 EvaluateHairBCSDF(float3 V, float3 L, float3 tangent, float3 radial,
                         HairMaterial hair, out float pdf) {
    float3 B = cross(tangent,radial);
    float sinO = clamp(dot(V,tangent),-1.0,1.0);
    float sinI = clamp(dot(L,tangent),-1.0,1.0);
    float cosO = sqrt(max(0.0,1.0-sinO*sinO));
    float cosI = sqrt(max(0.0,1.0-sinI*sinI));
    float phi = atan2(dot(L,B),dot(L,radial))-atan2(dot(V,B),dot(V,radial));
    float3 a[3], probabilities;
    float gammaO,gammaT;
    HairAttenuation(sinO,cosO,hair,a,probabilities,gammaO,gammaT);
    float3 value = 0.0;
    pdf = 0.0;
    [unroll] for (int p = 0; p < 3; ++p) {
        float so,co;
        HairRotateOutgoing(sinO,cosO,p,hair.alpha,so,co);
        float density = HairMp(sinI,cosI,so,co,HairVariance(hair.betaM,p))
                      * HairNp(phi,p,HairLogisticScale(hair.betaN),gammaO,gammaT);
        value += a[p]*density;
        pdf += probabilities[p]*density;
    }
    return value;
}

float3 SampleHairBCSDF(float3 V, float3 tangent, float3 radial,
                       HairMaterial hair, uint dimension, out float3 weight) {
    float sinO = clamp(dot(V,tangent),-1.0,1.0);
    float cosO = sqrt(max(0.0,1.0-sinO*sinO));
    float3 a[3], probabilities;
    float gammaO,gammaT;
    HairAttenuation(sinO,cosO,hair,a,probabilities,gammaO,gammaT);
    float2 choose = PathSample(dimension+1u);
    int p = choose.x < probabilities.x ? 0
          : (choose.x < probabilities.x+probabilities.y ? 1 : 2);
    float so,co;
    HairRotateOutgoing(sinO,cosO,p,hair.alpha,so,co);
    float2 u = PathSample(dimension);
    float sinI = HairSampleSinTheta(so,co,HairVariance(hair.betaM,p),u.x,u.y);
    float cosI = sqrt(max(0.0,1.0-sinI*sinI));
    float3 B = cross(tangent,radial);
    float phiO = atan2(dot(V,B),dot(V,radial));
    float phi = phiO+HairPhi(p,gammaO,gammaT)
              + HairLogisticSample(choose.y,HairLogisticScale(hair.betaN));
    float3 L = sinI*tangent+cosI*(cos(phi)*radial+sin(phi)*B);
    float pdf;
    float3 value = EvaluateHairBCSDF(V,L,tangent,radial,hair,pdf);
    weight = value/max(pdf,1e-20);
    return L;
}

bool PathIsHair(MaterialData mat) {
    return ((mat.materialFlags >> 24u) & 15u) == 6u;
}

// Stage-1 authoring reuses the existing material table: RGB is reflectance,
// roughnessMultiplier is beta_m and metallicMultiplier is beta_n for Hair only.
// The public Hair tag is already supported by uz.scene.set_material.
float3 EvaluatePathSurface(float3 N, float3 V, float3 L, float3 color,
    float roughness, float metallic, MaterialData mat, float3 tangent, float h) {
    if (!PathIsHair(mat)) return EvaluatePathLayered(N,V,L,color,roughness,metallic,mat);
    float pdf;
    return EvaluateHairBCSDF(V,L,tangent,N,
        PathHairMaterial(color,roughness,metallic,h),pdf);
}

float PathSurfacePDF(float3 N, float3 V, float3 L, float roughness,
    float metallic, MaterialData mat, float3 color, float3 tangent, float h) {
    if (!PathIsHair(mat)) return PathLayeredPDF(N,V,L,roughness,metallic,mat);
    float pdf;
    EvaluateHairBCSDF(V,L,tangent,N,PathHairMaterial(color,roughness,metallic,h),pdf);
    return pdf;
}

float3 SamplePathSurface(float3 N, float3 V, float3 color, float roughness,
    float metallic, uint dimension, MaterialData mat, float3 tangent, float h,
    out float3 weight) {
    if (!PathIsHair(mat))
        return SamplePathLayered(N,V,color,roughness,metallic,dimension,mat,weight);
    return SampleHairBCSDF(V,tangent,N,
        PathHairMaterial(color,roughness,metallic,h),dimension,weight);
}
#endif
#endif
