## 1. Core Infrastructure

- [x] 1.1 Create `ShaderParameterMetadata` struct in `runtime/engine/renderer/core/shader_parameters.h` — stores array of member descriptors (name, offset, size, type: Scalar/Texture/Sampler/UAV), CB total size, resource count.
- [x] 1.2 Create `BEGIN_SHADER_PARAMETER_STRUCT` / `END_SHADER_PARAMETER_STRUCT` macros in `shader_parameter_macros.h`. Macros: `SHADER_PARAMETER`, `SHADER_PARAMETER_ARRAY`, `SHADER_PARAMETER_SCALAR_END`, `SHADER_PARAMETER_TEXTURE`, `SHADER_PARAMETER_SAMPLER`, `SHADER_PARAMETER_UAV`. Metadata definition macros: `BEGIN_SHADER_PARAMETER_METADATA` / `METADATA_SCALAR` / `METADATA_TEXTURE2D` / `METADATA_TEXTURE_CUBE` / `METADATA_SAMPLER` / `END_SHADER_PARAMETER_METADATA`.
- [x] 1.3-1.6 Macro variants for Texture/Sampler/UAV — implemented in `shader_parameter_macros.h`.
- [x] 1.7 Implement `ShaderParameterBinder::Bind()` — uploads scalar members to CBV, binds textures via `SetFragmentTexture`, binds samplers via `SetFragmentSampler`. Implemented in `shader_parameter_binder.h/cpp`.
- [x] 1.8 Handle DX12 bindless path: skips `SetFragmentTexture` when bindless is active (textures accessed via descriptor heap indices in the CB).
- [x] 1.9 Handle compute shader path: DX11 uses `CSSetConstantBuffers`/`CSSetShaderResources` when `m_computePipelineBound`. Implemented.

## 2. HLSL Code Generation

- [x] 2.1 `GenerateHLSL()` function in `shader_parameter_hlsl.h/cpp` — generates HLSL cbuffer + texture/sampler declarations from metadata. Used for validation that C++ struct matches HLSL layout.
- [x] 2.2 Generate HLSL cbuffer declaration from metadata — infers HLSL types from byte size (float, float4, float4x4, etc.), outputs register annotations.
- [x] 2.3 Generate HLSL texture/sampler declarations from metadata — outputs `Texture2D name : register(tN)`, `TextureCube name : register(tN)`, `SamplerState name : register(sN)`.
- [x] 2.4 Implementation approach: runtime string generation via `GenerateHLSL(meta, structName, cbRegister)`. Shaders still use manual HLSL, but this can validate C++ struct matches HLSL layout.

## 3. RHI Integration

- [x] 3.1 DX11 `CreateComputePipeline` implemented — creates `ID3D11ComputeShader`, binds via `CSSetShader`. Compute CB/SRV/UAV binding routes to CS stage when `m_computePipelineBound`.
- [x] 3.2 Vulkan `CreateComputePipeline` implemented — creates `VkComputePipeline`, binds via `VK_PIPELINE_BIND_POINT_COMPUTE`.
- [x] 3.3 `GetBufferUAVIndex` implemented for DX11 (returns pool index when UAV exists) and Vulkan (returns `uavIndex` from buffer pool).
- [x] 3.4 `GetTextureUAVIndex` returns valid sentinel on DX11/Vulkan (pool index).
- [x] 3.5 DX11 `SetDescriptorTable` now handles UAV_Table — checks root layout param type, binds via `CSSetUnorderedAccessViews` when UAV. Added `FindTextureUAVByPoolIndex`/`FindBufferUAVByPoolIndex` to DX11Device.
- [x] 3.6 Vulkan `Dispatch()` now calls `FlushDescriptorSet()` before `vkCmdDispatch` — compute dispatches were missing descriptor binding.
- [x] 3.7 Vulkan `FlushDescriptorSet` picks correct bind point (`VK_PIPELINE_BIND_POINT_COMPUTE` vs `GRAPHICS`) via `m_computePipelineBound` tracking.
- [x] 3.8 Vulkan descriptor set layout extended with STORAGE_IMAGE bindings (48-63) for UAV support. Added `-fvk-u-shift 48 0` to shader compiler args.
- [x] 3.9 Vulkan `BeginRenderPass` creates per-slice `VkImageView` temporaries for cubemap face rendering. Destroyed in `EndRenderPass`.
- [x] 3.10 DX11 `BeginRenderPass` creates per-slice RTVs via `D3D11_RENDER_TARGET_VIEW_DESC` with `Texture2DArray` when `arraySlice != 0`.
- [x] 3.11 DX11 cubemap UAV dimension fix — `CreateTexture` UAV must use `D3D11_UAV_DIMENSION_TEXTURE2DARRAY` when `depthOrArraySize > 1`. Fixed.
- [x] 3.12 DX11 compute sampler binding — `Begin()` must call `CSSetSamplers(0, 2, defaultSamplers)` in addition to `PSSetSamplers`. Fixed.
- [x] 3.13 DX11 reversed-Z depth test — investigated and confirmed NOT the issue. Depth function already defaults to `Greater` (rhi_types.h:463). See 10.5 for details. Actual root cause was WC memory (10.8).

## 4. Migration: PerFrameCB

- [x] 4.1 Created `PerFrameParams` in `per_frame_params.h/cpp` — scalar section byte-identical to `PerFrameCB` (static_assert verified), resource handles appended after.
- [x] 4.2 Added IBL texture members: `irradianceCubemap`, `prefilteredSpecular`, `brdfLut` with slots t5, t6, t7.
- [x] 4.3 Lighting pass migrated to `ShaderParameterBinder::Bind` — when `perFrameParams != nullptr`, binder uploads 512B scalar section + binds IBL textures at t5-t7 via metadata. Legacy manual path preserved as fallback.
- [x] 4.4 Updated `deferred_lighting_ps.hlsl` — SM5 path declares `iblIrradiance(t5)`, `iblPrefiltered(t6)`, `iblBrdfLut(t7)`. Unified IBL ambient path works on both SM5 and SM6.6.
- [x] 4.5 `scene_renderer.cpp` now uses `PerFrameParams` instead of `PerFrameCB`. IBL texture handles assigned from `m_iblBaker`. Passed to lighting pass via `lightDesc.perFrameParams`.

## 5. Migration: ShadowDataCB

- [x] 5.1 Created `ShadowDataParams` in `shadow_data_params.h/cpp` — scalar section byte-identical to `ShadowDataCB` (768 bytes, static_assert verified). 19 scalar + 3 texture members in metadata.
- [x] 5.2 Remove per-shader `#define cascadeSplits(i)` macros from `deferred_lighting_ps.hlsl` and `mesh_ps.hlsl`. Moved to `shadow_common.hlsli` as helper functions (`GetCascadeSplit`, `GetCascadeFadeStart`, `GetCascadeDepthBias`, `GetCascadeNormalBias`) with convenience macros.
- [x] 5.3 Added shadow atlas (t3), static shadow atlas (t8), DFS output texture (t9) as texture members.
- [x] 5.4 Updated `shadow_common.hlsli` — cascade array accessors centralized as inline helper functions with `#ifndef` guards. All per-shader `#define` duplicates removed.
- [x] 5.5 Lighting pass binder path wired for ShadowDataParams — `scene_renderer.cpp` constructs `ShadowDataParams` from `SetupShadowFrame()` output, sets texture handles, passes to `lightDesc.shadowDataParams`. Binder uploads 768B CB + binds shadow textures via metadata on SM5.

## 6. Migration: Deferred Lighting Pass

- [x] 6.1 `LightingPassDesc` extended with `iblIrradiance`, `iblPrefiltered`, `iblBrdfLut` texture handles.
- [x] 6.2 `lighting_pass.cpp` binds IBL textures at t5-t7 via `SetFragmentTexture` when handles are valid.
- [x] 6.3 Binder path implemented — when `perFrameParams != nullptr`, `ShaderParameterBinder::Bind()` replaces manual `SetRootCBV` + `SetFragmentTexture`. Driven by metadata, not hardcoded slots.
- [x] 6.4 DX11 IBL visual parity. SM5 pixel shader path (`sky_cubemap_ps.hlsl` + `sky_cubemap_capture_ps_pass.cpp`) renders sky cubemap via 6 fullscreen draws with per-slice RTVs. IBL bake chain works end-to-end on DX11.
- [x] 6.5 Vulkan IBL visual parity. Vulkan `BeginRenderPass` creates per-slice `VkImageView` temporaries. SM5 pixel shader path works identically to DX11.

## 7. Migration: GBuffer + Shadow Passes

- [x] 7.1 Convert GBuffer pass to shader parameter struct. Created `GBufferPerObjectParams` (scalar section byte-identical to `PerObjectCB`, appends t0-t4 material texture handles). `gbuffer_pass.cpp` uses `ShaderParameterBinder::Bind()` for per-object CB + SM5 textures, `BindCB()` for per-frame via `PerFrameParams`. Legacy path preserved as fallback when `cbRingAllocator == nullptr`. Instanced path also migrated.
- [x] 7.2 Convert Shadow pass to shader parameter struct. Created `ShadowCascadeParams` (viewProj + pad to 256B) and `ShadowPerObjectParams` (byte-identical to `PerObjectCB`, no textures — depth-only). `shadow_pass.cpp` uses `ShaderParameterBinder::BindCB()` for both cascade CB (slot 3) and per-object CB (slot 2). Legacy path preserved. Instanced path migrated.
- [x] 7.3 Convert PBR forward pass to shader parameter struct. Reuses `GBufferPerObjectParams` (same CB layout + material textures). `pbr_pass.cpp` uses `ShaderParameterBinder::Bind()` for per-object CB + SM5 textures, `BindCB()` for per-frame via `PerFrameParams`. Legacy path preserved.

## 8. Testing

- [x] 8.1 Unit test: metadata separates scalar vs texture vs sampler. 11 macro tests in `test_shader_parameters.cpp`.
- [x] 8.2 Unit test: CB layout matches expected size. `AlignCBSize.*`, `PerFrameParams.MetadataCBSizeIs512`, `ShaderParamShadowCB.SizeIs768Bytes`.
- [x] 8.3 Unit test: binder produces correct CB blob. `PerFrameParams.ScalarSectionByteIdenticalToPerFrameCB`, `ShadowDataParams.ScalarSectionByteIdenticalToShadowDataCB`, `LightingParams.ScalarSectionByteIdenticalToLightingCB`.
- [x] 8.4 DX12 renders identically after migration (verified — no regression).
- [x] 8.5 DX11 FULL visual parity with DX12. PBR sphere + sky atmosphere + stats overlay + IBL + shadows. Screenshots: `.screenshots/dx11_stats_fixed.png` vs `.screenshots/dx12_compare_final.png`. All fixes: WC memory (10.8), buffer UAV (10.9), stale SRV (10.10), depth SRV/DSV (10.11), tonemap SM5 (10.12), sky transmittance LUT (10.13), SetFragmentSampler (10.14), MapBuffer scope (10.15).
- [x] 8.6 Vulkan visual parity. Verified — PBR sphere + sky + IBL + shadows render identically to DX12. Screenshot: `.screenshots/final_vulkan.png`. Zero Vulkan validation errors.
- [x] 8.7 3-backend PBR comparison — screenshots captured for DX12, DX11, Vulkan side-by-side. All 3 render identical PBR sphere with sky, IBL, shadows. Screenshots: `.screenshots/grid_dx12.png`, `.screenshots/grid_dx11.png`, `.screenshots/grid_vulkan.png`.
- [x] 8.8 Shadow debug grid on all 3 backends — 9-channel grid (Albedo, Normal, Shadow Atlas, Shadow, LIT, Depth, Cascade, AO, Roughness) verified identical on DX12/DX11/Vulkan. Screenshots: `.screenshots/shadow_pbr_dx12.png`, `.screenshots/shadow_pbr_dx11.png`, `.screenshots/shadow_pbr_vulkan.png`.
- [x] 8.9 56 unit tests passing — macro-generated structs, HLSL generation, ShadowDataParams, LightingParams, PerFrameParams metadata, slot mapping, offset verification.
- [x] 8.10 4 HLSL generation tests: cbuffer header, texture declarations, sampler declarations, PerFrameParams HLSL.
- [x] 8.11 9 ShadowDataParams tests: CB size, texture count, byte identity, resource slots, HLSL generation.
- [x] 8.12 6 LightingParams tests: CB size, no resources, byte identity, member count.

## 9. Documentation

- [x] 9.1 Created `docs/analysis/rendering/ue5-uniform-buffer-shader-binding-system.md` — UE5 pattern analysis.
- [x] 9.2 Updated `docs/guides/shader-parameter-binding-guide.md` — full rewrite documenting actual macro system, binder API, SM5 vs SM6.6 differences, migrated structs.
- [x] 9.3 Updated `runtime/CHANGELOG.md` — section `[0.2.30] — 2026-03-16` with UBS macros, HLSL gen, migrated structs, RHI fixes.
- [x] 9.4 Created `docs/guides/shader-parameter-migration-guide.md` — step-by-step guide with before/after code, worked ShadowDataParams example, pitfalls section.

## 10. RHI Backend Fixes (DX11/Vulkan Parity) — 2026-03-16

- [x] 10.1 DX11 `SetDescriptorTable` UAV binding — was always binding SRVs, now checks root layout param type for UAV_Table.
- [x] 10.2 Vulkan `Dispatch()` descriptor flush — compute dispatches were missing descriptor binding.
- [x] 10.3 Vulkan compute bind point — `FlushDescriptorSet` now picks `VK_PIPELINE_BIND_POINT_COMPUTE` when compute pipeline bound.
- [x] 10.4 Vulkan STORAGE_IMAGE descriptor layout — added bindings 48-63 + `-fvk-u-shift 48 0`.
- [x] 10.5 DX11 reversed-Z depth test — was NOT the issue. Depth function already defaults to `Greater` (rhi_types.h:463). The actual root cause was write-combined memory read-back (see 10.8).
- [x] 10.6 DX11 cubemap UAV dimension — already correct (`TEXTURE2DARRAY` for depthOrArraySize > 1).
- [x] 10.7 DX11 compute sampler binding — `CSSetSamplers` added in `Begin()`.
- [x] 10.8 **DX11 CB Ring write-combined memory** — ROOT CAUSE of DX11 black screen. DYNAMIC buffers use WC memory (write-only). `SetRootCBV` read from WC `alloc.cpuPtr` → all matrices arrived as zeros → vertices transformed to (0,0,0,0). Fix: added `shadowCopy` CPU-side buffer for DYNAMIC buffers. `MapBuffer` returns cacheable shadow copy instead of WC pointer.
- [x] 10.9 DX11 buffer UAV creation — non-structured buffers (auto-exposure histogram) now use `D3D11_RESOURCE_MISC_BUFFER_ALLOW_RAW_VIEWS` + `DXGI_FORMAT_R32_TYPELESS` + `D3D11_BUFFER_UAV_FLAG_RAW`.
- [x] 10.10 DX11 stale SRV/UAV bindings — added SRV/UAV clearing in `Begin()`, `SetPipeline()` (compute), and `BeginRenderPass()`. Matches UE5's `FD3D11DynamicRHI::CommitRenderTargets` pattern. Fixed IBL compute producing black output.
- [x] 10.11 DX11 depth SRV/DSV conflict — SRV clearing in `BeginRenderPass()` before `OMSetRenderTargets` prevents "Resource still bound on input" errors.
- [x] 10.12 DX11 tonemap SM5 path — `scene_renderer.cpp` was missing `SetFragmentTexture(0, m_hdrSceneColor)` for non-bindless path. Tonemap shader read zeros → black output.
- [x] 10.13 DX11 sky atmosphere transmittance LUT — `sky_atmosphere_pass.cpp` SM5 path never bound transmittance LUT at t1. Added `SetFragmentTexture(1, transmittanceLutTexture)`. Sky gradient now visible.
- [x] 10.14 DX11 `SetFragmentSampler` — was no-op default in `IRHICommandList`. Implemented override in DX11 using `PSSetSamplers`/`CSSetSamplers`. Fixed stats overlay font rendering.
- [x] 10.15 DX11 `MapBuffer` scope — shadow copy path now restricted to `ConstantBuffer` usage only. Vertex/index buffers use direct `MAP_WRITE_DISCARD`. Fixed overlay vertices being zeros.
- [x] 10.16 Auto-exposure buffers — changed histogram and exposure buffers to `StructuredBuffer` with stride. HLSL changed from `RWByteAddressBuffer` to `RWStructuredBuffer<uint>`/`RWStructuredBuffer<float>`. Eliminated D3D11 RAW buffer mismatch errors.

## Status

**DX11 FULL VISUAL PARITY WITH DX12 ACHIEVED** (2026-03-17):
- PBR sphere with stone brick textures ✅
- IBL ambient lighting (irradiance + prefiltered specular) ✅
- Shadows (CSM cascade) ✅
- Sky atmosphere gradient ✅
- Stats overlay (GPU, FPS, draw calls) ✅
- Checkerboard floor ✅
- Zero D3D11 Debug Layer errors ✅
- Screenshots: `.screenshots/dx11_stats_fixed.png` vs `.screenshots/dx12_compare_final.png` vs `.screenshots/final_vulkan.png`

**ALL ITEMS COMPLETE.** Phase0-0.8.0 Uniform Buffer System is fully implemented and verified on all 3 backends (DX12, DX11, Vulkan) with visual parity confirmed via screenshots.
