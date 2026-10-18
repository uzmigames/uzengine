#ifndef UZ_PATHTRACER_VOLUME
#define UZ_PATHTRACER_VOLUME

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

float PathVolumeShadow(float3 origin, float3 direction, float distance) {
    if (!PathVolumeActive()) return 1.0;
    float2 interval = PathVolumeInterval(origin,direction,distance);
    return PathVolumeTransmittance(volumeSigmaA+volumeSigmaS,interval.y-interval.x);
}
#endif
