/// Motion Blur Tile Max — Compute Shader
///
/// For each 16x16 tile of the velocity buffer, finds the pixel with the
/// maximum velocity magnitude and outputs that velocity vector. This produces
/// a downsampled velocity texture at (width/16) x (height/16) resolution.
///
/// The tile max texture is consumed by the neighbor max pass, which expands
/// the max velocity to a 3x3 tile neighborhood. Together they allow the
/// per-pixel motion blur gather shader to skip tiles with negligible motion.
///
/// # Algorithm
/// 1. Each thread in the 16x16 group loads its pixel's velocity from the
///    full-resolution velocity buffer.
/// 2. Velocity magnitude (squared) is stored in groupshared memory.
/// 3. Parallel reduction finds the pixel with the largest velocity.
/// 4. Thread 0 writes the winning velocity vector to the output tile.
///
/// # Resources
/// - Root constants at b0 (4 DWORDs): velocitySrvIndex, outputUavIndex,
///   tileCountX, tileCountY.
/// - No sampler needed (point-load via Load()).
///
/// @ref McGuire et al. 2012 — tile max velocity flatten:79-165 — MainCS
/// @ref McGuire et al. 2012 — velocity tile reduce:67-135 — ReduceVelocityFlattenTile
/// @ref Motion blur common —24 — VELOCITY_FLATTEN_TILE_SIZE = 16
/// @constants TILE_SIZE=16 (MotionBlurCommon.ush:24),
///            THREAD_COUNT=256 (VELOCITY_FLATTEN_TILE_SIZE^2)
/// @deviations the reference uses polar velocity encoding and multi-direction range tracking
///             (FVelocityRange with CONFIG_MAX_RANGE_SIZE directions). UzEngine uses
///             a simplified Cartesian max-magnitude approach since we only need a
///             single dominant velocity direction per tile for the neighbor max pass.
///             reference also encodes velocity+depth into a flattened texture
///             (MotionBlurVelocityFlatten.ush:137-145); we output raw Cartesian velocity.

// ─────────────────────────────────────────────────────────────────────────────
// Constants
// ─────────────────────────────────────────────────────────────────────────────

/// @ref Motion blur common —24 — VELOCITY_FLATTEN_TILE_SIZE
static const uint TILE_SIZE = 16;
static const uint THREAD_COUNT = TILE_SIZE * TILE_SIZE; // 256

// ─────────────────────────────────────────────────────────────────────────────
// Parameters
// ─────────────────────────────────────────────────────────────────────────────

/// Root constants at b0 — 4 DWORDs.
cbuffer TileMaxParams : register(b0) {
    uint velocitySrvIndex;  ///< Descriptor heap index: velocity buffer (RG16F, pixel-space).
    uint outputUavIndex;    ///< Descriptor heap index: tile max output UAV (RG16F).
    uint tileCountX;        ///< Number of tiles horizontally = ceil(width / 16).
    uint tileCountY;        ///< Number of tiles vertically = ceil(height / 16).
};

// ─────────────────────────────────────────────────────────────────────────────
// SM5 Fallback Resources
// ─────────────────────────────────────────────────────────────────────────────

#ifndef UZ_BINDLESS
Texture2D<float2>   velocityTex     : register(t0);
RWTexture2D<float2> tileMaxOutput   : register(u0);
#endif

// ─────────────────────────────────────────────────────────────────────────────
// Groupshared Memory for Parallel Reduction
// ─────────────────────────────────────────────────────────────────────────────

/// @ref McGuire et al. 2012 — velocity tile reduce:19-20 — SharedArray0/SharedArray1
/// Stores velocity vector and squared magnitude for parallel reduction.
groupshared float2 s_velocity[THREAD_COUNT];
groupshared float  s_magnitudeSq[THREAD_COUNT];

// ─────────────────────────────────────────────────────────────────────────────
// Entry Point
// ─────────────────────────────────────────────────────────────────────────────

/// @ref McGuire et al. 2012 — tile max velocity flatten:79-165 — MainCS
/// @ref McGuire et al. 2012 — velocity tile reduce:67-135 — ReduceVelocityFlattenTile
/// @deviations Simplified: Cartesian max-magnitude reduction instead of polar
///             velocity range with multi-direction support.
[numthreads(TILE_SIZE, TILE_SIZE, 1)]
void main(uint3 dtid : SV_DispatchThreadID, uint gi : SV_GroupIndex, uint3 gid : SV_GroupID)
{
    // ── Load velocity for this pixel ─────────────────────────────────────
#ifdef UZ_BINDLESS
    Texture2D<float2> velocityTex = ResourceDescriptorHeap[velocitySrvIndex];
#endif

    float2 vel = velocityTex.Load(int3(dtid.xy, 0));
    float magSq = dot(vel, vel);

    // ── Store into groupshared for reduction ─────────────────────────────
    s_velocity[gi] = vel;
    s_magnitudeSq[gi] = magSq;
    GroupMemoryBarrierWithGroupSync();

    // ── Parallel reduction to find max velocity magnitude ────────────────
    // @ref McGuire et al. 2012 — velocity tile reduce:88-132 — VelocityFlattenStep cascade
    // uses a cascade of VelocityFlattenStep calls with decreasing group sizes
    // (128, 64, 32, 16, 8, 4, 2, 1). We use the same pattern but simplified to
    // track only a single max velocity vector (not polar range with multiple directions).
    [unroll]
    for (uint stride = THREAD_COUNT / 2; stride > 0; stride >>= 1)
    {
        if (gi < stride)
        {
            if (s_magnitudeSq[gi + stride] > s_magnitudeSq[gi])
            {
                s_velocity[gi] = s_velocity[gi + stride];
                s_magnitudeSq[gi] = s_magnitudeSq[gi + stride];
            }
        }
        GroupMemoryBarrierWithGroupSync();
    }

    // ── Thread 0 writes the tile result ──────────────────────────────────
    // @ref McGuire et al. 2012 — tile max velocity flatten:156-160 — output tile
    if (gi == 0)
    {
        // Bounds check: only write if tile is within the output dimensions.
        if (gid.x < tileCountX && gid.y < tileCountY)
        {
#ifdef UZ_BINDLESS
            RWTexture2D<float2> tileMaxOutput = ResourceDescriptorHeap[outputUavIndex];
#endif
            tileMaxOutput[gid.xy] = s_velocity[0];
        }
    }
}
