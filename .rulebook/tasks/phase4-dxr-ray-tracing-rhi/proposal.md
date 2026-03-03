# Proposal: phase4-dxr-ray-tracing-rhi

## Why
DXR (DirectX Raytracing) enables raytraced reflections, shadows, ambient occlusion, and global illumination that rasterization cannot match in quality. RTX-class GPU hardware is now mainstream across mid-range and high-end consumer cards. Without a DXR RHI layer, none of these effects can ever be added to UzEngine regardless of demand. The RHI abstraction must be extended before any raytracing feature can be implemented on top of it.

## What Changes
- Add `rhi_ray_tracing.h` as an extension to `rhi.h` defining the DXR resource types and dispatch interface
- Implement `RHIAccelerationStructure` covering both BLAS (per-mesh geometry) and TLAS (per-frame scene instance list)
- Implement `RHIRayTracingPipeline` accepting Ray Generation, Miss, and Hit Group shader stages
- DX12 backend: create state objects via `ID3D12Device5::CreateStateObject` and dispatch via `DispatchRays()`
- Startup capability query using `D3D12_FEATURE_DATA_D3D12_OPTIONS5::RaytracingTier`
- Fallback path that routes to rasterized equivalents when DXR is unsupported (integrated GPUs, older hardware)
- BLAS build and compaction pass integrated into the asset pipeline for static geometry
- TLAS rebuild each frame for dynamic entities (skinned meshes, moving props)

## Impact
- Affected specs: `docs/specs/renderer.md`, `docs/specs/shader-system.md`, `docs/specs/shader-library.md`
- Affected code: `src/rhi/rhi.h`, `src/rhi/dx12/dx12_device.cpp`, `src/renderer/renderer.cpp`, `src/asset/mesh_importer.cpp`
- Breaking change: NO
- User benefit: Unlocks hardware-accelerated raytraced shadows, reflections, and AO as opt-in renderer features, with automatic fallback so existing projects continue working on all hardware.
