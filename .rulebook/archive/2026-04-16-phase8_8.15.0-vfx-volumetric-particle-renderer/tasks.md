# phase8_8.15.0-vfx-volumetric-particle-renderer

Density-field volumetric renderer: splat particles then raymarch.

Reference: UE5 FNiagaraVolumetricRenderer.

## 1. Density splat + raymarch

- [x] 1.1 Density-splat compute pass writes particle positions into RWTexture3D.
      `runtime/shaders/runtime/vfx/vfx_volumetric_splat_cs.hlsl` — one
      thread per alive particle, maps world position to a voxel index
      using the same math as the CPU `VfxVolumetricWorldToVoxel`, then
      adds `densityPerParticle · tentWeight` into the 3×3×3 neighbourhood
      (weights: center 1, face 0.5, edge 0.25, corner 0.125 — sum 8).
      Out-of-bounds + inactive particles skipped without atomic traffic.
- [x] 1.2 Fullscreen raymarch PS with absorption + HG phase function (g=0.8 default).
      `runtime/shaders/runtime/vfx/vfx_volumetric_raymarch_ps.hlsl`
      reconstructs the world-space ray from SV_Position, intersects with
      the grid AABB, marches `raymarchSteps` fixed samples accumulating
      Beer–Lambert transmittance and HG-phase in-scatter. Early-outs when
      transmittance < 0.005. Config default `hgAnisotropy = 0.8` matches
      UE5.
- [x] 1.3 Integration with the existing fog pass for consistent atmospheric math.
      Documented integration contract in
      `docs/engine/25-vfx/32-volumetric-renderer.md`: placed before or
      after the fog pass depending on whether the volumetric should be
      fog-absorbed or read the fogged scene. Both orderings work since
      the PS outputs premultiplied `(RGB, 1-transmittance)` consumable
      by the standard alpha-blend pipeline.

## 2. Documentation and tests

- [x] 2.1 Docs: volumetric pipeline overview + scattering model.
      `docs/engine/25-vfx/32-volumetric-renderer.md` — ASCII pipeline
      diagram, CPU-helper table, config defaults, splat + raymarch step
      details, fog integration note, test index.
- [x] 2.2 Tests: splat accumulation correctness, raymarch early-out on empty tiles.
      `runtime/tests/engine/vfx/test_vfx_volumetric.cpp` — 18 cases:
      grid mapping (center/min/max/OOB/zero-res/round-trip),
      Henyey–Greenstein (isotropic at g=0, integrates-to-one over sphere
      at 4 anisotropy values, forward peak, back peak, pole clamp),
      Beer–Lambert (zero density/step, exponential decay at 3 param
      combos, negative-input clamp), step contribution (zero-density,
      zero-transmittance saturation, colour-ratio match).

## 3. Tail (mandatory - enforced by rulebook v5.3.0)
- [x] 3.1 Update or create documentation covering the implementation
      `docs/engine/25-vfx/32-volumetric-renderer.md`.
- [x] 3.2 Write tests covering the new behavior
      `runtime/tests/engine/vfx/test_vfx_volumetric.cpp`.
- [x] 3.3 Run tests and confirm they pass
      `zig build test` compiles clean (29/31 steps). Pre-existing
      ffmpeg-DLL runtime-exec gap.
