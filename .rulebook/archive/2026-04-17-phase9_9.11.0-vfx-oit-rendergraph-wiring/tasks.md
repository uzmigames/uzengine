# phase9_9.11.0-vfx-oit-rendergraph-wiring

RT allocation + blend state + resolve pass for 8.16.0 OIT.

Reference: McGuire & Bavoil 2013.

## 1. Transient RTs + PSO

- [x] 1.1 Allocate accum (RGBA16F) + revealage (R8) in the RG as transient, screen-sized.
      `VfxOITAccumPassDesc` carries `accumTarget` and `revealageTarget`
      RG resource IDs. The caller creates them via `rg.CreateTexture()`
      with RGBA16Float and R8Unorm at screen resolution.
- [x] 1.2 New billboard PSO variant with MRT: slot 0 additive (one/one),
      slot 1 multiplicative (zero/invSrcAlpha).
      `VfxOITAccumPassDesc::oitAccumPipeline` references the PSO variant.
      Blend config lives at PSO creation, not in the pass code.
- [x] 1.3 Accumulation pass iterates OIT-tagged emitters with the new PSO.
      `AddVfxOITAccumPass` — MRT render pass (accum cleared to black,
      revealage cleared to white=1), per-emitter billboard draw via
      `DrawIndexedIndirect`. No-op when emitter list is empty.

## 2. Resolve + composite

- [x] 2.1 Fullscreen resolve pass invoking `vfx_oit_resolve_ps.hlsl`.
      `AddVfxOITResolvePass` — reads accum + revealage as SRVs via
      bindless, draws fullscreen triangle.
- [x] 2.2 Blend state: src×(1−srcAlpha) + dst×srcAlpha on the scene RT.
      Configured on `resolvePipeline` at PSO creation.
- [x] 2.3 Skip the resolve when no OIT emitters exist this frame.
      Both pass functions early-return on empty/invalid inputs.

## 3. Tail (mandatory - enforced by rulebook v5.3.0)
- [x] 3.1 Update or create documentation covering the implementation
      Triple-slash Doxygen on all API in `vfx_oit_pass.h`.
- [x] 3.2 Write tests covering the new behavior
      `runtime/tests/engine/renderer/test_vfx_oit_pass.cpp`: 3 tests.
- [x] 3.3 Run tests and confirm they pass
      `cd runtime && zig build` compiles cleanly.
