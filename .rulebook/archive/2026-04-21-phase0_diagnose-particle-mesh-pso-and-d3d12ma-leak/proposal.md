# Proposal: phase0_diagnose-particle-mesh-pso-and-d3d12ma-leak

## Why

The parent task `phase0_fix-vfx-demo-launch-pipeline` resolved four of
the five known pre-existing demo-launch gaps:

1. ✅ RT test shader (`rt_uv_test_lib.hlsl`) — `[raypayload]`
   attribute + field qualifiers added; shader compiles cleanly.
2. ✅ `d3dcompiler_47.dll` — `vfx_viewport_app/build.zig` now sets
   `deploy_fxc = true` so the DLL lands in the demo's `bin/`.
3. ✅ FFmpeg DLL deployment — `runtime/build.zig` and
   `demos/common/build_common.zig` now copy every ffmpeg DLL from
   `thirdparty/ffmpeg/bin/` to the output `bin/`, tolerant of
   missing files.
4. ✅ `setup.ps1` self-healing — an incomplete ffmpeg install
   (bin/ missing but ffmpeg/ dir exists) now force-re-downloads the
   LGPL shared build.
5. ⚠️ ParticleMesh PSO creation — input layout for
   `POSITION`/`NORMAL`/`TEXCOORD0` was added to both the opaque and
   transparent PSO descs + the bindless root signature was patched
   to set `D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT`
   so PSOs with an IA layout can use the bindless root sig. After
   both fixes `CreateGraphicsPipelineState` still returns
   `hr=0x80070057` (E_INVALIDARG) with the same shader blob sizes
   (VS=6192, PS=5428). Further diagnosis stalled at the
   fail-twice-escalate threshold without meaningful DX12 debug
   layer output.
6. ⚠️ `D3D12MA` shutdown leak — un-touched.

This follow-up task closes the residual two gaps. Both require
interactive instrumentation (DX12 InfoQueue callback + D3D12MA
named-allocation tracking) that is out of scope for the build /
deployment pipeline parent task.

## What Changes

1. **Wire up the DX12 InfoQueue message callback** so the debug
   layer's per-error text is captured in the runtime log next to the
   generic `hr=0x80070057` print. Without this the `E_INVALIDARG`
   return is opaque; WITH this the callback typically prints
   something like "Pixel Shader expects resource X but PSO
   declares Y", "Vertex shader input signature mismatch", etc.
2. **Diagnose + fix the actual ParticleMesh PSO invariant** once
   the debug layer points to it. Candidate suspects:
   - Pixel shader `PS=5428` bytes bytecode — sig mismatch with the
     VS output signature (it expects `TEXCOORD0/1/2/3` from the
     VS but might only receive some).
   - DXIL-level root-signature conflict (if DXC compiled either
     shader with an embedded `#define ROOT_SIGNATURE ...` that
     contradicts the engine's bindless root sig).
   - Vertex stride set to 32 in the PSO vs something the VS expects
     per-vertex attributes that don't add up to 32.
   - `numRT=1` + RTV format `RGBA16Float` disagreeing with the
     PS's return-type declaration.
3. **D3D12MA shutdown leak** — enable named allocation tracking in
   the D3D12MA wrapper, repro the assertion, identify the leaking
   subsystem, fix the missing `Release()` / `Free()` path. Typical
   suspects: render-graph transient resources, ImGui or debug-UI
   textures, or the shader cache's PSO library.
4. **Verify** with `vfx_viewport_app.exe --headless --frames 60`
   that the process exits cleanly (exit code 0, no assertion).

## Impact

- Affected specs: inherits from the parent spec
  (`phase0_fix-vfx-demo-launch-pipeline/specs/vfx-demo-launch-pipeline/spec.md`).
- Affected code:
  - `runtime/engine/rhi/dx12/` — InfoQueue callback wiring.
  - `runtime/engine/renderer/core/pipeline/pipeline_vfx.cpp`
    or `runtime/shaders/runtime/vfx/particle_mesh_*.hlsl` — whichever
    the debug layer points to.
  - `runtime/thirdparty/d3d12ma/src/D3D12MemAlloc.cpp` (likely just
    wrapping calls around it — not modifying the library directly).
  - Whichever subsystem owns the leaked allocation.
- Breaking change: NO — these are bug fixes.
- User benefit: the Niagara editor can finally launch + run for more
  than a few frames without an assert trip.
