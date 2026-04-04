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

/// Alias for RandomOnSphere — used by spawn shapes.
float3 RandomUnitVector(inout uint seed) {
    return RandomOnSphere(seed);
}

/// Returns a random direction within a cone around `axis` with half-angle `angleRad`.
/// @ref Pharr 2023 — uniform cone sampling
float3 RandomInCone(inout uint seed, float3 axis, float angleRad) {
    float cosAngle = cos(angleRad);
    float z = RandomFloat(seed, cosAngle, 1.0f);
    float phi = RandomFloat(seed, 0.0f, VFX_TWO_PI);
    float sinTheta = sqrt(max(1.0f - z * z, 0.0f));

    float3 localDir = float3(sinTheta * cos(phi), sinTheta * sin(phi), z);

    // Build orthonormal basis around axis
    float3 up = abs(axis.z) < 0.999f ? float3(0, 0, 1) : float3(1, 0, 0);
    float3 tangent = normalize(cross(up, axis));
    float3 bitangent = cross(axis, tangent);

    return tangent * localDir.x + bitangent * localDir.y + axis * localDir.z;
}

// ─────────────────────────────────────────────────────────────────────────────
// Noise — Simplex 3D + Curl noise for turbulence
// ─────────────────────────────────────────────────────────────────────────────

/// @ref Bridson 2007 "Curl-Noise for Procedural Fluid Flow"
/// @ref Gustavson 2005 "Simplex noise demystified"

/// Permutation hash for noise.
float4 SimplexPermute(float4 x) {
    return fmod(((x * 34.0f) + 1.0f) * x, 289.0f);
}

/// Simplex 3D noise — returns value in [-1, 1].
/// @ref Gustavson 2005 "Simplex noise demystified"
/// @ref Ashima Arts webgl-noise — HLSL port of snoise(vec3)
float SimplexNoise3D(float3 v) {
    const float2 C = float2(1.0f / 6.0f, 1.0f / 3.0f);

    float3 i = floor(v + dot(v, C.yyy));
    float3 x0 = v - i + dot(i, C.xxx);

    float3 g = step(x0.yzx, x0.xyz);
    float3 l = 1.0f - g;
    float3 i1 = min(g.xyz, l.zxy);
    float3 i2 = max(g.xyz, l.zxy);

    float3 x1 = x0 - i1 + C.xxx;
    float3 x2 = x0 - i2 + C.yyy;
    float3 x3 = x0 - 0.5f;

    i = fmod(i, 289.0f);
    float4 p = SimplexPermute(SimplexPermute(SimplexPermute(
        i.z + float4(0.0f, i1.z, i2.z, 1.0f))
      + i.y + float4(0.0f, i1.y, i2.y, 1.0f))
      + i.x + float4(0.0f, i1.x, i2.x, 1.0f));

    float n_ = 1.0f / 7.0f;
    float4 ns = float4(n_ * 2.0f - 1.0f, n_, n_ * 2.0f - 1.0f, n_);

    float4 j = p - 49.0f * floor(p * ns.z * ns.z);
    float4 x_ = floor(j * ns.z);
    float4 y_ = floor(j - 7.0f * x_);

    float4 xx = x_ * ns.x + ns.yyyy;
    float4 yy = y_ * ns.x + ns.yyyy;
    float4 h = 1.0f - abs(xx) - abs(yy);

    float4 b0 = float4(xx.xy, yy.xy);
    float4 b1 = float4(xx.zw, yy.zw);

    float4 s0 = floor(b0) * 2.0f + 1.0f;
    float4 s1 = floor(b1) * 2.0f + 1.0f;
    float4 sh = -step(h, float4(0, 0, 0, 0));

    float4 a0 = b0.xzyw + s0.xzyw * sh.xxyy;
    float4 a1 = b1.xzyw + s1.xzyw * sh.zzww;

    float3 p0 = float3(a0.xy, h.x);
    float3 p1 = float3(a0.zw, h.y);
    float3 p2 = float3(a1.xy, h.z);
    float3 p3 = float3(a1.zw, h.w);

    float4 norm = 1.79284291400159f - 0.85373472095314f *
        float4(dot(p0, p0), dot(p1, p1), dot(p2, p2), dot(p3, p3));
    p0 *= norm.x; p1 *= norm.y; p2 *= norm.z; p3 *= norm.w;

    float4 m = max(0.6f - float4(dot(x0,x0), dot(x1,x1), dot(x2,x2), dot(x3,x3)), 0.0f);
    m = m * m;
    return 42.0f * dot(m * m, float4(dot(p0,x0), dot(p1,x1), dot(p2,x2), dot(p3,x3)));
}

/// 3D curl noise — divergence-free velocity field from simplex noise gradients.
/// @ref Bridson 2007 "Curl-Noise for Procedural Fluid Flow"
/// @constants epsilon=0.01 for finite difference gradient approximation
float3 CurlNoise3D(float3 p, float time) {
    const float e = 0.01f;
    float3 pt = p + float3(0, 0, time * 0.3f);

    // Finite difference partial derivatives of simplex noise
    float dndx = SimplexNoise3D(pt + float3(e,0,0)) - SimplexNoise3D(pt - float3(e,0,0));
    float dndy = SimplexNoise3D(pt + float3(0,e,0)) - SimplexNoise3D(pt - float3(0,e,0));
    float dndz = SimplexNoise3D(pt + float3(0,0,e)) - SimplexNoise3D(pt - float3(0,0,e));

    // Second noise field for curl (need 2 scalar fields for 3D curl)
    float3 pt2 = pt + float3(31.416f, 47.853f, 12.793f);
    float dn2dx = SimplexNoise3D(pt2 + float3(e,0,0)) - SimplexNoise3D(pt2 - float3(e,0,0));
    float dn2dy = SimplexNoise3D(pt2 + float3(0,e,0)) - SimplexNoise3D(pt2 - float3(0,e,0));
    float dn2dz = SimplexNoise3D(pt2 + float3(0,0,e)) - SimplexNoise3D(pt2 - float3(0,0,e));

    // Curl = cross(grad(n1), grad(n2))
    return float3(
        dndy * dn2dz - dndz * dn2dy,
        dndz * dn2dx - dndx * dn2dz,
        dndx * dn2dy - dndy * dn2dx
    ) / (2.0f * e);
}

#endif // VFX_COMMON_HLSLI
