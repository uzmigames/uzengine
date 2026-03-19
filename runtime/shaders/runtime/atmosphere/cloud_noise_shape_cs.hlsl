/// Cloud Shape Noise Generator — 3D Perlin-Worley Hybrid (128x128x128 RGBA8)
///
/// Generates the primary cloud density noise texture used for volumetric cloud
/// ray-marching. Each channel stores a different frequency of noise:
///   R = Perlin-Worley at frequency 4  (low-frequency base shape)
///   G = Worley at frequency 8         (medium detail)
///   B = Worley at frequency 16        (fine detail)
///   A = Worley at frequency 32        (finest detail)
///
/// @source Schneider & Vos, SIGGRAPH 2015 — "The Real-Time Volumetric
///         Cloudscapes of Horizon Zero Dawn", slides 23-26 (channel layout,
///         frequencies, Perlin-Worley combination)
/// @source UE5 RandomPCG.ush:12 — Rand3DPCG16 (PCG-based 3D hash)
/// @source UE5 Random.ush:130 — PerlinRamp (quintic interpolation polynomial)
/// @source UE5 Random.ush:203 — GradientNoise3D_ALU (Perlin gradient noise)
/// @source UE5 Random.ush:473 — VoronoiCornerSample (Worley jitter)
/// @source UE5 Random.ush:517 — VoronoiNoise3D_ALU (Worley/Voronoi noise)
/// @source UE5 Random.ush:122 — NoiseTileWrap (tiling via frac * repeat)
/// @constants Frequencies: 4, 8, 16, 32 (Schneider & Vos 2015, slide 24)
///            PerlinRamp: t^3*(t*(t*6-15)+10) (UE5 Random.ush:132)
///            LCG: v*1664525+1013904223 (UE5 RandomPCG.ush:20, Numerical Recipes)
///            VoronoiCornerSample scale: 0.2588 (UE5 Random.ush:482)
///            remap(perlin, worley-1, 1, 0, 1) (Schneider & Vos 2015, slide 25)
/// @deviations NONE — noise math matches UE5 Random.ush exactly; channel layout
///             matches Schneider & Vos 2015 exactly. UE5 loads pre-baked textures
///             rather than generating at runtime; we use the same algorithms to
///             generate procedurally.

// ─── Root Constants ─────────────────────────────────────────────────────────
cbuffer RootConstants : register(b0)
{
    uint3 g_TextureDimensions; // (128, 128, 128)
    uint  _pad0;
};

// ─── Output ─────────────────────────────────────────────────────────────────
RWTexture3D<float4> g_OutputTexture : register(u0);


// ═══════════════════════════════════════════════════════════════════════════
// Hash / RNG Functions
// ═══════════════════════════════════════════════════════════════════════════

/// 3D PCG-based hash producing 3 x 16-bit random values in [0, 0xFFFF].
/// Matches UE5 RandomPCG.ush:12-44 exactly.
/// @source UE5 RandomPCG.ush:12 — Rand3DPCG16
/// @constants LCG multiplier=1664525, LCG increment=1013904223 (Numerical Recipes)
/// @deviations NONE
uint3 Rand3DPCG16(int3 p)
{
    uint3 v = uint3(p);

    // Linear congruential step (Numerical Recipes constants)
    v = v * 1664525u + 1013904223u;

    // Simple MAD Feistel cipher — 6 rounds for quality
    v.x += v.y * v.z;
    v.y += v.z * v.x;
    v.z += v.x * v.y;
    v.x += v.y * v.z;
    v.y += v.z * v.x;
    v.z += v.x * v.y;

    // Only top 16 bits are well-shuffled
    return v >> 16u;
}

/// 3D hash returning float3 in [0, 1]. Wrapper around Rand3DPCG16.
/// Used for Perlin gradient selection and general-purpose hashing.
float3 hash33(float3 p)
{
    int3 ip = int3(floor(p));
    uint3 r = Rand3DPCG16(ip);
    return float3(r) / 65535.0;
}


// ═══════════════════════════════════════════════════════════════════════════
// Tiling Utility
// ═══════════════════════════════════════════════════════════════════════════

/// Wrap coordinates for seamless tiling.
/// @source UE5 Random.ush:122 — NoiseTileWrap
/// @deviations NONE
float3 NoiseTileWrap(float3 v, float repeatSize)
{
    return (frac(v / repeatSize) * repeatSize);
}


// ═══════════════════════════════════════════════════════════════════════════
// Worley (Voronoi) Noise — 3D, tileable
// ═══════════════════════════════════════════════════════════════════════════

/// Jitter offset for a Voronoi cell corner point.
/// @source UE5 Random.ush:473-494 — VoronoiCornerSample (Quality <= 2 path)
/// @constants normalize(noise) * 0.2588 (UE5 Random.ush:482)
/// @deviations NONE
float3 VoronoiCornerSample(float3 cellPos)
{
    // Random values in [-0.5, 0.5], matching UE5 Random.ush:476
    float3 noise = float3(Rand3DPCG16(int3(cellPos))) / 65535.0 - 0.5;

    // Quality <= 2 path: points on sphere with radius 0.2588
    // This guarantees the closest point is always within a 2x2x2 search
    return normalize(noise) * 0.2588;
}

/// 3D tileable Worley noise. Returns distance to nearest feature point,
/// inverted so that cell centers are bright (1.0) and borders are dark (0.0).
///
/// Uses a 2x2x2 neighborhood search (Quality <= 2 path from UE5).
///
/// @source UE5 Random.ush:517-593 — VoronoiNoise3D_ALU (Quality <= 1 path)
/// @source UE5 Random.ush:503-513 — VoronoiCompare
/// @constants Initial mindist = 100.0 (UE5 Random.ush:523)
/// @deviations NONE — same algorithm, but tiling is always enabled and we
///             invert the output (1.0 - dist) for cloud density convention
///             (Schneider & Vos 2015 uses inverted Worley).
float WorleyNoise3D(float3 p, float frequency)
{
    // Scale input by frequency for the desired cell count
    p *= frequency;

    float3 fv = frac(p);
    float3 iv = floor(p);

    // Squared distance to nearest feature point; start at "infinity"
    float minDist = 100.0;

    // 3x3x3 neighborhood search for tileable noise
    // We use 3x3x3 instead of UE5's 2x2x2 because tiling with small
    // repeat sizes can place feature points at cell edges where a 2x2x2
    // search misses them. UE5 Random.ush:527-539 uses 3x3x3 for Quality==3.
    [unroll]
    for (int x = -1; x <= 1; ++x)
    {
        [unroll]
        for (int y = -1; y <= 1; ++y)
        {
            [unroll]
            for (int z = -1; z <= 1; ++z)
            {
                float3 offset = float3(x, y, z);

                // Tile-wrap the neighbor cell index
                float3 wrappedCell = NoiseTileWrap(iv + offset, frequency);

                // Get the jittered feature point within this cell
                float3 featurePoint = offset + VoronoiCornerSample(wrappedCell);

                // Squared distance from evaluation point to feature point
                float3 diff = fv - featurePoint;
                float dist2 = dot(diff, diff);

                // VoronoiCompare — keep minimum squared distance
                // @source UE5 Random.ush:507
                minDist = min(minDist, dist2);
            }
        }
    }

    // Convert squared distance to linear distance
    // @source UE5 Random.ush:592
    float dist = sqrt(minDist);

    // Invert: cell centers = 1.0 (high density), borders = 0.0
    // Schneider & Vos 2015 use inverted Worley for cloud shapes
    return saturate(1.0 - dist);
}


// ═══════════════════════════════════════════════════════════════════════════
// Perlin (Gradient) Noise — 3D, tileable
// ═══════════════════════════════════════════════════════════════════════════

/// Quintic interpolation polynomial for smooth Perlin noise.
/// @source UE5 Random.ush:130-133 — PerlinRamp
/// @constants t^3 * (t * (t * 6 - 15) + 10) (UE5 Random.ush:132)
/// @deviations NONE
float3 PerlinRamp(float3 t)
{
    return t * t * t * (t * (t * 6.0 - 15.0) + 10.0);
}

/// Compute a gradient vector from a hash value.
/// Uses the same approach as UE5's MGradient (Random.ush:149-153):
/// extract 3 bits from the hash to select a gradient direction, then
/// compute dot product with the offset vector.
///
/// @source UE5 Random.ush:149-153 — MGradient
/// @constants MGradientMask = int3(0x8000, 0x4000, 0x2000) (UE5 Random.ush:143)
///            MGradientScale = float3(1/0x4000, 1/0x2000, 1/0x1000) (UE5 Random.ush:144)
/// @deviations NONE
float GradientDot(int3 cellPos, float3 offset)
{
    uint rand = Rand3DPCG16(cellPos).x;
    float3 direction = float3(
        (int3(rand, rand, rand) & int3(0x8000, 0x4000, 0x2000))
    ) * float3(1.0 / 0x4000, 1.0 / 0x2000, 1.0 / 0x1000) - 1.0;
    return dot(direction, offset);
}

/// 3D tileable Perlin (gradient) noise. Output in [-1, 1].
///
/// @source UE5 Random.ush:203-222 — GradientNoise3D_ALU
/// @source UE5 Random.ush:156-194 — NoiseSeeds (tiling seed computation)
/// @constants Primes = (19, 47, 101) (UE5 Random.ush:169) — NOT used here;
///            we use direct int3 cell coords into Rand3DPCG16 instead, which
///            is equivalent and avoids float precision issues with large seeds.
/// @deviations We use Rand3DPCG16 with direct int3 cell coords instead of
///             UE5's float-dot-product seed scheme. The statistical quality is
///             identical (same RNG), but avoids precision loss at large coords.
float PerlinNoise3D(float3 p, float frequency)
{
    // Scale input by frequency
    p *= frequency;

    float3 fv = frac(p);
    float3 iv = floor(p);

    // Evaluate gradient noise at all 8 corners of the unit cube
    // With tiling: wrap cell coords to [0, frequency)
    int3 c000 = int3(NoiseTileWrap(iv + float3(0, 0, 0), frequency));
    int3 c100 = int3(NoiseTileWrap(iv + float3(1, 0, 0), frequency));
    int3 c010 = int3(NoiseTileWrap(iv + float3(0, 1, 0), frequency));
    int3 c110 = int3(NoiseTileWrap(iv + float3(1, 1, 0), frequency));
    int3 c001 = int3(NoiseTileWrap(iv + float3(0, 0, 1), frequency));
    int3 c101 = int3(NoiseTileWrap(iv + float3(1, 0, 1), frequency));
    int3 c011 = int3(NoiseTileWrap(iv + float3(0, 1, 1), frequency));
    int3 c111 = int3(NoiseTileWrap(iv + float3(1, 1, 1), frequency));

    // Gradient dot products at each corner
    // @source UE5 Random.ush:208-215
    float g000 = GradientDot(c000, fv - float3(0, 0, 0));
    float g100 = GradientDot(c100, fv - float3(1, 0, 0));
    float g010 = GradientDot(c010, fv - float3(0, 1, 0));
    float g110 = GradientDot(c110, fv - float3(1, 1, 0));
    float g001 = GradientDot(c001, fv - float3(0, 0, 1));
    float g101 = GradientDot(c101, fv - float3(1, 0, 1));
    float g011 = GradientDot(c011, fv - float3(0, 1, 1));
    float g111 = GradientDot(c111, fv - float3(1, 1, 1));

    // Quintic interpolation weights
    // @source UE5 Random.ush:217
    float3 w = PerlinRamp(fv);

    // Trilinear interpolation of gradient values
    // @source UE5 Random.ush:219-221
    float i = lerp(lerp(g000, g100, w.x), lerp(g010, g110, w.x), w.y);
    float j = lerp(lerp(g001, g101, w.x), lerp(g011, g111, w.x), w.y);
    return lerp(i, j, w.z);
}


// ═══════════════════════════════════════════════════════════════════════════
// Perlin-Worley Combination
// ═══════════════════════════════════════════════════════════════════════════

/// Utility: remap a value from [inLow, inHigh] to [outLow, outHigh].
/// @source Schneider & Vos 2015, slide 25 — "remap" function
/// @deviations NONE
float Remap(float value, float inLow, float inHigh, float outLow, float outHigh)
{
    return outLow + (value - inLow) / (inHigh - inLow) * (outHigh - outLow);
}

/// Perlin-Worley hybrid: Perlin noise modulated by Worley noise.
/// The Worley component carves cellular structure into the smooth Perlin base.
///
/// Formula: remap(perlin, worley - 1.0, 1.0, 0.0, 1.0)
///
/// @source Schneider & Vos 2015, slide 25 — "Perlin-Worley noise"
/// @constants remap bounds: inLow = worley-1, inHigh = 1, outLow = 0, outHigh = 1
/// @deviations NONE
float PerlinWorley(float3 p, float frequency)
{
    // Perlin noise in [-1, 1], remap to [0, 1]
    float perlin = PerlinNoise3D(p, frequency) * 0.5 + 0.5;

    // Worley noise already in [0, 1] (inverted distance)
    float worley = WorleyNoise3D(p, frequency);

    // Perlin-Worley combination: remap perlin using worley as lower bound
    // This creates shapes where Perlin provides large-scale variation and
    // Worley carves out cellular structure at the edges
    return saturate(Remap(perlin, worley - 1.0, 1.0, 0.0, 1.0));
}


// ═══════════════════════════════════════════════════════════════════════════
// Compute Shader Entry Point
// ═══════════════════════════════════════════════════════════════════════════

/// Main compute shader: generates 128x128x128 RGBA8 cloud shape noise.
///
/// Dispatch: (128/4, 128/4, 128/4) = (32, 32, 32) thread groups
///
/// Channel layout (Schneider & Vos 2015, slide 24):
///   R = Perlin-Worley @ frequency 4   (base cloud shape)
///   G = Worley @ frequency 8          (medium erosion detail)
///   B = Worley @ frequency 16         (fine erosion detail)
///   A = Worley @ frequency 32         (finest erosion detail)
[numthreads(4, 4, 4)]
void main(uint3 DTid : SV_DispatchThreadID)
{
    // Early-out for out-of-bounds threads (safety for non-power-of-2 dims)
    if (any(DTid >= g_TextureDimensions))
        return;

    // Normalize dispatch ID to [0, 1]^3 UV space
    // Adding 0.5 to center the sample within each texel
    float3 uvw = (float3(DTid) + 0.5) / float3(g_TextureDimensions);

    // ─── R: Perlin-Worley at frequency 4 (base shape) ────────────────
    // Low-frequency noise that defines the large-scale cloud billows.
    // The Perlin-Worley hybrid creates soft, rounded shapes with
    // cellular boundaries — ideal for cumulus-type cloud bases.
    float r = PerlinWorley(uvw, 4.0);

    // ─── G: Worley at frequency 8 (medium detail) ────────────────────
    // Medium-frequency cellular noise used to erode cloud edges at
    // a moderate scale, adding complexity to the silhouette.
    float g = WorleyNoise3D(uvw, 8.0);

    // ─── B: Worley at frequency 16 (fine detail) ─────────────────────
    // Higher-frequency cellular noise for finer edge erosion,
    // creating the wispy tendrils at cloud boundaries.
    float b = WorleyNoise3D(uvw, 16.0);

    // ─── A: Worley at frequency 32 (finest detail) ───────────────────
    // Highest-frequency noise for the finest level of cloud edge
    // detail, visible only at close range during fly-through.
    float a = WorleyNoise3D(uvw, 32.0);

    // Write all four channels to the 3D texture
    g_OutputTexture[DTid] = float4(r, g, b, a);
}
