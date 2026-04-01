# Proposal: phase1_falcor-dxr-rt-infrastructure

## Why
UzEngine has zero ray tracing support. Every RT feature — RT shadows, RT reflections, RT ambient occlusion, path tracing, ReSTIR DI — requires acceleration structures, shader tables, and `DispatchRays`. Falcor has a complete DXR pipeline abstracted behind its RHI, enabling all RT features to share the same TLAS/BLAS infrastructure. Without this foundation, each RT feature would have to build its own raw DX12 RT pipeline, leading to duplicated error-prone boilerplate and no abstraction boundary. This task establishes the RHI RT abstraction and DX12 backend implementation that all subsequent RT features will consume.

## What Changes
- RHI RT types in `rhi.h`: `RhiAccelerationStructureDesc`, `RhiShaderTableDesc`, `RhiDispatchRaysDesc`, `RhiRaytracingPipelineDesc`; vtable entries: `rhi_create_blas`, `rhi_create_tlas`, `rhi_build_acceleration_structure`, `rhi_dispatch_rays`
- `BLASManager` class: builds and caches per-mesh BLAS from vertex/index buffers, triggers rebuild on mesh change, performs BLAS compaction after initial build
- TLAS creation: `TLASManager` builds top-level AS from instance transforms + BLAS references each frame; per-instance data buffer (transform, material ID, geometry flags)
- Shader table: `ShaderTableBuilder` constructs ray generation, miss, and hit group entries; wraps `D3D12_STATE_OBJECT_DESC` for RT PSO creation from HLSL `lib_6_6` profile shaders
- `RTManager` singleton: owns BLAS/TLAS lifecycle, exposes `GetTLASSRV()` for use by RT render graph passes
- New render graph pass type `RT_PASS` in render graph infrastructure
- DX12 backend implementation of all vtable entries; Vulkan stub returning `NOT_SUPPORTED` (Vulkan RT in separate task)
- Affected files: `rhi.h`, new `blas_manager.h/.cpp`, new `tlas_manager.h/.cpp`, new `shader_table_builder.h/.cpp`, new `rt_manager.h/.cpp`, `dx12_device.cpp`

## Impact
- Affected specs: `docs/specs/rendering/ibl-pipeline.md`, new spec `docs/specs/rendering/dxr-ray-tracing-rhi.md`
- Affected code: `rhi.h` (vtable additions), `dx12_device.cpp`, new RT management classes
- Breaking change: NO — all new vtable entries; existing raster path is unaffected; Vulkan backend returns stub until RT task
- User benefit: All RT features (shadows, reflections, path tracer, ReSTIR) share one TLAS; zero per-feature boilerplate; Vulkan RT can be added later by implementing the stub vtable entries
