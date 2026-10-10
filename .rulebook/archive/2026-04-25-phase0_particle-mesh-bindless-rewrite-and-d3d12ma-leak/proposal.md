# Proposal: phase0_particle-mesh-bindless-rewrite-and-d3d12ma-leak

## Why

`phase0_diagnose-particle-mesh-pso-and-d3d12ma-leak` diagnosed the
ParticleMesh PSO failure and shipped the InfoQueue pump that surfaced
the real invariant:

> `"Root Signature doesn't match Vertex Shader: Shader SRV descriptor`
> `range (BaseShaderRegister=0, NumDescriptors=1, RegisterSpace=0)`
> `is not fully bound in root signature"`

`particle_mesh_vs.hlsl` binds `StructuredBuffer<float4x4>` at `t0`, but
the SM6.6 bindless root signature contains no SRV table at t0 — it
relies on `ResourceDescriptorHeap[]` access instead. The shader was
authored pre-bindless and never updated.

The fix is architectural: rewrite the mesh VS + PS + matching
compute instance shader with `#ifdef UZ_BINDLESS` branches that read
`g_instanceTransforms` via the bindless heap, plus thread a
descriptor-heap index through the CPU-side draw-call setup so the VS
knows which slot to sample.

In parallel, the D3D12MA shutdown assertion
(`m_pMetadata->IsEmpty()`) is un-diagnosed. A leaked GPU allocation
trips an assert at process exit. Need named-allocation tracking +
owner-subsystem identification + missing-`Release()` fix.

Neither item blocks visual verification — the Niagara editor renders
end-to-end before the assert fires (see `.screenshots/niagara_mode.png`).
But both are real defects and must land before phase-12 can be called
production-ready.

## What Changes

### A — ParticleMesh bindless rewrite

1. `particle_mesh_vs.hlsl` — gain `#ifdef UZ_BINDLESS` branch that
   replaces `StructuredBuffer<float4x4> g_instanceTransforms : register(t0)`
   with a cbuffer-sourced descriptor index + `ResourceDescriptorHeap[idx]`
   load. Mirrors the pattern in `particle_billboard_vs.hlsl`.
2. `particle_mesh_ps.hlsl` — same conditional pattern for any SRV it
   references.
3. `particle_mesh_instance_cs.hlsl` — already writes the instance
   transform buffer; no bindless change needed unless it reads SRVs.
4. CPU draw path (where `GetParticleMeshOpaque/Transparent` PSO is
   bound) — push the descriptor-heap index of the instance-transforms
   SRV as a root 32-bit constant at b0 so the VS can sample it.
5. Verify both PSOs succeed at demo startup (no InfoQueue error).

### B — D3D12MA leak diagnosis

1. Enable named-allocation logging in the D3D12MA wrapper
   (`engine/rhi/dx12/dx12_d3d12ma.cpp` or equivalent) — emit a log
   line per live allocation on shutdown before the assert fires.
2. Repro via `vfx_viewport_app.exe --headless --frames 60`; capture
   the leaked-allocation name from the log.
3. Trace the owner subsystem; add the missing `Release()` / `Free()`
   to the matching shutdown path.
4. Verify clean exit with no assertion.

## Impact

- Affected specs: inherits from
  `phase0_diagnose-particle-mesh-pso-and-d3d12ma-leak/specs/particle-mesh-pso/spec.md`.
- Affected code:
  - `runtime/shaders/runtime/vfx/particle_mesh_{vs,ps}.hlsl`
  - `runtime/engine/renderer/core/pipeline/pipeline_vfx.cpp` (root-
    constant push at draw time, if needed)
  - `runtime/engine/vfx/mesh/*` — CPU-side descriptor index wiring
  - `runtime/engine/rhi/dx12/dx12_d3d12ma.cpp` — named tracking
  - Whichever subsystem owns the leaking allocation (TBD)
- Breaking change: NO — both items are bug fixes.
- User benefit: Niagara editor PROD-ready, mesh-particle path
  functional, clean process exit.
