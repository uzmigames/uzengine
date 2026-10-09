# VFX HZB occlusion culling

Layer on top of the frustum cull (phase 3.89): an emitter whose bounds are
fully behind opaque geometry skips both sim and render. Expected savings
30–50% on VFX-heavy scenes with lots of occluders (interiors, dense
vegetation, crowds).

Reference: Niemeyer 2007, *Hierarchical Z-Buffer Occlusion Culling*.

## Pipeline

```
  Emitter AABB                HZB (from phase 3.36)
      │                             │
      ▼                             │
┌────────────────────┐              │
│ VfxProjectAABB…    │              │
│ Screen rect +      │              │
│ nearest-corner depth              │
└────────┬───────────┘              │
         ▼                          │
┌────────────────────┐              │
│ VfxSelectHZBMip    │──────────────┤ pick mip whose texel ≥ rect side
└────────┬───────────┘              │
         ▼                          │
  fetch 2×2 at rect centre ◀────────┘  take min (reversed-Z farthest)
         │
         ▼
  VfxIsOccludedByHZB(nearestDepth, hzbDepth, epsilon)
         │
         ▼
  VfxOcclusionHysteresis.Update(vote)
         │
         ▼
  skip sim + render when committed-occluded
```

Every helper is a pure function so tests exercise the logic without a
GPU. Runtime integration with the 3.36 HZB pyramid is a small wrapper
that samples the chosen mip level.

## AABB projection details

- `VfxProjectAABBToScreen` projects all 8 corners, clamps the resulting
  rect to `[0,1]²` in UV space, and returns the nearest reversed-Z depth.
- Y is flipped during the NDC→UV step so V grows downward (DirectX
  convention).
- **Straddle safety.** If any corner has `w ≤ 0` the AABB crosses the
  near plane and the screen projection is unreliable. The helper returns
  a full-screen rect at `nearestDepth = 1.0` so the emitter is treated
  as visible and never falsely culled.
- `offscreen == true` when the rect is entirely outside the screen; the
  caller already has a frustum cull for that case but the flag is
  surfaced for callers that want to double-check.

## Mip selection

`VfxSelectHZBMip` picks the smallest mip whose 2×2 texel footprint
covers the projected rect. Formula:

```
rectMaxTexels = max(rectWidthUV · baseWidth, rectHeightUV · baseHeight)
m = ceil(log2(max(rectMaxTexels / 2, 1)))
mip = clamp(m, 0, mipCount - 1)
```

Pinpoint rects (zero size) stay at mip 0; full-screen rects pick the
highest available mip. Never out of range.

## Occlusion compare

`VfxIsOccludedByHZB(nearestDepth, hzbDepth, epsilon)` uses reversed-Z:
closer = larger value. The emitter is occluded when its nearest corner
is strictly behind the HZB's farthest sample:

```
occluded = nearestDepth + epsilon < hzbDepth
```

`epsilon` (default 5e-4) provides conservative slack against self-
occlusion on planar alignment.

## Hysteresis

Raw per-frame votes thrash when an emitter's screen rect straddles an
occluder edge. `VfxOcclusionHysteresis(confirmFrames)` requires N
consecutive matching votes before flipping the committed state:

- Matching vote → dissent counter resets to 0.
- Disagreeing vote → counter increments.
- Counter reaches `confirmFrames` → committed state flips, counter
  resets.

Default `confirmFrames = 3` (~50 ms at 60 fps) balances pop-in against
responsiveness.

## Tests

`runtime/tests/engine/vfx/test_vfx_hzb_occlusion.cpp` — 16 cases:

- AABB projection: in-front produces non-degenerate rect, far-side
  flagged offscreen, behind-camera returns safe full-screen rect.
- Mip selection: small rect → low mip, full-screen → high mip, pinpoint
  clamps to 0, exceeds-mipCount clamps to max, zero mip count → 0.
- Occlusion compare: nearest-behind-HZB occluded, nearest-in-front not,
  equal depths not (self-guard), epsilon slack prevents self-occlusion.
- Hysteresis: starts visible, flips after N matching votes, dissent
  resets on matching vote, flip-back also needs N, Reset restores
  visible.
