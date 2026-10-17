#ifndef UZ_PATHTRACER_MATH
#define UZ_PATHTRACER_MATH

// Scalar transport math shared with the CPU regression tests.
#ifdef __cplusplus
#include <cstdint>
namespace uz {
using uint = uint32_t;
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

#undef PT_INOUT_FLOAT
#undef PT_PRECISE
#ifdef __cplusplus
} // namespace uz
#endif
#endif
