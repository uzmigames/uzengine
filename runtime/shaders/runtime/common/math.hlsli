/// UzEngine common math utilities.
///
/// Power helpers, safe math wrappers, hashing functions, noise generators,
/// and coordinate transforms used across all runtime shaders.

#ifndef UZ_COMMON_MATH_HLSLI
#define UZ_COMMON_MATH_HLSLI

#include "constants.hlsli"

// ─── Power helpers ─────────────────────────────────────────────────────────

/// x^2
float Pow2(float x) { return x * x; }

/// x^4
float Pow4(float x) { float x2 = x * x; return x2 * x2; }

/// x^5 (used in Schlick Fresnel)
float Pow5(float x) { float x2 = x * x; return x2 * x2 * x; }

/// Alias for Pow2 — squared magnitude / area calculations
float Sq(float x) { return x * x; }

// ─── Safe math wrappers ───────────────────────────────────────────────────

/// Safe reciprocal: returns 0.0 when |x| is near zero
float SafeRcp(float x)
{
    return (abs(x) > FLT_EPSILON) ? rcp(x) : 0.0f;
}

/// Safe square root: clamps negative input to zero
float SafeSqrt(float x)
{
    return sqrt(max(x, 0.0f));
}

/// Safe arccosine: clamps input to [-1, 1]
float SafeAcos(float x)
{
    return acos(clamp(x, -1.0f, 1.0f));
}

// ─── Vector length squared ────────────────────────────────────────────────

/// Squared length of a 2D vector (avoids sqrt)
float LengthSq(float2 v) { return dot(v, v); }

/// Squared length of a 3D vector (avoids sqrt)
float LengthSq(float3 v) { return dot(v, v); }

// ─── Hash functions ───────────────────────────────────────────────────────

/// PCG hash — fast high-quality 32-bit hash.
/// @ref O'Neill 2014 "PCG: A Family of Simple Fast Space-Efficient
///      Statistically Good Algorithms for Random Number Generation"
uint HashPCG(uint input)
{
    uint state = input * 747796405u + 2891336453u;
    uint word  = ((state >> ((state >> 28u) + 4u)) ^ state) * 277803737u;
    return (word >> 22u) ^ word;
}

/// Jenkins one-at-a-time hash.
/// @ref Jenkins 1997 "A Hash Function for Hash Table Lookup"
uint HashJenkins(uint x)
{
    x += x << 10u;
    x ^= x >> 6u;
    x += x << 3u;
    x ^= x >> 11u;
    x += x << 15u;
    return x;
}

// ─── Noise ────────────────────────────────────────────────────────────────

/// Interleaved gradient noise — spatiotemporally stable dither pattern.
/// @ref Jimenez 2014 "Next Generation Post Processing in Call of Duty:
///      Advanced Warfare"
///
/// Produces a scalar in [0, 1) that varies smoothly per pixel and per frame.
/// Used for dithered sampling, TAA jitter, and blue-noise approximation.
float InterleavedGradientNoise(float2 pixelPos, uint frameIndex)
{
    float framePhase = float(frameIndex % 64u) * 0.7548776662466927f;
    float x = pixelPos.x + framePhase;
    float y = pixelPos.y + framePhase;
    return frac(52.9829189f * frac(0.06711056f * x + 0.00583715f * y));
}

// ─── Basis construction ───────────────────────────────────────────────────

/// Build an orthonormal tangent frame from a single normal vector.
/// @ref Duff et al. 2017 "Building an Orthonormal Basis, Revisited"
///      Journal of Computer Graphics Techniques, Vol. 6, No. 1
///
/// Constructs two tangent vectors (t, b) perpendicular to n and each other.
/// Uses the improved sign-branch method that avoids degenerate cases near
/// n.z == 0 present in earlier methods (Frisvad 2012).
void BuildOrthonormalBasis(float3 n, out float3 t, out float3 b)
{
    float s = (n.z >= 0.0f) ? 1.0f : -1.0f;
    float a = -1.0f / (s + n.z);
    float c = n.x * n.y * a;
    t = float3(1.0f + s * n.x * n.x * a, s * c, -s * n.x);
    b = float3(c, s + n.y * n.y * a, -n.y);
}

// ─── Spherical coordinates ────────────────────────────────────────────────

/// Convert spherical angles to a unit Cartesian direction (Z-up).
/// sinTheta and cosTheta are the polar angle components; phi is azimuthal.
float3 SphericalToCartesian(float sinTheta, float cosTheta, float phi)
{
    return float3(sinTheta * cos(phi), sinTheta * sin(phi), cosTheta);
}

/// Convert a unit Cartesian direction to spherical angles (Z-up).
/// Returns float2(theta, phi) where theta = polar angle from +Z,
/// phi = azimuthal angle in the XY plane.
float2 CartesianToSpherical(float3 dir)
{
    return float2(acos(clamp(dir.z, -1.0f, 1.0f)), atan2(dir.y, dir.x));
}

// ─── Remapping ────────────────────────────────────────────────────────────

/// Linear remapping from [oldMin, oldMax] to [newMin, newMax].
float Remap(float value, float oldMin, float oldMax, float newMin, float newMax)
{
    return newMin + (value - oldMin) * (newMax - newMin) * SafeRcp(oldMax - oldMin);
}

#endif // UZ_COMMON_MATH_HLSLI
