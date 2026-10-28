// Shared by the GPU reconstruction and its CPU regression test.
#ifndef UZ_SSGI_SAMPLE_VALIDITY_HLSLI
#define UZ_SSGI_SAMPLE_VALIDITY_HLSLI
#ifdef __cplusplus
namespace uz {
#endif
// Alpha is availability/count, never luminance: valid unlit samples retain their weight.
float SsgiAvailableWeight(float weight, float sampleCount, bool rejectUnavailable) {
    return rejectUnavailable && sampleCount <= 0.0f ? 0.0f : weight;
}
#ifdef __cplusplus
}
#endif
#endif
