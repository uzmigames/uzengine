/// VFX Flipbook / SubUV Animation Utilities
///
/// Computes sprite sheet frame UVs from particle age, lifetime, and sheet
/// dimensions. Supports animated (FPS-driven) and random (per-particle hash)
/// frame selection with optional inter-frame blending.
///
/// @ref Nguyen 2007 "GPU Gems 3" ch. 23 — particle system rendering
/// @ref McGuire 2006 "Particle Systems" — sprite sheet animation fundamentals
/// @constants Row-major frame ordering, left-to-right top-to-bottom.
///            Wang hash: (seed ^ 61) ^ (seed >> 16), *9, ^(seed >> 4),
///            *0x27d4eb2d, ^(seed >> 15).

#ifndef VFX_FLIPBOOK_HLSLI
#define VFX_FLIPBOOK_HLSLI

// ─────────────────────────────────────────────────────────────────────────────
// FlipbookResult — output of all flipbook frame computation functions
// ─────────────────────────────────────────────────────────────────────────────

/// Holds the UV coordinates and blend factor for a flipbook frame lookup.
///
/// The caller decides how to consume the result:
/// - Linear blend: `lerp(tex.Sample(samp, uv0), tex.Sample(samp, uv1), blendFactor)`
/// - No blend:     use `uv0` only, ignore `uv1` and `blendFactor`.
struct FlipbookResult
{
    float2 uv0;         ///< Top-left UV of the current frame in atlas space.
    float2 uv1;         ///< Top-left UV of the next frame (for interpolation).
    float  blendFactor; ///< Lerp factor between uv0 and uv1, in [0, 1).
    float2 frameSize;   ///< Size of one frame in UV space: (1/cols, 1/rows).
};

// ─────────────────────────────────────────────────────────────────────────────
// Internal helpers
// ─────────────────────────────────────────────────────────────────────────────

/// Converts a linear frame index to the top-left UV in atlas space.
///
/// Frame ordering is row-major, left-to-right, top-to-bottom:
///   Frame 0 = (col=0, row=0), Frame 1 = (col=1, row=0), ...,
///   Frame cols = (col=0, row=1), etc.
///
/// @param frameIndex  Linear frame index in [0, totalFrames).
/// @param cols        Number of columns in the sprite sheet. Must be >= 1.
/// @param invCols     Precomputed 1.0 / cols.
/// @param invRows     Precomputed 1.0 / rows.
/// @return            Top-left UV of the frame cell in [0,1] atlas space.
float2 FrameIndexToUV(uint frameIndex, uint cols, float invCols, float invRows)
{
    uint col = frameIndex % cols;
    uint row = frameIndex / cols;
    return float2((float)col * invCols, (float)row * invRows);
}

/// Wang hash — deterministic uint-to-uint hash with good avalanche properties.
///
/// Produces a well-distributed pseudo-random uint from any input seed.
/// Same constants used in particle_mesh_instance_cs.hlsl for rotation hashing.
///
/// @param seed  Input seed value (e.g., particle index).
/// @return      Hashed uint with uniform bit distribution.
uint WangHash(uint seed)
{
    seed = (seed ^ 61u) ^ (seed >> 16u);
    seed *= 9u;
    seed = seed ^ (seed >> 4u);
    seed *= 0x27d4eb2du;
    seed = seed ^ (seed >> 15u);
    return seed;
}

// ─────────────────────────────────────────────────────────────────────────────
// ComputeFlipbookFrame — animated frame from particle age
// ─────────────────────────────────────────────────────────────────────────────

/// Computes flipbook frame UVs from particle age and playback rate.
///
/// @ref Nguyen 2007 "GPU Gems 3" ch. 23 — standard SubUV sprite sheet animation
/// @constants Row-major frame ordering, linear interpolation between adjacent frames
///
/// The animation loops: when the last frame is reached, it wraps to frame 0.
/// The blend factor is the fractional part of the continuous frame index, giving
/// smooth inter-frame transitions when the caller performs a linear blend.
///
/// # Parameters
/// @param age         Current particle age in seconds. Clamped to >= 0.
/// @param lifetime    Total particle lifetime in seconds. If <= 0, returns frame 0.
/// @param rows        Number of rows in the sprite sheet (e.g., 4). Must be >= 1.
/// @param cols        Number of columns in the sprite sheet (e.g., 4). Must be >= 1.
/// @param fps         Playback speed in frames per second (e.g., 30). If <= 0,
///                    returns frame 0.
/// @param totalFrames Total number of valid frames in the sheet. Typically
///                    rows * cols, but may be less for partially filled sheets.
///                    If 0, returns frame 0.
///
/// # Frame Ordering
/// Row-major, left-to-right, top-to-bottom:
///   Frame 0 = (col=0, row=0), Frame 1 = (col=1, row=0), ...
///   Frame cols = (col=0, row=1), ...
///
/// # Blend Mode
/// The caller decides how to use blendFactor:
/// - Linear: lerp(sample(uv0), sample(uv1), blendFactor)
/// - None:   use uv0 only (ignore uv1 and blendFactor)
///
/// @return FlipbookResult with UVs for the current and next frames.
FlipbookResult ComputeFlipbookFrame(
    float age,
    float lifetime,
    uint  rows,
    uint  cols,
    float fps,
    uint  totalFrames)
{
    FlipbookResult result;

    // Guard against degenerate inputs: zero dimensions, zero frames, or
    // non-positive playback rate. Return the first frame with no blending.
    if (totalFrames == 0u || cols == 0u || rows == 0u || fps <= 0.0)
    {
        result.uv0         = float2(0.0, 0.0);
        result.uv1         = float2(0.0, 0.0);
        result.blendFactor = 0.0;
        // Avoid division by zero: if cols or rows is 0, frameSize is 0.
        result.frameSize   = float2(
            cols > 0u ? 1.0 / (float)cols : 0.0,
            rows > 0u ? 1.0 / (float)rows : 0.0);
        return result;
    }

    float invCols = 1.0 / (float)cols;
    float invRows = 1.0 / (float)rows;
    result.frameSize = float2(invCols, invRows);

    // Continuous frame index from elapsed time and playback rate.
    // max(age, 0) prevents negative ages from producing negative frame indices.
    float frameFloat = max(age, 0.0) * fps;

    // Current and next frame indices, wrapping around totalFrames.
    uint frame0 = (uint)frameFloat % totalFrames;
    uint frame1 = (frame0 + 1u) % totalFrames;

    // Fractional part gives the inter-frame blend weight.
    result.blendFactor = frac(frameFloat);

    // Convert linear frame indices to top-left UV coordinates.
    result.uv0 = FrameIndexToUV(frame0, cols, invCols, invRows);
    result.uv1 = FrameIndexToUV(frame1, cols, invCols, invRows);

    return result;
}

// ─────────────────────────────────────────────────────────────────────────────
// SampleFlipbookUV — transform billboard UV to atlas sub-rectangle
// ─────────────────────────────────────────────────────────────────────────────

/// Transforms a [0,1] base UV to the correct sub-rectangle for a given frame.
///
/// The base UV comes from the particle billboard or mesh UV channel and covers
/// the full [0,1]x[0,1] range. This function remaps it into the sub-tile
/// occupied by the target frame in the sprite sheet atlas.
///
/// @param baseUV    Particle billboard UV in [0,1]x[0,1].
/// @param frameUV   Top-left corner of the target frame (from FlipbookResult.uv0
///                  or FlipbookResult.uv1).
/// @param frameSize Size of one frame in UV space (from FlipbookResult.frameSize).
/// @return          Atlas UV that samples the correct frame sub-rectangle.
float2 SampleFlipbookUV(float2 baseUV, float2 frameUV, float2 frameSize)
{
    return frameUV + baseUV * frameSize;
}

// ─────────────────────────────────────────────────────────────────────────────
// ComputeFlipbookFrameRandom — deterministic random frame per particle
// ─────────────────────────────────────────────────────────────────────────────

/// Selects a deterministic random frame per particle (not animated).
///
/// @ref Nguyen 2007 "GPU Gems 3" ch. 23 — particle variety via sprite sheet randomization
/// @constants Wang hash: (seed ^ 61) ^ (seed >> 16), *9, ^(seed >> 4), *0x27d4eb2d, ^(seed >> 15)
///
/// Used for variety rendering: e.g., 8 different leaf sprites where each
/// particle gets a random frame based on a hash of its unique index.
/// The result is deterministic -- the same particleIndex always maps to the
/// same frame, so the selection is stable across frames.
///
/// blendFactor is always 0 (no animation), and uv1 equals uv0 (no next frame).
///
/// # Parameters
/// @param particleIndex Unique particle ID for deterministic hashing. Any uint.
/// @param rows          Number of rows in the sprite sheet. Must be >= 1.
/// @param cols          Number of columns in the sprite sheet. Must be >= 1.
/// @param totalFrames   Number of valid frames in the sheet. If 0, returns frame 0.
///
/// @return FlipbookResult with a single randomly selected frame (no blending).
FlipbookResult ComputeFlipbookFrameRandom(
    uint particleIndex,
    uint rows,
    uint cols,
    uint totalFrames)
{
    FlipbookResult result;

    // Guard against degenerate inputs.
    if (totalFrames == 0u || cols == 0u || rows == 0u)
    {
        result.uv0         = float2(0.0, 0.0);
        result.uv1         = float2(0.0, 0.0);
        result.blendFactor = 0.0;
        result.frameSize   = float2(
            cols > 0u ? 1.0 / (float)cols : 0.0,
            rows > 0u ? 1.0 / (float)rows : 0.0);
        return result;
    }

    float invCols = 1.0 / (float)cols;
    float invRows = 1.0 / (float)rows;
    result.frameSize = float2(invCols, invRows);

    // Deterministic frame selection via Wang hash of the particle index.
    uint hashed = WangHash(particleIndex);
    uint frame  = hashed % totalFrames;

    result.uv0 = FrameIndexToUV(frame, cols, invCols, invRows);
    result.uv1 = result.uv0; // No next frame — static selection.
    result.blendFactor = 0.0; // No inter-frame blending.

    return result;
}

#endif // VFX_FLIPBOOK_HLSLI
