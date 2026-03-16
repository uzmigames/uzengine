## 1. Core Infrastructure

- [x] 1.1 Create `ShaderParameterMetadata` struct in `runtime/engine/renderer/core/shader_parameters.h` — stores array of member descriptors (name, offset, size, type: Scalar/Texture/Sampler/UAV), CB total size, resource count.
- [ ] 1.2 Create `UZ_BEGIN_SHADER_PARAMETERS` / `UZ_END_SHADER_PARAMETERS` macros that generate a struct + static `GetMetadata()` method. (Deferred — manual metadata via `GetMetadata()` static method used instead, same pattern as UE5 but without macro magic.)
- [ ] 1.3-1.6 Macro variants for Texture/Sampler/UAV. (Deferred — manual member registration in `GetMetadata()` works for now.)
- [x] 1.7 Implement `ShaderParameterBinder::Bind()` — uploads scalar members to CBV, binds textures via `SetFragmentTexture`, binds samplers via `SetFragmentSampler`. Implemented in `shader_parameter_binder.h/cpp`.
- [x] 1.8 Handle DX12 bindless path: skips `SetFragmentTexture` when bindless is active (textures accessed via descriptor heap indices in the CB).
- [x] 1.9 Handle compute shader path: DX11 uses `CSSetConstantBuffers`/`CSSetShaderResources` when `m_computePipelineBound`. Implemented.

## 2. HLSL Code Generation

- [ ] 2.1 Create `ShaderParameterLayout` — maps metadata member names to HLSL register slots. (Deferred — manual `register(tN)` in HLSL for now.)
- [ ] 2.2 Generate HLSL cbuffer declaration from metadata. (Deferred.)
- [ ] 2.3 Generate HLSL texture/sampler declarations from metadata. (Deferred.)
- [ ] 2.4 Choose implementation approach (compile-time vs runtime). (Deferred.)

## 3. RHI Integration

- [x] 3.1 DX11 `CreateComputePipeline` implemented — creates `ID3D11ComputeShader`, binds via `CSSetShader`. Compute CB/SRV/UAV binding routes to CS stage when `m_computePipelineBound`.
- [x] 3.2 Vulkan `CreateComputePipeline` implemented — creates `VkComputePipeline`, binds via `VK_PIPELINE_BIND_POINT_COMPUTE`.
- [ ] 3.3 `GetBufferUAVIndex` for DX11/Vulkan still returns UINT32_MAX — blocks auto-exposure histogram. (Next priority.)
- [x] 3.4 `GetTextureUAVIndex` returns valid sentinel on DX11/Vulkan (pool index).

## 4. Migration: PerFrameCB

- [x] 4.1 Created `PerFrameParams` in `per_frame_params.h/cpp` — scalar section byte-identical to `PerFrameCB` (static_assert verified), resource handles appended after.
- [x] 4.2 Added IBL texture members: `irradianceCubemap`, `prefilteredSpecular`, `brdfLut` with slots t5, t6, t7.
- [ ] 4.3 Update passes to use `ShaderParameterBinder::Bind`. (Partial — lighting pass binds IBL textures manually via `LightingPassDesc`, not yet via binder.)
- [x] 4.4 Updated `deferred_lighting_ps.hlsl` — SM5 path declares `iblIrradiance(t5)`, `iblPrefiltered(t6)`, `iblBrdfLut(t7)`. Unified IBL ambient path works on both SM5 and SM6.6.
- [ ] 4.5 Update `mesh_ps.hlsl`. (Not needed yet — forward pass uses different CB.)

## 5. Migration: ShadowDataCB

- [ ] 5.1 Convert `ShadowDataCB` to shader parameter struct.
- [ ] 5.2 Remove `#define cascadeSplits(i)` macros.
- [ ] 5.3 Add shadow atlas as texture member.
- [ ] 5.4 Update `shadow_common.hlsli`.

## 6. Migration: Deferred Lighting Pass

- [x] 6.1 `LightingPassDesc` extended with `iblIrradiance`, `iblPrefiltered`, `iblBrdfLut` texture handles.
- [x] 6.2 `lighting_pass.cpp` binds IBL textures at t5-t7 via `SetFragmentTexture` when handles are valid.
- [ ] 6.3 Remove manual binding — replace with `ShaderParameterBinder::Bind`. (Deferred until full migration.)
- [ ] 6.4 DX11 IBL visual parity. **BLOCKED**: sky cubemap capture compute shader requires bindless (`ResourceDescriptorHeap`). Without sky cubemap, IBL bake chain breaks — no irradiance/prefiltered textures to bind. Fix: implement SM5-compatible sky cubemap capture or CPU-side cubemap generation.
- [ ] 6.5 Vulkan IBL visual parity. **BLOCKED**: same as 6.4.

## 7. Migration: GBuffer + Shadow Passes

- [ ] 7.1 Convert GBuffer pass to shader parameter struct.
- [ ] 7.2 Convert Shadow pass to shader parameter struct.
- [ ] 7.3 Convert PBR forward pass to shader parameter struct.

## 8. Testing

- [ ] 8.1 Unit test: metadata separates scalar vs texture vs sampler.
- [ ] 8.2 Unit test: CB layout matches expected size.
- [ ] 8.3 Unit test: binder produces correct CB blob.
- [x] 8.4 DX12 renders identically after migration (verified — no regression).
- [ ] 8.5 DX11 IBL visual parity. (BLOCKED by 6.4.)
- [ ] 8.6 Vulkan IBL visual parity. (BLOCKED by 6.5.)
- [ ] 8.7 3-backend comparison grid.
- [ ] 8.8 Shadow on all 3 backends.
- [ ] 8.9 95%+ coverage.

## 9. Documentation

- [x] 9.1 Created `docs/analysis/rendering/ue5-uniform-buffer-shader-binding-system.md` — UE5 pattern analysis.
- [x] 9.2 Created `docs/guides/shader-parameter-binding-guide.md` — implementation guide.
- [ ] 9.3 Update CHANGELOG.

## Blockers

The remaining visual parity issue is NOT in the shader parameter system — it's in the **sky cubemap capture pipeline**:

1. `sky_cubemap_cs.hlsl` uses `ResourceDescriptorHeap[index]` (bindless-only)
2. Without sky cubemap, `IBLBaker::BakeEnvironment()` has no source cubemap to process
3. No irradiance or prefiltered cubemap textures are generated
4. The SM5 IBL shader path has working texture declarations (t5-t7) but nothing to bind

**Next steps**: Implement SM5-compatible sky cubemap capture (pixel shader render-to-cubemap instead of compute) or pre-bake cubemaps offline (UE5 approach for SM5).
