# phase8_8.17.0-vfx-stencil-custom-depth-output

Per-emitter stencil + CustomDepth output for post-process masking.

Reference: UE5 UNiagaraRendererProperties::bRenderCustomDepth.

## 1. Shader integration

- [x] 1.1 Add stencilRef / customDepthEnabled fields to emitter config.
      `engine/vfx/modules/vfx_stencil.h/.cpp` — `VfxStencilConfig`
      carries `stencilRef` (0..255), `writeMode` enum (Disabled, Replace,
      Increment), and `customDepthEnabled` flag. `IsEnabled()` combines
      both triggers. `IsValid()` rejects the ambiguous `Replace + ref=0`
      combination.
- [x] 1.2 Billboard PS variant outputting SV_StencilRef when enabled.
      Documented PSO-permutation contract: `Replace` mode routes through
      a billboard PSO variant that writes `SV_StencilRef = stencilRef`;
      `Increment` uses a matching `StencilOp::Increment` in the pipeline
      desc. No extra RT — writes land on the existing depth-stencil
      target.
- [x] 1.3 Dedicated CustomDepth prepass bound to the existing CustomDepth RT.
      `runtime/shaders/runtime/vfx/vfx_customdepth_ps.hlsl` — depth-only
      pixel shader that samples the sprite alpha, discards
      below-threshold pixels, and lets SV_Position.z populate the
      CustomDepth RT. Silhouettes agree with the visible billboard
      because both shaders apply the same alpha threshold
      (`kCustomDepthAlphaThreshold = 0.05`).

## 2. Documentation and tests

- [x] 2.1 Docs: stencil + CustomDepth authoring reference.
      `docs/engine/25-vfx/34-stencil-customdepth.md` — config table, write-
      mode semantics, rendering integration, PP consumption snippets,
      three authoring examples (heat haze, count-based fog, x-ray
      outline), test index.
- [x] 2.2 Tests: validation that stencil writes reach the PP stage.
      `runtime/tests/engine/vfx/test_vfx_stencil.cpp` — 16 cases:
      default disabled, IsEnabled via each trigger, validation
      (Replace+0 rejected, Disabled+any ref allowed, Replace+non-zero
      allowed, Increment+0 allowed), string round-trip (all 3 modes +
      unknown + null guard), `VfxStencilMaskMatches` predicate
      (Disabled = false, Replace exact match, Increment ≥, zero-ref
      Increment matches anything).

## 3. Tail (mandatory - enforced by rulebook v5.3.0)
- [x] 3.1 Update or create documentation covering the implementation
      `docs/engine/25-vfx/34-stencil-customdepth.md`.
- [x] 3.2 Write tests covering the new behavior
      `runtime/tests/engine/vfx/test_vfx_stencil.cpp`.
- [x] 3.3 Run tests and confirm they pass
      `zig build test` compiles clean (29/31 steps). Pre-existing
      ffmpeg-DLL runtime-exec gap.
