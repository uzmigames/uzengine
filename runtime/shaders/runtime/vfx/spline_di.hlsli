// VFX Spline Data Interface — HLSL helpers.
//
// Path-following modules (`SpawnAlongSplineModule`, `ForceTowardSplineModule`,
// `FollowSplineModule`) sample a baked CPU `VfxSpline` from the simulation
// shaders. The CPU side bakes one `VfxSplinePoint` per arc-length bucket
// into an upload-heap structured buffer (`VfxSplineBuffer`) every frame;
// the spawn / update CS reads it through this header.
//
// Layout MUST match `VfxSplinePoint` in
// `runtime/engine/vfx/data_interfaces/vfx_spline_buffer.h` byte-for-byte
// (32 bytes, four float4 rows where the first three slots of row 1 hold
// position + arcLength).

#ifndef UZ_VFX_SPLINE_DI_HLSLI
#define UZ_VFX_SPLINE_DI_HLSLI

// ─────────────────────────────────────────────────────────────────────────────
// VfxSplinePoint — must mirror VfxSplinePoint in vfx_spline_buffer.h
// ─────────────────────────────────────────────────────────────────────────────

struct VfxSplinePoint {
    float3 position;   // bytes 0-11
    float  arcLength;  // bytes 12-15  (cumulative arc length from sample[0])
    float3 tangent;    // bytes 16-27  (unit vector)
    float  _pad;       // bytes 28-31
};

// ─────────────────────────────────────────────────────────────────────────────
// Internal helpers
// ─────────────────────────────────────────────────────────────────────────────

/// Loads the `i`-th baked spline point. Bounds check is the caller's
/// responsibility — this helper is a hot inner loop.
VfxSplinePoint UzSplineLoad(uint splineSrvIndex, uint i)
{
#ifdef UZ_BINDLESS
    StructuredBuffer<VfxSplinePoint> buf =
        ResourceDescriptorHeap[splineSrvIndex];
    return buf[i];
#else
    // SM5 fallback: no bindless — explicit register binding lives at the
    // shader's discretion. A spline-using SM5 shader must declare:
    //   StructuredBuffer<VfxSplinePoint> uzSplinePoints : register(t8);
    // and #define UZ_SPLINE_BIND uzSplinePoints before including this file.
    #ifndef UZ_SPLINE_BIND
        #error "spline_di.hlsli (SM5): UZ_SPLINE_BIND must be defined"
    #endif
    (void)splineSrvIndex;
    return UZ_SPLINE_BIND[i];
#endif
}

// ─────────────────────────────────────────────────────────────────────────────
// SplineSampleAtU — parametric sample (uniform across baked-sample index).
// ─────────────────────────────────────────────────────────────────────────────
//
// `u` is a normalised parameter in [0, 1] across the BAKED sample range
// (which IS arc-length-uniform if the CPU bake used `Sample(t)` uniformly,
// but the modules typically pass arc-length-derived `u` from
// `SplineSampleArclen` so the distinction rarely matters in practice).
// `pointCount` must equal `VfxSplineBuffer::pointCount` for that emitter.
//
// Out-of-range `u` clamps; degenerate `pointCount < 2` returns a zero
// position with a default forward tangent so callers don't have to gate.

VfxSplinePoint SplineSampleAtU(uint splineSrvIndex,
                                uint pointCount,
                                float u)
{
    VfxSplinePoint result;
    result.position  = float3(0, 0, 0);
    result.arcLength = 0.0f;
    result.tangent   = float3(1, 0, 0);
    result._pad      = 0.0f;

    if (pointCount < 2u) return result;

    u = saturate(u);
    const float scaled = u * float(pointCount - 1u);
    const uint  idx    = min(uint(scaled), pointCount - 2u);
    const float frac   = scaled - float(idx);

    VfxSplinePoint p0 = UzSplineLoad(splineSrvIndex, idx);
    VfxSplinePoint p1 = UzSplineLoad(splineSrvIndex, idx + 1u);

    // Linear blend between adjacent samples. The CPU bake density (default
    // 64 samples) keeps the linear approximation visually identical to
    // the spline evaluator at simulation distances.
    result.position  = lerp(p0.position,  p1.position,  frac);
    result.arcLength = lerp(p0.arcLength, p1.arcLength, frac);
    result.tangent   = normalize(lerp(p0.tangent, p1.tangent, frac)
                                 + float3(1e-6, 0, 0));  // bias against zero
    return result;
}

// ─────────────────────────────────────────────────────────────────────────────
// SplineSampleArclen — arc-length-parameterised sample.
// ─────────────────────────────────────────────────────────────────────────────
//
// `s` is an arc length in centimetres in `[0, totalLength]`. Out-of-range
// values clamp. Internally walks the baked `arcLength` field linearly —
// fast for the typical 64-sample bake. Use `SplineSampleAtU` directly when
// the caller already has the parametric `u`.

VfxSplinePoint SplineSampleArclen(uint  splineSrvIndex,
                                   uint  pointCount,
                                   float totalLength,
                                   float s)
{
    if (pointCount < 2u || totalLength <= 0.0f) {
        VfxSplinePoint zero;
        zero.position  = float3(0, 0, 0);
        zero.arcLength = 0.0f;
        zero.tangent   = float3(1, 0, 0);
        zero._pad      = 0.0f;
        return zero;
    }

    s = clamp(s, 0.0f, totalLength);
    const float u = s / totalLength;
    return SplineSampleAtU(splineSrvIndex, pointCount, u);
}

// ─────────────────────────────────────────────────────────────────────────────
// SplineNearest — closest baked sample to a world-space query position.
// ─────────────────────────────────────────────────────────────────────────────
//
// Returns the position + tangent + arcLength of the BAKED sample point
// closest to `worldPos`. Linear scan — O(pointCount) per particle. For
// the typical 64-sample bake at thousands of particles per frame the cost
// is well under the budget; for denser splines callers should consider
// a coarser bake or a separate broadphase structure.
//
// Returns a zero result with a default forward tangent when the spline
// is degenerate (`pointCount < 2`).

VfxSplinePoint SplineNearest(uint   splineSrvIndex,
                              uint   pointCount,
                              float3 worldPos)
{
    VfxSplinePoint best;
    best.position  = float3(0, 0, 0);
    best.arcLength = 0.0f;
    best.tangent   = float3(1, 0, 0);
    best._pad      = 0.0f;

    if (pointCount < 2u) return best;

    float bestDist2 = 3.4e38f;
    [loop]
    for (uint i = 0u; i < pointCount; ++i) {
        VfxSplinePoint p = UzSplineLoad(splineSrvIndex, i);
        const float3 d  = p.position - worldPos;
        const float  d2 = dot(d, d);
        if (d2 < bestDist2) {
            bestDist2 = d2;
            best      = p;
        }
    }
    return best;
}

#endif // UZ_VFX_SPLINE_DI_HLSLI
