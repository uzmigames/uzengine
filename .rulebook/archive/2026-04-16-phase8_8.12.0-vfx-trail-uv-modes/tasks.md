# phase8_8.12.0-vfx-trail-uv-modes

Stretch / Tiled / PerRibbon UV modes on the trail / ribbon mesh builder.

Reference: UE5 ENiagaraRibbonUVMode.

## 1. UV authoring

- [x] 1.1 VfxTrailUVMode enum (Stretch / Tiled / PerRibbon) + config field on TrailComponent.
      `engine/vfx/trails/trail_mesh_builder.h` — `VfxTrailUVMode` enum
      plus string round-trip helpers (`VfxTrailUVModeToString`,
      `VfxTrailUVModeFromString`). `TrailAsset` gains `uvMode` +
      `tilesPerRibbon` fields (defaults Stretch / 1.0 so legacy assets
      are byte-for-byte unchanged).
- [x] 1.2 TrailMeshBuilder emits V per selected mode with a tilesPerSegment parameter for Tiled.
      `Build` signature extended with `uvMode` + `tilesPerRibbon`
      trailing defaults. Pure-function `VfxComputeTrailV(mode, t, s, N, k)`
      computes the V coordinate so tests verify the math directly.
      Stretch: `V = t`. Tiled: `V = t * k` (zero-clamped). PerRibbon:
      `V = (s + t) / N` (single-strand degenerates to Stretch).
- [x] 1.3 JSON round-trip uv_mode field on .uztrail assets.
      `engine/vfx/trails/trail_asset.cpp` reads optional `"uv_mode"`
      (stretch / tiled / per_ribbon) + `"tiles_per_ribbon"` (> 0).
      Unknown strings and non-positive tile counts reject the load with
      a `VFX/Error`.

## 2. Documentation and tests

- [x] 2.1 Docs: UV mode reference diagram.
      `docs/engine/25-vfx/29-trail-uv-modes.md` — per-mode V formula,
      JSON schema, tiled sampler-addressing requirement, PerRibbon
      atlas-band semantics, API usage.
- [x] 2.2 Tests: per-mode V output at known arc lengths.
      `runtime/tests/engine/vfx/test_trail_uv_modes.cpp` — 18 cases:
      string round-trip (4), Stretch endpoints + midpoint + strand-
      index independence (3), Tiled multiplication + zero-clamp +
      fractional tiles (3), PerRibbon single-strand degeneracy + 2-
      strand halves + 4-strand quarter bands (3), JSON loader default
      is Stretch, tiled + tile count, per_ribbon, unknown rejected,
      negative tiles rejected (5).

## 3. Tail (mandatory - enforced by rulebook v5.3.0)
- [x] 3.1 Update or create documentation covering the implementation
      `docs/engine/25-vfx/29-trail-uv-modes.md`.
- [x] 3.2 Write tests covering the new behavior
      `runtime/tests/engine/vfx/test_trail_uv_modes.cpp`.
- [x] 3.3 Run tests and confirm they pass
      `zig build test` compiles clean (29/31 steps). Pre-existing
      ffmpeg-DLL runtime-exec gap.
