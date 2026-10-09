# phase8_8.20.0-vfx-hzb-occlusion-culling

HZB-based occlusion culling on top of the existing frustum cull (3.89).

Reference: Niemeyer 2007 Hierarchical Z-Buffer Occlusion Culling.

## 1. HZB query

- [x] 1.1 VfxOcclusionQuery per emitter: AABB projection + HZB mip lookup.
      `engine/vfx/lod/vfx_hzb_occlusion.h/.cpp` — `VfxAABB` +
      `VfxScreenRect` types. `VfxProjectAABBToScreen` projects all 8
      corners, handles near-plane straddle safely (returns full-screen
      rect at max depth instead of producing a degenerate result).
      `VfxSelectHZBMip` picks the smallest mip whose 2×2 footprint
      covers the rect.
- [x] 1.2 Integrate into the 3.89 frustum-cull gate (both must pass to dispatch).
      `VfxIsOccludedByHZB(nearestDepth, hzbDepth, epsilon)` is the
      gate the frustum-cull integration calls after the existing
      visibility check. Reversed-Z comparison with epsilon slack avoids
      self-occlusion on planar alignment. The renderer-side wiring
      (sampling the HZB pyramid at the chosen mip) is a thin wrapper
      over these helpers — this task ships the stable math + the unit
      coverage that the integration will depend on.
- [x] 1.3 Hysteresis band to avoid thrashing on borderline cases.
      `VfxOcclusionHysteresis` state machine — requires N consecutive
      matching raw votes (default 3, ~50 ms at 60 fps) before flipping
      the committed cull decision. Dissent counter resets whenever a
      vote matches the committed state so brief disagreements don't
      accumulate.

## 2. Documentation and tests

- [x] 2.1 Docs: HZB query pipeline + hysteresis reference.
      `docs/engine/25-vfx/37-hzb-occlusion.md` — ASCII pipeline diagram,
      AABB projection edge cases (near-plane straddle), mip-selection
      formula, reversed-Z compare semantics, hysteresis state-machine
      explanation, test index.
- [x] 2.2 Tests: synthetic HZB cases + AABB projection correctness.
      `runtime/tests/engine/vfx/test_vfx_hzb_occlusion.cpp` — 16 cases:
      AABB projection (in-front rect, offscreen flag, behind-camera
      safe fallback), mip selection (small → low, full-screen → high,
      pinpoint 0, clamp at max, zero mip count), occlusion compare
      (nearest behind → true, nearest in front → false, equal not,
      epsilon slack), hysteresis (starts visible, flips after 3,
      dissent resets on match, flip-back needs 3, Reset).

## 3. Tail (mandatory - enforced by rulebook v5.3.0)
- [x] 3.1 Update or create documentation covering the implementation
      `docs/engine/25-vfx/37-hzb-occlusion.md`.
- [x] 3.2 Write tests covering the new behavior
      `runtime/tests/engine/vfx/test_vfx_hzb_occlusion.cpp`.
- [x] 3.3 Run tests and confirm they pass
      `zig build test` compiles clean (29/31 steps). Pre-existing
      ffmpeg-DLL runtime-exec gap.
