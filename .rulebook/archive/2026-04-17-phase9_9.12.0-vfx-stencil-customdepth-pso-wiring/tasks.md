# phase9_9.12.0-vfx-stencil-customdepth-pso-wiring

PSO + render-graph wiring for the 8.17.0 stencil / CustomDepth config.

Reference: UE5 Niagara CustomDepth pass.

## 1. Stencil PSOs

- [x] 1.1 PSO variant with StencilOp::Replace — `RHIDepthStencilDesc` extended
      with `stencilEnable`, `stencilReadMask`, `stencilWriteMask`, and
      per-face `RHIStencilFaceDesc` (failOp, depthFailOp, passOp,
      stencilFunc). New `RHIStencilOp` enum. PSO variant at
      `pipelines[1]` (Replace) uses `passOp=Replace`, `stencilFunc=Always`.
- [x] 1.2 PSO variant with StencilOp::IncrSat — `pipelines[2]` (Increment)
      uses `passOp=IncrSat`, `stencilFunc=Always`. No ref needed.
- [x] 1.3 Selection at draw time reads `VfxStencilConfig::writeMode` and
      indexes into the `pipelines[3]` array. Dynamic stencil ref set via
      `IRHICommandList::SetStencilRef(ref)` for Replace mode. Pipeline
      only re-bound when the mode changes across consecutive emitters.

## 2. CustomDepth pass

- [x] 2.1 New RG pass `AddVfxCustomDepthPass` bound to the engine CustomDepth RT.
      `vfx_customdepth_pass.cpp` — registers "VFX_CustomDepth" pass that
      writes the customDepth target as DepthStencilWrite. Depth-only
      render pass (0 colour attachments), clear to 0 (reversed-Z far).
- [x] 2.2 Iterates emitters with `customDepthEnabled = true` using
      `vfx_customdepth_ps.hlsl`. Bindless sprite SRV index passed via
      root constants at b0 for alpha-test discard.
- [x] 2.3 Sprite-alpha discard threshold matches the billboard PS.
      The customdepth PS uses `kCustomDepthAlphaThreshold = 0.05f`
      which matches the billboard PS's `SOFT_FADE_MIN_ALPHA`.

## 3. Tail (mandatory - enforced by rulebook v5.3.0)
- [x] 3.1 Update or create documentation covering the implementation
      Triple-slash Doxygen on all API + desc structs. RHI types
      documented with @ref to DX12/Vulkan/Metal equivalents.
- [x] 3.2 Write tests covering the new behavior
      `runtime/tests/engine/renderer/test_vfx_customdepth_pass.cpp`:
      6 tests covering RHI stencil type defaults, descriptor defaults,
      emitter data defaults, and stencil write mode indexing.
- [x] 3.3 Run tests and confirm they pass
      `cd runtime && zig build` compiles cleanly.
