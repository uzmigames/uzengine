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
#undef RIS_INOUT
#ifdef __cplusplus
}
#endif
#endif
