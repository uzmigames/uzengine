## 1. Sort core — alive-detection + custom-attribute keys
- [x] 1.1 Extend `VFXSortConstants` (HLSL + CPU mirror) with `agesSrvIndex`, `lifespansSrvIndex`, `customSortKeySrvIndex`, `sortMode` — total 96 bytes
- [x] 1.2 Add `static_assert(sizeof(VFXSortConstants) == 96)` on the CPU side
- [x] 1.3 `GenerateSortKey` reads `agesBuf[idx]` + `lifespansBuf[idx]`; assigns `SORT_KEY_DEAD` when `age >= lifespan`
- [x] 1.4 Branch `GenerateSortKey` on `sortMode`: ViewDepth (current), CustomAttribute (reads `customSortKeyBuf[idx]`)
- [x] 1.5 `LocalSortPhase` dispatches over `paddedCount = NextPow2(capacity)` (not aliveCount); CB `keyGenCount` becomes `capacity`
- [x] 1.6 `gpu_particle_sort.cpp` `DispatchSort` accepts a `DispatchParams` struct exposing all SRV indices + sort mode

## 2. Inline dispatch + sprite re-enable
- [x] 2.1 New `SceneRenderer::DispatchVFXSort` runs inline on the main command list (no render graph dependency)
- [x] 2.2 Called immediately after `DispatchVFXSimulation` in `scene_render_frame.cpp` — sortedIndices populated before mesh CS dispatch
- [x] 2.3 Auto-enabled for sprite emitters with `blendMode = AlphaBlend` (mode 0)
- [x] 2.4 `particle_billboard_vs.hlsl` translates `instanceID -> particleID` via `sortedIndices` when SRV is valid (identity fallback)
- [x] 2.5 Mesh instance CB `sortedIndicesSrvIndex` no longer gated to UINT32_MAX — picks up the dispatch output for every mesh emitter with `sortMode != None`

## 3. Mesh CS slot allocation + CB mismatch fix
- [x] 3.1 Mesh CS sorted path: writes `transform[sortedSlot]` deterministically, no `InterlockedAdd`
- [x] 3.2 Thread 0 in sorted path writes `aliveCount` directly to indirect args instance count + mirrors in `instanceCount[0]`
- [x] 3.3 Added `colorsSrvIndex` + `instanceColorsUavIndex` to CPU `VFXMeshInstanceConstants` (matches 80-byte HLSL block)
- [x] 3.4 Wire `colorsSrvIndex` from `gpuBuf.WriteSet().colors`; `instanceColorsUavIndex` from `MeshRenderModule::GetInstanceColorBuffer()`
- [x] 3.5 `static_assert(sizeof(VFXMeshInstanceConstants) == 80)` on CPU side

## 4. Custom-attribute SRV resolution
- [x] 4.1 `ParticleEmitter::FindUserAttribute(name)` already provides the resolver (existing API since the user-custom-attributes task)
- [x] 4.2 Mesh emitter dispatch resolves `customSortAttribute` -> `userFloat[slotIndex]` SRV index, plumbs into the sort CB
- [x] 4.3 If `sortMode == CustomAttribute` but lookup fails (missing attribute, wrong type, no GPU buffer), log a one-time warning per emitter (`customSortWarned` flag) and downgrade to `None`

## 5. Demo + perf validation
- [x] 5.1 New sample assets shipped in BOTH schemas:
      * `sample/content/vfx/tests/mesh_sort_shards.uzfx` — editor schema (`stages` array). Loaded via DeserializeEmitter + new `ApplyEmitterStackToModuleStack`. Tested by `test_mesh_sort_uzfx_pipeline.cpp`.
      * `sample/content/vfx/tests/mesh_sort_shards.uzvfx` — production schema (`emitters[].modules.{spawn,update,render}`). Loaded via UzfxLoader::Instantiate. Tested by 4 new tests in `test_uzfx_loader.cpp`.
      Required (a) `IVFXModule::ApplyJsonConfig` override on `MeshRenderModule` (`engine/vfx/modules/mesh_render_module.{h,cpp}`); (b) new `ApplyEmitterStackToModuleStack` helper (`engine/vfx/components/apply_module_stack.h`); (c) extending `UzfxLoader::InstantiateEmitter` mesh-render branch to read `rotation_mode`, `sort_mode`, `custom_sort_attribute`, `max_instances` (`engine/asset/formats/uzfx_loader.cpp`). Production scene_vfx_sync path now picks up sort_mode automatically when authors add the field to existing `.uzvfx` mesh emitters.
- [x] 5.2 -> moved to follow-up `phase16_vfx-sort-pipeline-hardware-validation` section 1.2 (requires DX12 hardware run for screenshot capture)
- [x] 5.3 -> moved to follow-up `phase16_vfx-sort-pipeline-hardware-validation` section 1.3-1.4 (GPU timestamp perf budget)
- [x] 5.4 -> moved to follow-up `phase16_vfx-sort-pipeline-hardware-validation` section 2 (CVar fallback depends on having perf data first)

## 6. Tests
- [x] 6.1 -> moved to follow-up `phase16_vfx-sort-pipeline-hardware-validation` section 4.1 (needs MockComputeDispatch infra)
- [x] 6.2 -> moved to follow-up `phase16_vfx-sort-pipeline-hardware-validation` section 4.2
- [x] 6.3 CB size assertions — `static_assert(sizeof(VFXSortConstants) == 96)` in `gpu_particle_sort.h`; `static_assert(sizeof(VFXMeshInstanceConstants) == 80)` in `frame_particles.cpp`; runtime offset checks in `test_gpu_particle_sort.cpp` (added rows for agesSrvIndex@80, lifespansSrvIndex@84, customSortKeySrvIndex@88, sortMode@12 + AllFields_Roundtrip extended)
- [x] 6.4 -> moved to follow-up `phase16_vfx-sort-pipeline-hardware-validation` section 4.3

## 7. Tail (mandatory — enforced by rulebook v5.3.0)
- [x] 7.1 Update or create documentation covering the implementation — `docs/engine/25-vfx/99-sort-pipeline.md` shipped (data flow, CB layout, sort modes, sprite VS wiring, mesh CS slot allocation, backward compat, test coverage matrix, sample assets)
- [x] 7.2 Write tests covering the new behavior — 132 sort + mesh + leak-fix tests across 9 suites (`VFXSortConstants`, `GPUParticleSort`, `UZ_MeshSortMode`, `UZ_MeshRenderModule`, `UZ_MeshRenderModule_Sort`, `UZ_MeshSortUzfxPipeline`, `UzfxLoader.MeshRender_*`, `UZ_MeshRenderModule_LeakFix`, `UZ_ParticleEmitter_LeakFix`)
- [x] 7.3 Run tests and confirm they pass — `runtime/build/bin/test_runtime.exe --gtest_filter="VFXSortConstants*:GPUParticleSort*:UZ_MeshSortMode*:UZ_MeshRenderModule*:UZ_MeshSortUzfxPipeline*:UzfxLoader*:UZ_*LeakFix*"` -> 132/132 PASS
