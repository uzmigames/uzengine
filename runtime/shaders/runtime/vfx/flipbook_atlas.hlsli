// ─────────────────────────────────────────────────────────────────────────────
// flipbook_atlas.hlsli — Single source of truth for sprite-sheet atlas math.
//
// @ref Niagara NiagaraSpriteVertexFactory.ush — SubUV VS pre-computation
// @ref Nguyen 2007 "GPU Gems 3" ch.23 — SubUV sprite sheet animation
//
// Used by:
//   - particle_billboard_vs.hlsl  (per-vertex atlas UV pre-compute)
//   - flipbook_debug_ps.hlsl      (debug overlay cycling atlas)
//
// Keeping the math in one header ensures the debug quad and the particle
// pipeline always sample the same tile for the same `subImageIndex`.
// ─────────────────────────────────────────────────────────────────────────────

#ifndef UZ_VFX_FLIPBOOK_ATLAS_HLSLI
#define UZ_VFX_FLIPBOOK_ATLAS_HLSLI

// Niagara pattern: decompose a floating `subImageIndex` into an integer frame
// pair plus a fractional blend factor, wrapping at `totalFrames`.
//
//   frameA = floor(subImageIndex) % totalFrames
//   frameB = (frameA + 1)          % totalFrames
//   lerpT  = frac(subImageIndex)
//
// `totalFrames` is clamped to >=1 to guarantee non-zero modulus.
void ComputeFlipbookFrames(
    float  subImageIndex,
    uint   totalFrames,
    out uint frameA,
    out uint frameB,
    out float lerpT)
{
    uint framesClamped = max(totalFrames, 1u);
    frameA = (uint)floor(subImageIndex) % framesClamped;
    frameB = (frameA + 1u) % framesClamped;
    lerpT  = frac(subImageIndex);
}

// Compute the atlas UV for a single vertex of a quad representing `frameIndex`
// inside a `rows`x`cols` sprite sheet. `cornerUV` selects which of the quad's
// 4 corners this vertex represents — caller supplies `(0,0) (1,0) (0,1) (1,1)`.
//
// The atlas is laid out left-to-right, top-to-bottom:
//
//   col = frameIndex % cols
//   row = frameIndex / cols
//   uv  = (col + cornerU, row + cornerV) * (1/cols, 1/rows)
//
// Rows/cols are clamped to >=1 to avoid divide-by-zero on unconfigured atlases.
float2 AtlasUV(
    uint   frameIndex,
    uint   rows,
    uint   cols,
    float2 cornerUV)
{
    uint  colsClamped = max(cols, 1u);
    uint  rowsClamped = max(rows, 1u);
    float invCols     = 1.0f / (float)colsClamped;
    float invRows     = 1.0f / (float)rowsClamped;

    float col = (float)(frameIndex % colsClamped);
    float row = (float)(frameIndex / colsClamped);

    return (float2(col, row) + cornerUV) * float2(invCols, invRows);
}

#endif // UZ_VFX_FLIPBOOK_ATLAS_HLSLI
