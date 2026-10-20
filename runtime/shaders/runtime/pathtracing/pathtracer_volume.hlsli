#ifndef UZ_PATHTRACER_VOLUME
#define UZ_PATHTRACER_VOLUME

#include "pathtracer_math.hlsli"
#ifdef __cplusplus
namespace uz {
#define VOLUME_INOUT uint&
#else
#define VOLUME_INOUT inout uint
#endif

// Scalar kernels are shared with the CPU proofs. q is the projection onto
// (1,1,0)/sqrt(2), normalized by the sphere radius: -1 <= q <= 1.
inline float PathVolumeDensity(float q, float variation) {
    return 1.0f + variation*q;
}

inline uint PathVolumeHash(uint value) {
    value = (value ^ (value >> 16u))*0x7feb352du;
    value = (value ^ (value >> 15u))*0x846ca68bu;
    return value ^ (value >> 16u);
}

inline float PathVolumeRandom(VOLUME_INOUT state) {
    state = state*747796405u+2891336453u;
    uint word = ((state >> ((state >> 28u)+4u)) ^ state)*277803737u;
    word = (word >> 22u) ^ word;
    // Open interval, so candidate distances always advance and never log(0).
    return (float(word >> 9u)+0.5f)/8388608.0f;
}

inline float PathVolumeDelta(float sigmaT, float variation, float q0, float dq,
                            float distance, float firstU, uint state) {
    if (variation == 0.0f) return PathVolumeDistance(sigmaT,firstU);
    float majorant = sigmaT*(1.0f+variation);
    if (majorant == 0.0f) return 1e30f;
    float t = PathVolumeDistance(majorant,firstU);
    // No fixed iteration cap: truncating null collisions biases transport.
    while (t < distance) {
        float acceptance = PathVolumeDensity(q0+dq*t,variation)/(1.0f+variation);
        if (PathVolumeRandom(state) < acceptance) return t;
        t += PathVolumeDistance(majorant,PathVolumeRandom(state));
    }
    return t;
}

inline float PathVolumeRatio(float sigmaT, float variation, float q0, float dq,
                            float distance, uint state) {
    if (variation == 0.0f) return PathVolumeTransmittance(sigmaT,distance);
    float majorant = sigmaT*(1.0f+variation);
    if (majorant == 0.0f || distance == 0.0f) return 1.0f;
    float weight = 1.0f;
    float t = PathVolumeDistance(majorant,PathVolumeRandom(state));
    while (t < distance) {
        weight *= 1.0f-PathVolumeDensity(q0+dq*t,variation)/(1.0f+variation);
        if (weight == 0.0f) break;
        t += PathVolumeDistance(majorant,PathVolumeRandom(state));
    }
    return weight;
}

#undef VOLUME_INOUT
#ifdef __cplusplus
} // namespace uz
#else
// Pass-private b3, in engine world units (centimeters). A finite sphere
// makes environment and directional-light optical depths well-defined.
cbuffer PathVolumeRoot : register(b3) {
    float volumeSigmaA;
    float volumeSigmaS;
    float volumeG;
    float volumeRadius;
    float volumeCenterX;
    float volumeCenterY;
    float volumeCenterZ;
    uint volumeEnabled;
    float volumeVariation;
    float volumePadding0;
    float volumePadding1;
    float volumePadding2;
};

bool PathVolumeActive() {
    return volumeEnabled != 0u && volumeSigmaA + volumeSigmaS > 0.0;
}

float2 PathVolumeInterval(float3 origin, float3 direction, float distance) {
    float3 offset = origin - float3(volumeCenterX,volumeCenterY,volumeCenterZ);
    float b = dot(offset,direction);
    float discriminant = b*b - dot(offset,offset) + volumeRadius*volumeRadius;
    if (discriminant <= 0.0) return 0.0;
    float root = sqrt(discriminant);
    float start = max(0.0,-b-root);
    float end = min(distance,-b+root);
    return float2(start,max(start,end));
}

uint PathVolumeSeed(uint sample, uint2 pixel, uint stream) {
    return PathVolumeHash(sample ^ PathVolumeHash(pixel.x+pixel.y*65536u)
        ^ PathVolumeHash(stream));
}

float PathVolumeCoordinate(float3 P) {
    return ((P.x-volumeCenterX)+(P.y-volumeCenterY))*(0.707106781187/volumeRadius);
}

float PathVolumeSlope(float3 direction) {
    return (direction.x+direction.y)*(0.707106781187/volumeRadius);
}

float PathVolumeShadow(float3 origin, float3 direction, float distance, uint seed) {
    if (!PathVolumeActive()) return 1.0;
    float2 interval = PathVolumeInterval(origin,direction,distance);
    return PathVolumeRatio(volumeSigmaA+volumeSigmaS,volumeVariation,
        PathVolumeCoordinate(origin+direction*interval.x),PathVolumeSlope(direction),
        interval.y-interval.x,seed);
}
#endif
#endif
