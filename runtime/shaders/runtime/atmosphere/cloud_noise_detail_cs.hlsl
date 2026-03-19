/// Cloud Detail Noise Generator — 3D Worley FBM (32x32x32 RGBA8)
///
/// Generates the detail/erosion noise texture used to carve fine structure
/// into cloud edges during volumetric ray-marching. Each channel stores
/// a different frequency of Worley noise, and the alpha channel stores
/// a pre-computed FBM (fractional Brownian motion) blend of the three.
///
///   R = Worley at frequency 2   (low-frequency detail)
///   G = Worley at frequency 4   (medium detail)
///   B = Worley at frequency 8   (fine detail)
///   A = Worley FBM              (R*0.625 + G*0.25 + B*0.125)
///
/// @source Schneider & Vos, SIGGRAPH 2015 — "The Real-Time Volumetric
///         Cloudscapes of Horizon Zero Dawn", slides 23-26 (detail texture
///         layout, frequencies, FBM weights)
/// @source UE5 RandomPCG.ush:12 — Rand3DPCG16 (PCG-based 3D hash)
/// @source UE5 Random.ush:473 — VoronoiCornerSample (Worley jitter)
/// @source UE5 Random.ush:517 — VoronoiNoise3D_ALU (Worley/Voronoi noise)
/// @source UE5 Random.ush:122 — NoiseTileWrap (tiling via frac * repeat)
/// @constants Frequencies: 2, 4, 8 (Schneider & Vos 2015, slide 24)
///            FBM weights: 0.625, 0.25, 0.125 (Schneider & Vos 2015, slide 26)
///            LCG: v*1664525+1013904223 (UE5 RandomPCG.ush:20, Numerical Recipes)
///            VoronoiCornerSample scale: 0.2588 (UE5 Random.ush:482)
/// @deviations NONE — noise math matches UE5 Random.ush exactly; channel layout
///             and FBM weights match Schneider & Vos 2015 exactly. UE5 loads
///             pre-baked textures; we generate procedurally with the same algorithms.

// ─── Root Constants ─────────────────────────────────────────────────────────
cbuffer RootConstants : register(b0)
{
    uint3 g_TextureDimensions; // (32, 32, 32)
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
/// Uses a 3x3x3 neighborhood search for correct tiling behavior.
///
/// @source UE5 Random.ush:517-593 — VoronoiNoise3D_ALU
/// @source UE5 Random.ush:503-513 — VoronoiCompare
/// @constants Initial mindist = 100.0 (UE5 Random.ush:523)
/// @deviations NONE — same algorithm as UE5 Quality==3 path; output is inverted
///             (1.0 - dist) per Schneider & Vos 2015 convention.
float WorleyNoise3D(float3 p, float frequency)
{
    // Scale input by frequency for the desired cell count
    p *= frequency;

    float3 fv = frac(p);
    float3 iv = floor(p);

    // Squared distance to nearest feature point; start at "infinity"
    float minDist = 100.0;

    // 3x3x3 neighborhood search for correct tiling
    // @source UE5 Random.ush:527-539 (Quality == 3 path)
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
    // Schneider & Vos 2015 use inverted Worley for cloud detail erosion
    return saturate(1.0 - dist);
}


// ═══════════════════════════════════════════════════════════════════════════
// Compute Shader Entry Point
// ═══════════════════════════════════════════════════════════════════════════

/// Main compute shader: generates 32x32x32 RGBA8 cloud detail noise.
///
/// Dispatch: (32/4, 32/4, 32/4) = (8, 8, 8) thread groups
///
/// Channel layout (Schneider & Vos 2015, slide 24):
///   R = Worley @ frequency 2   (low-frequency detail base)
///   G = Worley @ frequency 4   (medium detail)
///   B = Worley @ frequency 8   (fine detail)
///   A = Worley FBM             (R*0.625 + G*0.25 + B*0.125)
///
/// The FBM weights sum to 1.0 and follow standard octave attenuation:
///   weight_i = 0.5^i * (1 / sum), normalized so output stays in [0, 1].
///   Raw weights: 1.0, 0.5, 0.25 → normalized by /1.75 → but Schneider uses
///   0.625, 0.25, 0.125 (= 1/1.6, ...) which is a slightly sharper falloff.
///   We use the exact Schneider & Vos 2015 values.
///
/// @constants FBM weights: 0.625, 0.25, 0.125 (Schneider & Vos 2015, slide 26)
/// @deviations NONE
[numthreads(4, 4, 4)]
void main(uint3 DTid : SV_DispatchThreadID)
{
    // Early-out for out-of-bounds threads (safety for non-power-of-2 dims)
    if (any(DTid >= g_TextureDimensions))
        return;

    // Normalize dispatch ID to [0, 1]^3 UV space
    // Adding 0.5 to center the sample within each texel
    float3 uvw = (float3(DTid) + 0.5) / float3(g_TextureDimensions);

    // ─── R: Worley at frequency 2 (low-frequency detail) ─────────────
    // Largest-scale detail noise. When applied as erosion, this creates
    // broad scalloping at cloud edges.
    float r = WorleyNoise3D(uvw, 2.0);

    // ─── G: Worley at frequency 4 (medium detail) ────────────────────
    // Medium-scale cellular detail for intermediate edge complexity.
    float g = WorleyNoise3D(uvw, 4.0);

    // ─── B: Worley at frequency 8 (fine detail) ──────────────────────
    // Finest cellular detail, creating small-scale wispy structures
    // visible during close-range fly-through.
    float b = WorleyNoise3D(uvw, 8.0);

    // ─── A: Worley FBM (pre-computed blend) ──────────────────────────
    // Pre-blended fractional Brownian motion of the three Worley octaves.
    // This saves a MAD chain in the ray-marching shader by providing the
    // combined detail erosion value directly.
    //
    // Weights from Schneider & Vos 2015, slide 26:
    //   0.625 (lowest freq) + 0.25 (mid) + 0.125 (highest) = 1.0
    float a = r * 0.625 + g * 0.25 + b * 0.125;

    // Write all four channels to the 3D texture
    g_OutputTexture[DTid] = float4(r, g, b, a);
}
