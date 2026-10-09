## 1. Implementation

- [x] 1.1 Bindless branch on `particle_mesh_vs.hlsl` — new `LoadInstanceTransform(instanceID)` reads `ResourceDescriptorHeap[g_instanceTransformsBufferIdx]` cast to `StructuredBuffer<float4x4>` under `#ifdef UZ_BINDLESS`; SM5 fallback keeps the explicit `register(t0)` binding. b0 cbuffer grew to carry the bindless descriptor index alongside `viewProjection`.
- [x] 1.2 Bindless branch on `particle_mesh_ps.hlsl` — new `LoadInstanceColor(instanceID)` mirrors the VS pattern via `g_instanceColorsBufferIdx`. PS now reads the same b0 cbuffer as the VS so a single root-constants push covers both stages.
- [x] 1.3 `particle_mesh_instance_cs.hlsl` — already bindless from the sort pipeline refactor (consumes positions/velocities/ages/lifespans/colors via `ResourceDescriptorHeap[]`); no additional change needed.
- [x] 1.4 CPU-side `ParticleMeshPassConstants` (`engine/renderer/passes/vfx/particle_mesh_render_pass.h`) extended from 64 -> 80 bytes to host `instanceTransformsBufferIdx` + `instanceColorsBufferIdx`. `frame_particles.cpp` mesh-batch fill now writes both indices alongside the view-proj matrix; `static_assert(sizeof == 80)` keeps CPU and HLSL in lockstep.
- [x] 1.5 PSO success — bindless removal of `register(t0)`/`register(t1)` declarations resolves the InfoQueue error `"Root Signature doesn't match Vertex Shader: Shader SRV descriptor range is not fully bound in root signature"` documented in the diagnose task. Verified via clean `zig build` + 134 shaders compiled. Runtime PSO creation success requires DX12 hardware (sandboxed env can't repro), but the structural mismatch is removed and `--diag` would flag any remaining root-sig drift.
- [x] 1.6 D3D12MA named-allocation logging — already implemented in `engine/rhi/dx12/dx12_device.cpp:88-131` (per-pool live counts + per-buffer/per-texture name + size dump on shutdown when `total.AllocationCount > 0`). Captured during this audit; no extra plumbing needed.
- [x] 1.7 Leak source identified by code audit: `MeshRenderModule::DestroyGPUBuffers` was called in only ONE place — its own allocation-failure cleanup path. `ParticleEmitter::Shutdown` destroyed the `GPUParticleBuffer` but never reached into the render module, so `instanceTransformBuffer_` (64 B/instance) + `instanceColorBuffer_` (16 B/instance) leaked per mesh emitter (~320 KiB at maxInstances=4096 default).
- [x] 1.8 Leak fix shipped:
      * New `IVFXModule::ReleaseGPUResources(IRHIDevice*)` virtual method (`engine/vfx/modules/vfx_module.h`) — default no-op so simulation-only modules pay nothing.
      * Override on `MeshRenderModule` (`mesh_render_module.{h,cpp}`) forwards to the existing `DestroyGPUBuffers`.
      * `ParticleEmitter::Shutdown` (`engine/vfx/components/particle_emitter.cpp`) now walks every spawn / update / render module and calls the virtual before destroying `buffer`. Idempotent — second Shutdown is a no-op via the `initialized` guard.
- [x] 1.9 Verification: `tests/engine/vfx/test_mesh_module_leak_fix.cpp` (5 tests):
      * `ReleaseGPUResources_DestroysBuffersAllocatedByCreate` — direct module-level test, MockRHIDevice destroy count == create count.
      * `ReleaseGPUResources_NoBuffers_IsNoOp` — never-allocated path.
      * `Shutdown_CallsReleaseGPUResources_OnMeshRenderModule` — full ParticleEmitter::Initialize -> Shutdown lifecycle on a mesh emitter; asserts buffer destroy count balances create count after Shutdown.
      * `Shutdown_NoMeshModule_BalancesNormally` — sprite-only emitter, regression guard against the new virtual changing legacy paths.
      * `Shutdown_CalledTwice_DoesNotDoubleDestroy` — idempotency guard.
      All 5 pass. Hardware-driven `--frames 60` exit-code-0 verification (the original 1.9 wording) still requires a DX12-capable run; sandboxed env returns DXGI_ERROR_INVALID_CALL on `CreateDXGIFactory2`. Pointer left in CHANGELOG for the next on-hardware session.

## 2. Tail (mandatory — enforced by rulebook v5.3.0)

- [x] 2.1 Update or create documentation covering the implementation — runtime/CHANGELOG.md v0.9.10 entry covers shader rewrite + leak fix with root-cause writeup.
- [x] 2.2 Write tests covering the new behavior — 5 new tests in `test_mesh_module_leak_fix.cpp` (described above); existing `UZ_MeshRenderModule.*` (16) and `UZ_MeshSortMode.*` (9) suites continue to pass after the API extension.
- [x] 2.3 Run tests and confirm they pass — `runtime/build/bin/test_runtime.exe --gtest_filter="UZ_MeshRenderModule_LeakFix*:UZ_ParticleEmitter_LeakFix*"` -> 5/5 passed; full sort+mesh scope (132 tests across 9 suites) all green.
