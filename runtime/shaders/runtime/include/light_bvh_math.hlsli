#ifndef UZ_LIGHT_BVH_MATH
#define UZ_LIGHT_BVH_MATH
#ifdef __cplusplus
namespace uz {
#endif
// Both MIS techniques use a solid-angle density at the same receiver.
float LightBVHSolidAnglePDF(float selectionPdf, float area, float distanceSquared,
                            float emitterCosine) {
    return selectionPdf * distanceSquared / (area * emitterCosine);
}
float LightBVHBalanceWeight(float sampledPdf, float competingPdf) {
    return sampledPdf / (sampledPdf + competingPdf);
}
#ifdef __cplusplus
}
#endif
#endif
