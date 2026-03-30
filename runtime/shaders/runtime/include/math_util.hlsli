/// Math constants, hashing, coordinate conversions, and utility functions.
///
/// All shaders should #include this header instead of defining local PI
/// constants. The UZ_PI_DEFINED guard prevents redefinition when multiple
/// headers include this file.
#ifndef MATH_UTIL_HLSLI
#define MATH_UTIL_HLSLI

// ─────────────────────────────────────────────────────────────────────────────
// Constants
// ─────────────────────────────────────────────────────────────────────────────

#ifndef UZ_PI_DEFINED
#define UZ_PI_DEFINED
static const float PI     = 3.14159265358979323846f;
static const float M_2PI  = 6.28318530717958647692f;  ///< 2 * PI
static const float M_1_PI = 0.31830988618379067154f;  ///< 1 / PI
static const float M_PI_2 = 1.57079632679489661923f;  ///< PI / 2
static const float M_PI_4 = 0.78539816339744830962f;  ///< PI / 4
#endif

// ─────────────────────────────────────────────────────────────────────────────
// Power Helpers
// ─────────────────────────────────────────────────────────────────────────────

float Pow2(float x) { return x * x; }
float Pow4(float x) { float x2 = x * x; return x2 * x2; }
float Pow5(float x) { float x2 = x * x; return x2 * x2 * x; }

// ─────────────────────────────────────────────────────────────────────────────
// Color Space Conversions
// ─────────────────────────────────────────────────────────────────────────────

float3 LinearToSrgb(float3 c) {
    float3 lo = c * 12.92;
    float3 hi = 1.055 * pow(max(c, 0.0), 1.0 / 2.4) - 0.055;
    return float3(c.r <= 0.0031308 ? lo.r : hi.r,
                  c.g <= 0.0031308 ? lo.g : hi.g,
                  c.b <= 0.0031308 ? lo.b : hi.b);
}

float3 SrgbToLinear(float3 c) {
    float3 lo = c / 12.92;
    float3 hi = pow((c + 0.055) / 1.055, 2.4);
    return float3(c.r <= 0.04045 ? lo.r : hi.r,
                  c.g <= 0.04045 ? lo.g : hi.g,
                  c.b <= 0.04045 ? lo.b : hi.b);
}

// ─────────────────────────────────────────────────────────────────────────────
// Hash Functions
// ─────────────────────────────────────────────────────────────────────────────

/// PCG hash — single uint → uint with full avalanche.
/// @ref O'Neill 2014 "PCG: A Family of Simple Fast Space-Efficient Statistically
///   Good Algorithms for Random Number Generation"
/// @constants Multiplier=747796405, Increment=2891336453, Shift sequence=28→4→xorshift
uint pcgHash(uint seed) {
    uint state = seed * 747796405u + 2891336453u;
    uint word  = ((state >> ((state >> 28u) + 4u)) ^ state) * 277803737u;
    return (word >> 22u) ^ word;
}

/// PCG hash — 2D uint2 → uint2. Two independent hash rounds with decorrelation.
/// @ref O'Neill 2014 "PCG: A Family of Simple Fast Space-Efficient Statistically
///   Good Algorithms for Random Number Generation"
uint2 pcgHash2D(uint2 v) {
    v = v * 1664525u + 1013904223u;
    v.x += v.y * 1664525u;
    v.y += v.x * 1664525u;
    v = v ^ (v >> 16u);
    v.x += v.y * 1664525u;
    v.y += v.x * 1664525u;
    v = v ^ (v >> 16u);
    return v;
}

/// Jenkins one-at-a-time hash.
/// @ref Jenkins 1997 "Hash Functions for Hash Table Lookup"
uint jenkinsHash(uint seed) {
    seed += seed << 10u;
    seed ^= seed >> 6u;
    seed += seed << 3u;
    seed ^= seed >> 11u;
    seed += seed << 15u;
    return seed;
}

// ─────────────────────────────────────────────────────────────────────────────
// Branchless Orthonormal Basis
// ─────────────────────────────────────────────────────────────────────────────

/// Constructs an orthonormal basis (tangent, bitangent) from a unit normal.
/// Branchless, no sqrt, no pole singularity (works for all orientations).
/// @ref Duff et al. 2017 "Building an Orthonormal Basis, Revisited" (JCGT Vol. 6 No. 1)
/// @constants sign = copysign(1, n.z), a = -rcp(sign + n.z), b = n.x * n.y * a
void branchlessONB(float3 n, out float3 t, out float3 b) {
    float sign_ = n.z >= 0.0f ? 1.0f : -1.0f;
    float a = -1.0f / (sign_ + n.z);
    float bb = n.x * n.y * a;
    t = float3(1.0f + sign_ * n.x * n.x * a, sign_ * bb, -sign_ * n.x);
    b = float3(bb, sign_ + n.y * n.y * a, -n.y);
}

// ─────────────────────────────────────────────────────────────────────────────
// Spherical / LatLong Coordinate Conversions
// ─────────────────────────────────────────────────────────────────────────────

/// Converts a unit direction to spherical coordinates (theta, phi).
/// theta = polar angle from +Z axis [0, PI], phi = azimuthal angle from +X [0, 2PI).
float2 CartesianToSpherical(float3 dir) {
    float theta = acos(clamp(dir.z, -1.0f, 1.0f));
    float phi   = atan2(dir.y, dir.x);
    if (phi < 0.0f) phi += M_2PI;
    return float2(theta, phi);
}

/// Converts spherical coordinates (theta, phi) to a unit direction.
/// theta = polar angle from +Z [0, PI], phi = azimuthal from +X [0, 2PI).
float3 SphericalToCartesian(float theta, float phi) {
    float sinT, cosT, sinP, cosP;
    sincos(theta, sinT, cosT);
    sincos(phi, sinP, cosP);
    return float3(sinT * cosP, sinT * sinP, cosT);
}

/// Converts a UV in [0,1]^2 (latitude-longitude / equirectangular) to a unit direction.
/// U maps longitude [0, 2PI), V maps latitude [0, PI] (top = +Z pole).
float3 LatLongToDirection(float2 uv) {
    float phi   = uv.x * M_2PI;
    float theta = uv.y * PI;
    return SphericalToCartesian(theta, phi);
}

/// Converts a unit direction to lat-long UV in [0,1]^2.
float2 DirectionToLatLong(float3 dir) {
    float2 sp = CartesianToSpherical(dir);
    return float2(sp.y / M_2PI, sp.x / PI);
}

#endif
