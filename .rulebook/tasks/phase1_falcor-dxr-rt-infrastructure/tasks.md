## Session 1: RHI RT Type Declarations

- [ ] 1.1 Add RT structs to `rhi.h`: `RhiGeometryDesc` (vertex buffer, index buffer, vertex stride, format, count), `RhiAccelerationStructureDesc` (type BLAS/TLAS, geometry list, flags), `RhiInstanceDesc` (BLAS handle, transform 3x4, instance ID, mask, hit group offset), `RhiDispatchRaysDesc` (raygen table, miss table, hit table, width, height, depth)
- [ ] 1.2 Add RHI vtable entries: `rhi_create_acceleration_structure(desc) → RhiASHandle`, `rhi_build_acceleration_structure(cmd, handle, scratch)`, `rhi_compact_acceleration_structure(cmd, src, dst)`, `rhi_dispatch_rays(cmd, desc)`, `rhi_create_rt_pipeline(desc) → RhiPipelineHandle`
- [ ] 1.3 Add Vulkan backend stubs returning `RHI_NOT_SUPPORTED` for all RT vtable entries (ensures Vulkan builds cleanly)
- [ ] 1.4 Build + verify: `zig build` compiles on both DX12 and Vulkan paths with no errors

## Session 2: BLAS Manager

- [ ] 2.1 Create `blas_manager.h/.cpp` — `BLASManager` class: `GetOrBuildBLAS(MeshId, vertexBuffer, indexBuffer) → RhiASHandle`. Cache key is `MeshId`. On miss: allocate scratch buffer, call `rhi_build_acceleration_structure`, schedule compaction
- [ ] 2.2 DX12 implementation of `rhi_build_acceleration_structure` for BLAS: fill `D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC` from `RhiAccelerationStructureDesc`, call `BuildRaytracingAccelerationStructure` on command list
- [ ] 2.3 BLAS compaction: after build, insert UAV barrier, copy compacted size to readback buffer, call `CopyRaytracingAccelerationStructure` with COMPACT flag. Cache the compacted BLAS handle.
- [ ] 2.4 `BLASManager::InvalidateMesh(MeshId)` — removes cached BLAS, triggers rebuild next frame
- [ ] 2.5 Build + verify: single mesh BLAS builds without device removal; validation layer reports no errors

## Session 3: TLAS Manager

- [ ] 3.1 Create `tlas_manager.h/.cpp` — `TLASManager` class: `Rebuild(scene, cmd)` iterates all visible meshes, collects `RhiInstanceDesc` (transform from TransformComponent, BLAS from BLASManager, material ID as instanceID)
- [ ] 3.2 DX12 implementation: fill `D3D12_RAYTRACING_INSTANCE_DESC` array in upload buffer; call `rhi_build_acceleration_structure` for TLAS with `ALLOW_UPDATE` flag for frame-to-frame update
- [ ] 3.3 Expose `GetTLASSRV() → RhiDescriptorHandle` for binding to RT shader descriptor tables
- [ ] 3.4 Add UAV barrier after TLAS build to ensure visibility before `DispatchRays`
- [ ] 3.5 Build + verify: TLAS builds with 3+ meshes in scene; no device removal; GPU validation layer clean

## Session 4: Shader Table Builder + RT PSO

- [ ] 4.1 Create `shader_table_builder.h/.cpp` — `ShaderTableBuilder`: `AddRayGenShader(name)`, `AddMissShader(name)`, `AddHitGroup(name, closestHit, anyHit, intersection)`. `Build(device, pipeline) → ShaderTableBuffers { raygen, miss, hitGroup }` aligned to `D3D12_RAYTRACING_SHADER_TABLE_BYTE_ALIGNMENT`
- [ ] 4.2 DX12 RT PSO creation: `rhi_create_rt_pipeline` implementation — `D3D12_STATE_OBJECT_DESC` with `D3D12_STATE_SUBOBJECT_TYPE_DXIL_LIBRARY` for each HLSL lib shader, hit group subobjects, payload/attribute config, max recursion depth
- [ ] 4.3 Compile HLSL `lib_6_6` profile shaders via DXC: enable in `ShaderManager` with target profile detection (`.rgen`, `.rmiss`, `.rchit` extensions → `lib_6_6`)
- [ ] 4.4 Build + verify: trivial RT PSO with a ray gen shader compiles and creates without error

## Session 5: RTManager + Render Graph Integration + End-to-End Test

- [ ] 5.1 Create `rt_manager.h/.cpp` — `RTManager` singleton: owns `BLASManager`, `TLASManager`. `BeginFrame(scene, cmd)` rebuilds TLAS. `GetTLASSRV()` forwarded from `TLASManager`. Registered in application init.
- [ ] 5.2 Add `RT_PASS` pass type to render graph: differs from compute pass by binding TLAS SRV automatically and using `rhi_dispatch_rays` instead of `rhi_dispatch`
- [ ] 5.3 Write trivial test RT render graph pass `rt_uv_test_pass.h/.cpp`: ray gen shader outputs `float2(DispatchRaysIndex.xy) / DispatchRaysDimensions.xy` to UAV texture (UV gradient visualization). No actual ray intersection needed.
- [ ] 5.4 Add CLI flag `--rt-test` that enables the UV test pass and captures output via `--screenshot-stdout`. Expected output: smooth UV gradient across the full frame.
- [ ] 5.5 Build + verify: `demo.exe --rt-test --screenshot-stdout --frames 5` produces a valid UV gradient screenshot. GPU validation layer reports no errors. Confirm TLAS builds for all meshes in scene.
