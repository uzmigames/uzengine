#ifndef UZ_RESTIR_MATH
#define UZ_RESTIR_MATH
#ifdef __cplusplus
#include <cstdint>
namespace uz {
using uint = uint32_t;
#define RIS_INOUT float&
#else
#define RIS_INOUT inout float
#endif

// Streaming RIS, including zero-weight candidates in M (Talbot et al. 2005).
// The source density is joint area density for mesh emitters, or discrete
// selection probability for the normalized directional source disk.
inline float RestirCandidateWeight(float target, float sourcePdf) {
    return target / sourcePdf;
}
// Visibility restricts the contribution invariant to the visible support.
// Apply it to every source/receiving target, including the MIS denominator.
inline float RestirVisibilityTarget(float target, float visibility) {
    return target * visibility;
}
inline bool RestirStream(RIS_INOUT sum, float weight, float u) {
    sum += weight;
    return weight > 0.0f && u * sum < weight;
}
inline float RestirWeight(float sum, uint M, float target) {
    return M > 0u && target > 0.0f ? sum / (float(M) * target) : 0.0f;
}
// Rescale the sum as well as M, preserving W when truncating history.
inline float RestirCappedSum(float sum, uint M, uint cap) {
    return M > cap ? sum * float(cap) / float(M) : sum;
}
inline float RestirCombineWeight(float targetHere, float W, uint M) {
    return targetHere * W * float(M);
}
/// @ref Lin et al. 2022 "Generalized Resampled Importance Sampling", Eqs. 4, 7, 22.
// Source targets need not be the (intractable) reservoir PDFs. These weights
// must partition unity, and vanish outside each source's support.
inline float RestirMIS(float sourceTarget, uint M, float targetSum) {
    return targetSum > 0.0f ? float(M) * sourceTarget / targetSum : 0.0f;
}
inline float RestirReuseWeight(float targetHere, float W, float mis) {
    return targetHere * W * mis;
}
inline float RestirReuseFinalize(float sum, float target) {
    return target > 0.0f ? sum / target : 0.0f;
}
#undef RIS_INOUT
#ifdef __cplusplus
}
#endif
#endif
