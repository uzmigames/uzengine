/// Motion Blur Neighbor Max — Compute Shader
///
/// For each tile, finds the maximum velocity from its 3x3 neighborhood in
/// the tile max texture. This ensures that fast-moving objects produce blur
/// streaks that extend into neighboring tiles, preventing hard tile-boundary
/// artifacts in the per-pixel motion blur gather pass.
///
/// # Algorithm
/// 1. Each thread corresponds to one tile in the tile max texture.
/// 2. Sample the 3x3 neighborhood (9 tiles including self).
/// 3. Keep the velocity with the largest squared magnitude.
/// 4. Output the winning velocity to the neighbor max texture.
///
/// uses a more sophisticated approach in MotionBlurTileGather.usf that
/// considers velocity direction and projects tile offsets onto the velocity
/// vector to determine which neighbors are relevant (scatter-as-gather with
/// oriented quad intersection). Our simplified 3x3 approach is sufficient
/// for the initial implementation and produces correct blur extension at
/// tile boundaries.
///
/// # Resources
/// - Root constants at b0 (4 DWORDs): tileMaxSrvIndex, outputUavIndex,
///   tileCountX, tileCountY.
/// - No sampler needed (point-load via Load()).
///
/// @ref McGuire et al. 2012 — neighbor max tile gather:24-91 — MainCS
/// @ref Motion blur common —24 — VELOCITY_FLATTEN_TILE_SIZE = 16
/// @constants TILE_SIZE=16 (MotionBlurCommon.ush:24)
/// @deviations the reference uses a 7x7 neighborhood (-3..+3) with velocity-direction-aware
///             quad intersection test to determine which neighbors contribute
///             (MotionBlurTileGather.usf:37-83). UzEngine uses a simpler 3x3
///             neighborhood (-1..+1) with unconditional max, which is conservative
///             (may include slightly more tiles) but correct for preventing
///             tile-boundary blur cutoff.

// ─────────────────────────────────────────────────────────────────────────────
// Parameters
// ─────────────────────────────────────────────────────────────────────────────

/// Root constants at b0 — 4 DWORDs.
cbuffer NeighborMaxParams : register(b0) {
    uint tileMaxSrvIndex;   ///< Descriptor heap index: tile max texture SRV (RG16F).
    uint outputUavIndex;    ///< Descriptor heap index: neighbor max output UAV (RG16F).
    uint tileCountX;        ///< Number of tiles horizontally.
    uint tileCountY;        ///< Number of tiles vertically.
};

// ─────────────────────────────────────────────────────────────────────────────
// SM5 Fallback Resources
// ─────────────────────────────────────────────────────────────────────────────

#ifndef UZ_BINDLESS
Texture2D<float2>   tileMaxTex       : register(t0);
RWTexture2D<float2> neighborMaxOut   : register(u0);
#endif

// ─────────────────────────────────────────────────────────────────────────────
// Entry Point
// ─────────────────────────────────────────────────────────────────────────────

/// @ref McGuire et al. 2012 — neighbor max tile gather:24-91 — MainCS
/// @deviations 3x3 neighborhood instead of the reference engine's 7x7 direction-aware quad test.
[numthreads(8, 8, 1)]
void main(uint3 dtid : SV_DispatchThreadID)
{
    // ── Bounds check ─────────────────────────────────────────────────────
    if (dtid.x >= tileCountX || dtid.y >= tileCountY)
        return;

    // ── Resource access ──────────────────────────────────────────────────
#ifdef UZ_BINDLESS
    Texture2D<float2>   tileMaxTex     = ResourceDescriptorHeap[tileMaxSrvIndex];
    RWTexture2D<float2> neighborMaxOut = ResourceDescriptorHeap[outputUavIndex];
#endif

    // ── Find max velocity in 3x3 neighborhood ───────────────────────────
    // @ref McGuire et al. 2012 — neighbor max tile gather:37-84 — 7x7 scatter-as-gather loop
    // We use a simpler 3x3 kernel that is conservative but correct.
    float2 maxVel = float2(0.0, 0.0);
    float maxMagSq = 0.0;

    [unroll]
    for (int dy = -1; dy <= 1; dy++)
    {
        [unroll]
        for (int dx = -1; dx <= 1; dx++)
        {
            int2 coord = int2(dtid.xy) + int2(dx, dy);

            // Clamp to tile texture bounds.
            coord = clamp(coord, int2(0, 0), int2(tileCountX - 1, tileCountY - 1));

            float2 vel = tileMaxTex.Load(int3(coord, 0));
            float magSq = dot(vel, vel);

            if (magSq > maxMagSq)
            {
                maxVel = vel;
                maxMagSq = magSq;
            }
        }
    }

    // ── Write result ─────────────────────────────────────────────────────
    neighborMaxOut[dtid.xy] = maxVel;
}
