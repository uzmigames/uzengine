#ifndef UZ_CLOUD_AERIAL_MATH
#define UZ_CLOUD_AERIAL_MATH

// Scalar form shared by the shader and native regression tests.
float CloudAtmosphereRadiance(float radiance, float atmosphereTransmittance,
    float inscatter, float cloudTransmittance) {
    return radiance * atmosphereTransmittance + inscatter * (1.0f - cloudTransmittance);
}

float CloudSegmentTransmittance(float startToBoundary, float endToBoundary) {
    float numerator = startToBoundary > 1e-6f ? startToBoundary : 1e-6f;
    return numerator / (endToBoundary > 1e-6f ? endToBoundary : 1e-6f);
}

#endif
