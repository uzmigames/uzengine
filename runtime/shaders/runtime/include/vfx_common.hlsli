/// VFX Common Utilities — RNG, math constants, and helpers for GPU particle shaders.
///
/// @ref Wang 2003 "Hash Functions for GPU Rendering" — integer hash for particle RNG
/// @ref Pharr 2023 "Physically Based Rendering" — uniform sphere sampling
/// @constants VFX_PI=3.14159265, VFX_TWO_PI=6.28318530

#ifndef VFX_COMMON_HLSLI
#define VFX_COMMON_HLSLI

// ─────────────────────────────────────────────────────────────────────────────
// Constants
// ─────────────────────────────────────────────────────────────────────────────

static const float VFX_PI     = 3.14159265358979323846f;
static const float VFX_TWO_PI = 6.28318530717958647692f;

// ─────────────────────────────────────────────────────────────────────────────
// RNG — Wang hash based particle seeding
// ─────────────────────────────────────────────────────────────────────────────

/// Deterministic particle seed from index and frame number.
/// Produces a well-distributed uint suitable for subsequent RandomFloat calls.
uint VFXSeed(uint particleIndex, uint frameNumber) {
    uint seed = particleIndex * 1103515245u + frameNumber * 12345u;
    seed = (seed ^ 61u) ^ (seed >> 16u);
    seed *= 9u;
    seed = seed ^ (seed >> 4u);
    seed *= 0x27d4eb2du;
    seed = seed ^ (seed >> 15u);
    return seed;
}

/// Advances the RNG state and returns a float in [0, 1).
float RandomFloat01(inout uint seed) {
    seed = seed * 747796405u + 2891336453u;
    uint result = ((seed >> ((seed >> 28u) + 4u)) ^ seed) * 277803737u;
    result = (result >> 22u) ^ result;
    return float(result) / 4294967295.0f;
}

/// Returns a random float in [minVal, maxVal].
float RandomFloat(inout uint seed, float minVal, float maxVal) {
    return minVal + RandomFloat01(seed) * (maxVal - minVal);
}

/// Returns a random unit-length direction vector (uniform on sphere).
/// @ref Pharr 2023 — uniform sphere sampling via cos(theta), phi
float3 RandomOnSphere(inout uint seed) {
    float z = RandomFloat(seed, -1.0f, 1.0f);
    float phi = RandomFloat(seed, 0.0f, VFX_TWO_PI);
    float r = sqrt(max(1.0f - z * z, 0.0f));
    return float3(r * cos(phi), r * sin(phi), z);
}

/// Returns a random point inside a sphere of given radius.
float3 RandomInSphere(inout uint seed, float radius) {
    float3 dir = RandomOnSphere(seed);
    float t = pow(RandomFloat01(seed), 1.0f / 3.0f); // cube root for uniform volume
    return dir * t * radius;
}

/// Returns a random 2D direction on the unit circle.
float2 RandomOnCircle(inout uint seed) {
    float angle = RandomFloat(seed, 0.0f, VFX_TWO_PI);
    return float2(cos(angle), sin(angle));
}

#endif // VFX_COMMON_HLSLI
