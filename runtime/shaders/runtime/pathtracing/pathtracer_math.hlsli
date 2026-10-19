#ifndef UZ_PATHTRACER_MATH
#define UZ_PATHTRACER_MATH

// Scalar transport math shared with the CPU regression tests.
#ifdef __cplusplus
#include <cstdint>
#include <cmath>
namespace uz {
using uint = uint32_t;
using std::exp;
using std::log;
using std::sqrt;
#define PT_INOUT_FLOAT float&
#define PT_PRECISE
#else
#define PT_INOUT_FLOAT inout float
#define PT_PRECISE precise
#endif

// @ref Kahan 1965 "Pracniques: Further Remarks on Reducing Truncation Errors"
// @constants 1 = one new sample; count excludes that sample.
inline float PathAccumulate(float value, float previous, PT_INOUT_FLOAT correction, uint count) {
    if (count == 0u) { correction = 0.0f; return value; }
    PT_PRECISE float increment = (value - previous) / float(count + 1u) - correction;
    PT_PRECISE float mean = previous + increment;
    correction = (mean - previous) - increment;
    return mean;
}

// @ref Veach 1997 "Robust Monte Carlo Methods for Light Transport Simulation"
// @constants 0.05 = minimum survival; 1 = certain survival.
inline float PathSurvivalProbability(float peakThroughput) {
    return peakThroughput < 0.05f ? 0.05f : (peakThroughput > 1.0f ? 1.0f : peakThroughput);
}

inline bool PathCanContinue(uint completedBounces, uint maxBounces) {
    return completedBounces < maxBounces;
}

/// @ref Karis 2013 "Real Shading in Unreal Engine 4" - integrated environment BRDF.
/// @constants 0.005 = conservative absolute albedo reserve for the existing
/// 1024-sample RG16F LUT and bilinear filtering; checked by the furnace sweep.
/// 0.001 = minimum NoV in brdf_lut_cs.hlsl. Below the baked domain, extend
/// the specular budget toward its conservative upper bound 1 at the horizon.
/// The Lambert lobe integrates to this remaining directional energy budget.
inline float PathDiffuseEnergy(float specularAlbedo, float NoV) {
    float remaining = 1.0f - specularAlbedo - 0.005f;
    float grazing = NoV < 0.001f ? NoV / 0.001f : 1.0f;
    return remaining < 0.0f ? 0.0f : remaining * grazing;
}

// @ref Schlick 1994 "An Inexpensive BRDF Model for Physically-Based Rendering"
// Air-to-coat interface; the CVar boundary restricts IOR to [1,3].
inline float PathCoatFresnel(float cosine, float ior) {
    if (ior == 1.0f) return 0.0f;
    float r = (ior - 1.0f) / (ior + 1.0f);
    float x = 1.0f - cosine;
    return r*r + (1.0f-r*r)*x*x*x*x*x;
}

// Both interface crossings attenuate the existing base; neither amplifies it.
inline float PathCoatTransmission(float weight, float NoV, float NoL, float ior) {
    if (weight == 0.0f) return 1.0f;
    return (1.0f-weight*PathCoatFresnel(NoV,ior))
         * (1.0f-weight*PathCoatFresnel(NoL,ior));
}

// @ref Heitz 2014 "Understanding the Masking-Shadowing Function"
// GGX dielectric coat, f*cos(theta), using the engine's correlated visibility.
inline float PathCoatReflection(float weight, float roughness, float ior,
                                float NoV, float NoL, float NoH, float VoH) {
    if (weight == 0.0f || NoV <= 0.0f || NoL <= 0.0f) return 0.0f;
    float alpha = roughness*roughness;
    if (alpha < 0.0064f) alpha = 0.0064f; // engine kMinGGXAlpha
    float a2 = alpha*alpha;
    float d = (NoH*a2-NoH)*NoH+1.0f;
    float visibilityDenominator = NoL*sqrt(NoV*NoV*(1.0f-a2)+a2)
                               + NoV*sqrt(NoL*NoL*(1.0f-a2)+a2);
    if (visibilityDenominator < 1e-7f) visibilityDenominator = 1e-7f;
    return weight*PathCoatFresnel(VoH,ior)*a2/(3.14159265359f*d*d)
         * 0.5f/visibilityDenominator*NoL;
}

inline float PathCoatCompose(float base, float coat, float transmission) {
    if (transmission == 1.0f && coat == 0.0f) return base;
    return coat + transmission*base;
}

inline float PathCoatMixturePDF(float probability, float coatPDF, float basePDF) {
    if (probability == 0.0f) return basePDF;
    return probability*coatPDF+(1.0f-probability)*basePDF;
}

inline float PathVolumeTransmittance(float sigmaT, float distance) {
    return exp(-sigmaT * distance);
}

// Inverse CDF of the untruncated exponential. The surface-survival mass
// is exp(-sigmaT*d); do not apply Beer-Lambert a second time after sampling.
inline float PathVolumeDistance(float sigmaT, float u) {
    return sigmaT > 0.0f ? -log(1.0f - u) / sigmaT : 1e30f;
}

inline float PathVolumeDistancePDF(float sigmaT, float distance) {
    return sigmaT * PathVolumeTransmittance(sigmaT, distance);
}

inline float PathVolumePhase(float cosine, float g) {
    float denominator = 1.0f + g*g - 2.0f*g*cosine;
    return (1.0f - g*g) / (12.566370614359f * denominator * sqrt(denominator));
}

inline float PathVolumePhaseCosine(float u, float g) {
    // Algebraic inverse CDF without cancellation or a near-isotropic
    // approximation: this is also exact at g=0.
    float s = 2.0f*u-1.0f;
    float denominator = 1.0f+g*s;
    return (2.0f*s+g*(s*s+3.0f)+2.0f*g*g*s+g*g*g*(s*s-1.0f))
         / (2.0f*denominator*denominator);
}

#undef PT_INOUT_FLOAT
#undef PT_PRECISE
#ifdef __cplusplus
} // namespace uz
#endif
#endif
