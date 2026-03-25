/// VFX common utilities — RNG, random distributions, shared constants.
///
/// Provides stateless pseudo-random number generation based on PCG hash,
/// plus distribution helpers (uniform float, unit sphere, cone, disk) used
/// by particle spawn and update shaders.
///
/// # RNG Strategy
/// All VFX RNG is stateless: seed = f(particle_index, frame_number).
/// Each call to RandomFloat01 / RandomFloat advances the seed in-place via
/// PCG hash, producing a new uncorrelated value. This avoids per-particle
/// persistent state while maintaining good distribution quality.
///
/// @ref O'Neill 2014 "PCG: A Family of Simple Fast Space-Efficient Statistically
///      Good Algorithms for Random Number Generation"

#ifndef VFX_COMMON_HLSLI
#define VFX_COMMON_HLSLI

// ─────────────────────────────────────────────────────────────────────────────
// Constants
// ─────────────────────────────────────────────────────────────────────────────

/// Two pi — used for full-circle angular distributions.
static const float VFX_TWO_PI = 6.28318530718f;

/// Reciprocal of 2^32 — converts uint hash to [0, 1) float.
static const float VFX_RCP_UINT_MAX = 1.0f / 4294967296.0f;

/// Sentinel value for dead/unallocated particle slots.
static const uint VFX_PARTICLE_DEAD = 0xFFFFFFFF;

// ─────────────────────────────────────────────────────────────────────────────
// PCG Hash — Core RNG Primitive
// ─────────────────────────────────────────────────────────────────────────────

/// Single-iteration PCG hash for high-quality stateless RNG.
///
/// Produces a well-distributed 32-bit hash from any 32-bit input. Two
/// applications of the hash are sufficient to decorrelate sequential seeds
/// (e.g., particle indices 0, 1, 2, ...).
///
/// # Arguments
/// - `input`: arbitrary 32-bit seed value.
///
/// # Returns
/// 32-bit pseudo-random hash.
uint PCGHash(uint input)
{
    uint state = input * 747796405u + 2891336453u;
    uint word  = ((state >> ((state >> 28u) + 4u)) ^ state) * 277803737u;
    return (word >> 22u) ^ word;
}

// ─────────────────────────────────────────────────────────────────────────────
// Uniform Distributions
// ─────────────────────────────────────────────────────────────────────────────

/// Advances the seed and returns a uniform float in [0, 1).
///
/// # Arguments
/// - `seed`: in/out RNG state. Modified on return.
///
/// # Returns
/// Uniform pseudo-random float in [0, 1).
float RandomFloat01(inout uint seed)
{
    seed = PCGHash(seed);
    return float(seed) * VFX_RCP_UINT_MAX;
}

/// Advances the seed and returns a uniform float in [minVal, maxVal).
///
/// # Arguments
/// - `seed`:   in/out RNG state. Modified on return.
/// - `minVal`: inclusive lower bound.
/// - `maxVal`: exclusive upper bound.
///
/// # Returns
/// Uniform pseudo-random float in [minVal, maxVal).
float RandomFloat(inout uint seed, float minVal, float maxVal)
{
    return minVal + RandomFloat01(seed) * (maxVal - minVal);
}

// ─────────────────────────────────────────────────────────────────────────────
// Spatial Distributions
// ─────────────────────────────────────────────────────────────────────────────

/// Generates a uniformly distributed random unit vector on the sphere surface.
///
/// Uses the cylindrical equal-area projection method: uniform Z in [-1, 1]
/// and uniform azimuthal angle in [0, 2pi). This produces an unbiased
/// distribution over the sphere surface without rejection sampling.
///
/// # Arguments
/// - `seed`: in/out RNG state. Modified on return.
///
/// # Returns
/// Normalized 3D direction vector.
float3 RandomUnitVector(inout uint seed)
{
    float z   = RandomFloat(seed, -1.0f, 1.0f);
    float phi = RandomFloat(seed, 0.0f, VFX_TWO_PI);
    float r   = sqrt(1.0f - z * z);
    return float3(r * cos(phi), r * sin(phi), z);
}

/// Generates a uniformly distributed random point inside a sphere.
///
/// Applies cube-root correction to the radial component to ensure uniform
/// volume distribution (without this, points would cluster near the center).
///
/// # Arguments
/// - `seed`:   in/out RNG state. Modified on return.
/// - `radius`: sphere radius. Must be >= 0.
///
/// # Returns
/// 3D point within the sphere of given radius, centered at origin.
float3 RandomInSphere(inout uint seed, float radius)
{
    float3 dir = RandomUnitVector(seed);
    // Cube root for uniform volume distribution in 3D
    float r = pow(RandomFloat01(seed), 1.0f / 3.0f) * radius;
    return dir * r;
}

/// Generates a random direction within a cone around the given axis.
///
/// The cone is defined by `direction` (axis) and `halfAngle` (half-cone
/// angle in radians). At halfAngle = 0 the result is exactly `direction`;
/// at halfAngle = pi the distribution covers the full sphere.
///
/// Constructs an orthonormal basis from `direction` and rotates the
/// local-space cone sample into world space.
///
/// # Arguments
/// - `seed`:      in/out RNG state. Modified on return.
/// - `direction`: normalized cone axis direction.
/// - `halfAngle`: half-cone angle in radians. Must be in [0, pi].
///
/// # Returns
/// Normalized 3D direction vector within the specified cone.
float3 RandomInCone(inout uint seed, float3 direction, float halfAngle)
{
    // Generate random direction in cone around +Z
    float cosHalf  = cos(halfAngle);
    float z        = RandomFloat(seed, cosHalf, 1.0f);
    float phi      = RandomFloat(seed, 0.0f, VFX_TWO_PI);
    float sinTheta = sqrt(1.0f - z * z);

    float3 localDir = float3(sinTheta * cos(phi), sinTheta * sin(phi), z);

    // Build orthonormal basis to rotate from +Z to `direction`
    float3 up    = abs(direction.y) < 0.999f ? float3(0, 1, 0) : float3(1, 0, 0);
    float3 right = normalize(cross(up, direction));
    up           = cross(direction, right);

    return right * localDir.x + up * localDir.y + direction * localDir.z;
}

/// Generates a uniformly distributed random point on a disk in the XZ plane.
///
/// Uses square-root correction for uniform area distribution.
///
/// # Arguments
/// - `seed`:   in/out RNG state. Modified on return.
/// - `radius`: disk radius. Must be >= 0.
///
/// # Returns
/// 3D point on the XZ disk (Y = 0), centered at origin.
float3 RandomOnDisk(inout uint seed, float radius)
{
    float r   = sqrt(RandomFloat01(seed)) * radius;
    float phi = RandomFloat(seed, 0.0f, VFX_TWO_PI);
    return float3(r * cos(phi), 0.0f, r * sin(phi));
}

/// Creates a unique RNG seed from particle index and frame number.
///
/// Combines the two inputs with coprime multipliers to avoid correlated
/// sequences across particles and frames.
///
/// # Arguments
/// - `particleIndex`: unique index of the particle within the pool.
/// - `frameNumber`:   current simulation frame counter.
///
/// # Returns
/// 32-bit seed suitable for passing to RandomFloat01 / RandomFloat.
uint VFXSeed(uint particleIndex, uint frameNumber)
{
    return particleIndex * 1973u + frameNumber * 6311u;
}

// ─────────────────────────────────────────────────────────────────────────────
// GPU Noise Functions — Gradient, Perlin, Simplex, FBM, Curl
// ─────────────────────────────────────────────────────────────────────────────

/// Computes a pseudo-random gradient vector at an integer lattice point.
///
/// Hashes the 3D integer coordinates via PCGHash and selects one of 12
/// gradient directions corresponding to cube edge midpoints. These 12
/// directions provide the best isotropy for 3D Perlin noise.
///
/// # Arguments
/// - `ip`: integer lattice coordinates.
///
/// # Returns
/// One of 12 unit-length gradient vectors (cube edge midpoints).
float3 Gradient3D(int3 ip)
{
    uint h = PCGHash(uint(ip.x) + PCGHash(uint(ip.y) + PCGHash(uint(ip.z))));
    h = h % 12u;

    // 12 gradient directions: midpoints of cube edges
    // Avoids a branch-heavy switch by using two small LUTs
    static const float3 kGradients[12] = {
        float3( 1, 1, 0), float3(-1, 1, 0), float3( 1,-1, 0), float3(-1,-1, 0),
        float3( 1, 0, 1), float3(-1, 0, 1), float3( 1, 0,-1), float3(-1, 0,-1),
        float3( 0, 1, 1), float3( 0,-1, 1), float3( 0, 1,-1), float3( 0,-1,-1)
    };

    return kGradients[h];
}

/// 3D Perlin noise using gradient interpolation on an integer lattice.
///
/// Floors the input to find the enclosing unit cube, computes dot-product
/// gradients at all 8 corners, and interpolates with Perlin's improved
/// quintic fade curve: t = t^3 * (t * (t*6 - 15) + 10). This eliminates
/// second-derivative discontinuities that cause visible artifacts.
///
/// # Arguments
/// - `pos`: continuous 3D sampling position.
///
/// # Returns
/// Noise value in approximately [-1, 1].
float Perlin3D(float3 pos)
{
    int3 ip   = int3(floor(pos));
    float3 fp = pos - float3(ip);

    // Quintic fade — C2-continuous (zero first and second derivatives at 0 and 1)
    float3 u = fp * fp * fp * (fp * (fp * 6.0f - 15.0f) + 10.0f);

    // Dot products of gradient vectors with distance vectors at 8 cube corners
    float d000 = dot(Gradient3D(ip + int3(0, 0, 0)), fp - float3(0, 0, 0));
    float d100 = dot(Gradient3D(ip + int3(1, 0, 0)), fp - float3(1, 0, 0));
    float d010 = dot(Gradient3D(ip + int3(0, 1, 0)), fp - float3(0, 1, 0));
    float d110 = dot(Gradient3D(ip + int3(1, 1, 0)), fp - float3(1, 1, 0));
    float d001 = dot(Gradient3D(ip + int3(0, 0, 1)), fp - float3(0, 0, 1));
    float d101 = dot(Gradient3D(ip + int3(1, 0, 1)), fp - float3(1, 0, 1));
    float d011 = dot(Gradient3D(ip + int3(0, 1, 1)), fp - float3(0, 1, 1));
    float d111 = dot(Gradient3D(ip + int3(1, 1, 1)), fp - float3(1, 1, 1));

    // Trilinear interpolation with quintic weights
    float x00 = lerp(d000, d100, u.x);
    float x10 = lerp(d010, d110, u.x);
    float x01 = lerp(d001, d101, u.x);
    float x11 = lerp(d011, d111, u.x);

    float y0 = lerp(x00, x10, u.y);
    float y1 = lerp(x01, x11, u.y);

    return lerp(y0, y1, u.z);
}

/// 3D simplex noise using tetrahedral decomposition.
///
/// Skews 3D space into a rhombohedral lattice, identifies which of the 6
/// possible tetrahedra the point falls in, then evaluates radial kernels
/// at the 4 simplex corners. Cheaper than Perlin (4 gradient lookups vs 8)
/// and produces fewer directional artifacts.
///
/// Uses F3 = 1/3 (skew factor) and G3 = 1/6 (unskew factor).
///
/// # Arguments
/// - `v`: continuous 3D sampling position.
///
/// # Returns
/// Noise value in approximately [-1, 1].
float SimplexNoise3D(float3 v)
{
    static const float F3 = 1.0f / 3.0f;
    static const float G3 = 1.0f / 6.0f;

    // Skew input space to find simplex cell
    float s  = (v.x + v.y + v.z) * F3;
    int3  i  = int3(floor(v + s));
    float t  = float(i.x + i.y + i.z) * G3;
    float3 x0 = v - (float3(i) - t);

    // Determine which simplex we are in via magnitude ordering of x0 components
    int3 i1, i2;
    if (x0.x >= x0.y)
    {
        if (x0.y >= x0.z)      { i1 = int3(1,0,0); i2 = int3(1,1,0); } // x>=y>=z
        else if (x0.x >= x0.z) { i1 = int3(1,0,0); i2 = int3(1,0,1); } // x>=z>y
        else                    { i1 = int3(0,0,1); i2 = int3(1,0,1); } // z>x>=y
    }
    else
    {
        if (x0.y < x0.z)       { i1 = int3(0,0,1); i2 = int3(0,1,1); } // z>y>x
        else if (x0.x < x0.z)  { i1 = int3(0,1,0); i2 = int3(0,1,1); } // y>=z>x
        else                    { i1 = int3(0,1,0); i2 = int3(1,1,0); } // y>x>=z
    }

    // Offsets for the 4 simplex corners
    float3 x1 = x0 - float3(i1) + G3;
    float3 x2 = x0 - float3(i2) + 2.0f * G3;
    float3 x3 = x0 - 1.0f + 3.0f * G3;

    // Radial falloff kernel (r^2 = 0.6 - |x|^2, clamped to zero)
    float4 w = float4(
        0.6f - dot(x0, x0),
        0.6f - dot(x1, x1),
        0.6f - dot(x2, x2),
        0.6f - dot(x3, x3)
    );
    w = max(w, 0.0f);
    w = w * w * w * w; // w^4 kernel

    // Gradient dot products at each corner
    float4 d = float4(
        dot(Gradient3D(i),      x0),
        dot(Gradient3D(i + i1), x1),
        dot(Gradient3D(i + i2), x2),
        dot(Gradient3D(i + 1),  x3)
    );

    // Scale factor chosen so output fits approximately [-1, 1]
    return 32.0f * dot(w, d);
}

/// Fractal Brownian Motion — layered Perlin noise with decreasing amplitude.
///
/// Accumulates `octaves` layers of Perlin3D with lacunarity 2.0 (frequency
/// doubling) and gain 0.5 (amplitude halving). Time is added to the x
/// coordinate for temporal animation. The result is a float3 where each
/// component samples a different noise field (offset by large primes) to
/// produce uncorrelated vector turbulence.
///
/// # Arguments
/// - `pos`:     3D sampling position.
/// - `time`:    animation time offset (added to pos.x per octave).
/// - `octaves`: number of noise layers. Clamped to [1, 8].
///
/// # Returns
/// 3D noise vector, each component in approximately [-1, 1].
float3 FBM3D(float3 pos, float time, int octaves)
{
    octaves = clamp(octaves, 1, 8);

    // Offset seeds for uncorrelated x/y/z components
    static const float3 kOffsetX = float3(0.0f,    0.0f,    0.0f);
    static const float3 kOffsetY = float3(31.416f, 73.156f, 57.793f);
    static const float3 kOffsetZ = float3(97.213f, 41.862f, 13.579f);

    float3 result = float3(0.0f, 0.0f, 0.0f);
    float amplitude = 1.0f;
    float frequency = 1.0f;
    float normalization = 0.0f;

    for (int i = 0; i < octaves; i++)
    {
        float3 p = (pos + float3(time, 0.0f, 0.0f)) * frequency;
        result.x += amplitude * Perlin3D(p + kOffsetX);
        result.y += amplitude * Perlin3D(p + kOffsetY);
        result.z += amplitude * Perlin3D(p + kOffsetZ);

        normalization += amplitude;
        amplitude *= 0.5f;
        frequency *= 2.0f;
    }

    return result / normalization;
}

/// Curl of a 3D noise field for divergence-free velocity perturbation.
///
/// Computes the curl of FBM3D using central finite differences. The result
/// is inherently divergence-free (div(curl(F)) = 0), making it ideal for
/// smoke, fire, and fluid-like VFX turbulence that should not compress
/// or expand the particle field.
///
/// # Arguments
/// - `pos`:  3D sampling position.
/// - `time`: animation time offset (passed through to FBM3D).
///
/// # Returns
/// Divergence-free 3D velocity vector.
float3 CurlNoise3D(float3 pos, float time)
{
    static const int kFBMOctaves = 4;
    static const float kEpsilon  = 0.01f;

    // Central differences for partial derivatives of each FBM component
    float3 dx = FBM3D(pos + float3(kEpsilon, 0, 0), time, kFBMOctaves)
              - FBM3D(pos - float3(kEpsilon, 0, 0), time, kFBMOctaves);
    float3 dy = FBM3D(pos + float3(0, kEpsilon, 0), time, kFBMOctaves)
              - FBM3D(pos - float3(0, kEpsilon, 0), time, kFBMOctaves);
    float3 dz = FBM3D(pos + float3(0, 0, kEpsilon), time, kFBMOctaves)
              - FBM3D(pos - float3(0, 0, kEpsilon), time, kFBMOctaves);

    float invEps2 = 1.0f / (2.0f * kEpsilon);

    // curl(F) = (dFz/dy - dFy/dz, dFx/dz - dFz/dx, dFy/dx - dFx/dy)
    return float3(
        (dy.z - dz.y) * invEps2,
        (dz.x - dx.z) * invEps2,
        (dx.y - dy.x) * invEps2
    );
}

#endif // VFX_COMMON_HLSLI
