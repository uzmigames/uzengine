## 1. RHI Ray Tracing Header Extension

- [ ] 1.1 Create `src/rhi/rhi_ray_tracing.h` extending `rhi.h` with forward declarations for: `RHIAccelerationStructure`, `RHIRayTracingPipeline`, `RHIShaderTable`, `RHIRayTracingDispatchDesc`
- [ ] 1.2 Define `struct RHIBLASDesc` in `rhi_ray_tracing.h` with fields: `vertex_buffer` (RHIBuffer handle), `vertex_stride` (uint32_t), `vertex_count` (uint32_t), `index_buffer` (RHIBuffer handle), `index_count` (uint32_t), `flags` (enum: None, Opaque, AllowUpdate)
- [ ] 1.3 Define `struct RHITLASDesc` in `rhi_ray_tracing.h` with fields: `instance_buffer` (RHIBuffer of `D3D12_RAYTRACING_INSTANCE_DESC` entries), `instance_count` (uint32_t), `flags` (enum: None, AllowUpdate)
- [ ] 1.4 Define `struct RHIRayTracingPipelineDesc` in `rhi_ray_tracing.h` with fields: `ray_gen_shader` (RHIShader), `miss_shaders` (vector), `hit_groups` (vector of RHIHitGroup), `max_recursion_depth` (uint32_t, default 1), `max_payload_size` (uint32_t)

## 2. Hardware Capability Query

- [ ] 2.1 In `src/rhi/dx12/dx12_device.cpp`, query `D3D12_FEATURE_DATA_D3D12_OPTIONS5::RaytracingTier` at device initialization
- [ ] 2.2 Store the result as `bool rhi_ray_tracing_supported_` on the device object; expose via `RHIDevice::supports_ray_tracing() const`
- [ ] 2.3 Log the detected raytracing tier to the engine log at startup: "DXR Tier 1.0", "DXR Tier 1.1", or "DXR Not Supported"

## 3. BLAS Implementation

- [ ] 3.1 Implement `RHIDevice::create_blas(RHIBLASDesc)` in `src/rhi/dx12/dx12_device.cpp` calling `ID3D12Device5::GetRaytracingAccelerationStructurePrebuildInfo()` to determine scratch and result sizes
- [ ] 3.2 Allocate scratch buffer (`D3D12_RESOURCE_STATE_UNORDERED_ACCESS`) and result buffer (`D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE`) on the GPU heap
- [ ] 3.3 Call `ID3D12GraphicsCommandList4::BuildRaytracingAccelerationStructure()` with `D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL`
- [ ] 3.4 After build, compact the BLAS using `EmitRaytracingAccelerationStructurePostbuildInfo()` and `CopyRaytracingAccelerationStructure(COMPACT)` to reduce GPU memory usage

## 4. TLAS Implementation

- [ ] 4.1 Implement `RHIDevice::create_tlas(RHITLASDesc)` building the top-level AS from an instance buffer of `D3D12_RAYTRACING_INSTANCE_DESC` entries
- [ ] 4.2 Implement `RHICommandList::update_tlas(RHIAccelerationStructure, instance_buffer, instance_count)` for per-frame TLAS rebuild for dynamic instances
- [ ] 4.3 Call TLAS rebuild each frame for all dynamic entities (skinned meshes, moving props) using `D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PERFORM_UPDATE` when possible

## 5. Ray Tracing Pipeline and Shader Table

- [ ] 5.1 Implement `RHIDevice::create_ray_tracing_pipeline(RHIRayTracingPipelineDesc)` calling `ID3D12Device5::CreateStateObject()` with a `D3D12_STATE_OBJECT_TYPE_RAYTRACING_PIPELINE` state object
- [ ] 5.2 Implement `RHIShaderTable` creation: allocate a GPU buffer containing Ray Generation, Miss, and Hit Group shader records with shader identifiers queried via `GetShaderIdentifier()`
- [ ] 5.3 Implement `RHICommandList::dispatch_rays(RHIRayTracingDispatchDesc)` calling `ID3D12GraphicsCommandList4::DispatchRays()` with the shader table buffer addresses and dispatch dimensions

## 6. Fallback Path

- [ ] 6.1 When `supports_ray_tracing() == false`, `create_blas()` and `create_tlas()` return null handles; callers must check the handle validity before use
- [ ] 6.2 Document in code comments that fallback to rasterized equivalents is the caller's responsibility; the RHI layer does not implement rasterized fallbacks itself

## 7. BLAS Asset Pipeline Integration

- [ ] 7.1 In `src/asset/mesh_importer.cpp`, after mesh import for static geometry, call `RHIDevice::create_blas()` and store the resulting BLAS handle in the mesh asset
- [ ] 7.2 Store the compacted BLAS handle in the mesh asset metadata for reuse across frames

## 8. Testing

- [ ] 8.1 Create `tests/renderer/test_dxr_rhi.cpp` testing `RHIDevice::supports_ray_tracing()` returns a value without crashing on any DX12 device
- [ ] 8.2 Test BLAS creation on a triangle mesh: verify the returned `RHIAccelerationStructure` handle is non-null when DXR is supported
- [ ] 8.3 Test BLAS compaction: verify compacted size <= pre-compaction size
- [ ] 8.4 Test TLAS creation from a single instance: verify handle is non-null when DXR is supported
- [ ] 8.5 Test `create_blas()` on a device without DXR support returns a null handle without crashing
- [ ] 8.6 Test shader table creation: verify all shader records have non-zero data (shader identifiers are populated)
- [ ] 8.7 Test `dispatch_rays()` with 1x1 dispatch completes without a D3D12 validation error (requires DXR-capable device; mark as skip if not available)
- [ ] 8.8 Test TLAS update (`update_tlas()`) changes instance positions on the GPU without requiring a full rebuild
- [ ] 8.9 Achieve >= 95% line and branch coverage across DXR path in `dx12_device.cpp` and `rhi_ray_tracing.h` API surface

## 9. Documentation

- [ ] 9.1 Update `docs/specs/renderer.md` documenting DXR capability query, BLAS/TLAS lifecycle, and the shader table format
- [ ] 9.2 Update `docs/specs/shader-system.md` documenting ray generation, miss, and hit group shader stages and their expected HLSL entry points
- [ ] 9.3 Update `docs/specs/shader-library.md` documenting the DXR fallback policy: callers must handle null handles for non-DXR hardware
