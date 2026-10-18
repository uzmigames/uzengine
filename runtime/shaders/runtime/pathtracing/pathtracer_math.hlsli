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
