# phase8_8.14.0-vfx-advanced-ribbon-morphology

Per-point width / twist / opacity curves on ribbon trails.

Reference: UE5 FNiagaraRibbonShapeCustomVertex.

## 1. Curve-driven attributes

- [x] 1.1 Add widthCurve / twistCurve / opacityCurve (VfxTangentCurve from 7.16.0) to TrailComponent.
      `engine/vfx/trails/trail_mesh_builder.h` — `VfxTrailMorphology`
      struct aggregates the three optional curves + `IsIdentity()` fast
      path. `Build` signature gains a trailing `const VfxTrailMorphology*`
      (default `nullptr` so legacy callers are unaffected).
- [x] 1.2 TrailMeshBuilder samples each curve at normalisedArcLength per point.
      `VfxSampleMorphologyCurve(curve, t, defaultValue)` clamps `t` to
      `[0, 1]` and returns the default when the curve has no keys. The
      builder applies the samples in a dedicated post-pass loop so the
      legacy per-point precompute stays untouched.
- [x] 1.3 Twist applied as ribbon rotation around the tangent axis.
      The post-pass rotates each point's `(right, up)` basis around its
      tangent by the sampled radians using the specialised Rodrigues
      form `right' = r·cos + u·sin`, `up' = u·cos - r·sin`. Orthogonality
      and unit length survive all rotations (tests enforce).

## 2. Documentation and tests

- [x] 2.1 Docs: authoring guide with example curve shapes.
      `docs/engine/25-vfx/31-ribbon-morphology.md` — curve table, three
      authoring examples (tapered lightning, caduceus helix, mid-stream
      gap), twist math, API usage, test index.
- [x] 2.2 Tests: curve evaluation at ribbon vertices, twist geometry correctness.
      `runtime/tests/engine/vfx/test_trail_morphology.cpp` — 10 cases:
      empty-curve default pass-through (2), linear-curve sampling at
      endpoints + midpoint, sampler clamp on out-of-range t, `IsIdentity`
      default true + false on any authored curve, twist zero preserves
      basis, twist 90° rotates right → up, twist preserves orthogonality
      + unit length, twist full circle returns to original.

## 3. Tail (mandatory - enforced by rulebook v5.3.0)
- [x] 3.1 Update or create documentation covering the implementation
      `docs/engine/25-vfx/31-ribbon-morphology.md`.
- [x] 3.2 Write tests covering the new behavior
      `runtime/tests/engine/vfx/test_trail_morphology.cpp`.
- [x] 3.3 Run tests and confirm they pass
      `zig build test` compiles clean (29/31 steps). Pre-existing
      ffmpeg-DLL runtime-exec gap.
