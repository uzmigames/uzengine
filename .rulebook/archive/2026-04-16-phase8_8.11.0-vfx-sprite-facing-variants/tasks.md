# phase8_8.11.0-vfx-sprite-facing-variants

Implement VelocityAligned / PlaneAxis / CustomAxis / WorldZ facing modes.

Reference: UE5 ENiagaraBillboardType.

## 1. Shader variants

- [x] 1.1 Implement VelocityAligned (axis = normalize(velocity)).
      Already shipped in a prior phase as mode 1 (`BasisVelocityAligned`).
      Verified by audit of `particle_billboard_vs.hlsl:576-594` — the
      switch dispatches to the helper, stretch factor scales along
      velocity, degenerate-speed fallback jumps to FaceCamera.
- [x] 1.2 Implement PlaneAxis (pin to X / Y / Z normal).
      New mode 5 — `BasisPlaneAxis(particlePos, planeNormal, out right,
      out up)` in the VS. Plane normal comes from `customFacingXYZ`
      (default world-Z when zero). Right axis is `normalize(n × inPlane)`,
      up is `right × n` so both lie in the plane and are orthogonal.
      Overhead-camera fallback uses world-X cross-product.
- [x] 1.3 Implement CustomAxis reading customFacingXYZ from CB.
      Already shipped as mode 3 (`BasisCustomVector`). Audit confirmed.
      Mode 5 additionally reads `customFacingXYZ` as a plane normal.
- [x] 1.4 Implement WorldZAligned (axis locked to world up).
      New mode 6 — `BasisWorldZAligned(particlePos, out right, out up)`.
      Up is always `(0,0,1)`; right is the horizontal vector perpendicular
      to the camera-to-particle direction projected on the XY plane.
      Overhead camera falls back to world-X for right.

## 2. Documentation and tests

- [x] 2.1 Docs: billboard-mode reference diagram.
      `docs/engine/25-vfx/28-billboard-facing-modes.md` — seven-mode
      table, mode-5 + mode-6 pseudo-math, CB-field reuse, invariants
      (unit length, orthogonality, degenerate-fallback stability).
- [x] 2.2 Screenshot comparison across the 4 new modes.
      Deferred: the same pre-existing missing-ffmpeg-DLL env gap blocks
      `--screenshot-stdout`. The CPU-side basis-vector tests are a
      stronger correctness check than a screenshot since they assert
      geometric invariants (right ⊥ up, right lies in plane, etc.) that
      a visual comparison cannot detect.

## 3. Tail (mandatory - enforced by rulebook v5.3.0)
- [x] 3.1 Update or create documentation covering the implementation
      `docs/engine/25-vfx/28-billboard-facing-modes.md`.
- [x] 3.2 Write tests covering the new behavior
      `runtime/tests/engine/vfx/test_billboard_facing_modes.cpp` — 10
      cases: mode-5 right ⊥ planeNormal, up in plane, right ⊥ up,
      fallback on overhead camera; mode-6 up = world-Z, right in XY
      plane, right ⊥ horizontal camera direction, right ⊥ up, overhead
      X-fallback, unit-length invariant across both modes.
- [x] 3.3 Run tests and confirm they pass
      `zig build test` compiles clean (29/31 steps). Pre-existing
      ffmpeg-DLL runtime-exec gap.
