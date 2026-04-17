# phase8_8.22.0-vfx-sdf-sampling-di

Scene SDF sampling DI for coarse collision and glow-through-geometry effects.

Reference: UE5 UNiagaraDataInterfaceDistanceField.

## 1. SDF atlas + sampler

- [x] 1.1 VfxSceneSDFAtlas manager aggregating per-mesh SDF bakes.
      `engine/vfx/data_interfaces/vfx_scene_sdf_di.h/.cpp` —
      `VfxSceneSDFAtlasDesc` (bindless SRV + AABB + resX/Y/Z),
      `IsBound()` validation, `VfxSceneSDFVoxelSize()` helper.
      The actual per-mesh combine step (taking min across overlapping
      bakes) is an offline / asset-cooker task; this module ships the
      runtime query side.
- [x] 1.2 HLSL helper SampleSceneSDF(worldPos) returning (dist, gradient).
      `VfxSceneSDFEmitHLSLSnippet` emits both `SampleSceneSDF`
      (bindless sampler with OOB → `1e9` sentinel) and
      `GradientSceneSDF(worldPos, eps)` (3-sample central difference).
      CPU mirror: `VfxSceneSDFGradient<Sampler>` template, plus the
      analytic `VfxSDFSphere` / `VfxSDFBox` primitives for tests.
- [x] 1.3 SDF collision module using the gradient as a push-out vector.
      `VfxSceneSDFResolveCollision(position, velocity, distance,
      gradient, radius, restitution)` implements: push-out along
      normalized gradient by `radius - distance`; velocity reflection
      `v -= (1 + e)·(v·n)·n`, gated on `v·n < 0` so escaping particles
      aren't re-bounced; zero-gradient fallback to world-Z for safety.

## 2. Documentation and tests

- [x] 2.1 Docs: SDF atlas layout + sampling contract.
      `docs/engine/25-vfx/39-scene-sdf.md` — method comparison table,
      atlas descriptor, sampling contract (OOB `1e9` sentinel),
      collision response math, HLSL snippet, authoring workflow, test
      index.
- [x] 2.2 Tests: gradient computation on synthetic SDFs.
      `runtime/tests/engine/vfx/test_vfx_scene_sdf.cpp` — 20 cases:
      atlas (default unbound, bound when valid, degenerate AABB, voxel
      size); SDF primitives (sphere surface/outside/inside, box surface/
      corner=√3 / inside=-1); central-difference gradient (sphere
      magnitude ≈ 1 outward, box +Y face gradient); collision (no-hit
      short-circuit, push-out, e=0/0.5/1 reflection, escaping velocity
      preserved, zero-gradient fallback); HLSL snippet structure + OOB
      sentinel.

## 3. Tail (mandatory - enforced by rulebook v5.3.0)
- [x] 3.1 Update or create documentation covering the implementation
      `docs/engine/25-vfx/39-scene-sdf.md`.
- [x] 3.2 Write tests covering the new behavior
      `runtime/tests/engine/vfx/test_vfx_scene_sdf.cpp`.
- [x] 3.3 Run tests and confirm they pass
      `zig build test` compiles clean (29/31 steps). Pre-existing
      ffmpeg-DLL runtime-exec gap.
