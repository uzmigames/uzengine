## 1. Core Infrastructure

- [ ] 1.1 Create `ShaderParameterMetadata` struct in `runtime/engine/renderer/core/shader_parameters.h` — stores array of member descriptors (name, offset, size, type: Scalar/Texture/Sampler/UAV), CB total size, resource count.
- [ ] 1.2 Create `UZ_BEGIN_SHADER_PARAMETERS` / `UZ_END_SHADER_PARAMETERS` macros that generate a struct + static `GetMetadata()` method returning `ShaderParameterMetadata`.
- [ ] 1.3 Create `UZ_SHADER_PARAMETER(Type, Name)` macro for scalar members (float, Vec4, Mat4, uint32_t) — adds to CBV layout with automatic 16-byte alignment.
- [ ] 1.4 Create `UZ_SHADER_PARAMETER_TEXTURE(Dimension, Name)` macro for texture members — stores `RHITextureHandle` in struct, registers as resource binding in metadata.
- [ ] 1.5 Create `UZ_SHADER_PARAMETER_SAMPLER(Name)` macro for sampler members — stores `RHISamplerHandle` in struct, registers as sampler binding.
- [ ] 1.6 Create `UZ_SHADER_PARAMETER_UAV(Format, Name)` macro for UAV members — stores `RHITextureHandle`, registers as UAV binding.
- [ ] 1.7 Implement `ShaderParameterBinder::Bind(IRHICommandList*, const void* structData, const ShaderParameterMetadata&, CBRingAllocator&, uint32_t cbSlot)` — uploads scalar members to CBV, binds textures via `SetFragmentTexture`, binds samplers via `SetFragmentSampler`.
- [ ] 1.8 Handle DX12 bindless path: texture members use `SetDescriptorIndex` with the texture's SRV heap index instead of `SetFragmentTexture`.
- [ ] 1.9 Handle compute shader path: use `CSSetShaderResources`/`CSSetUnorderedAccessViews` instead of `PSSetShaderResources` when `m_computePipelineBound`.

## 2. HLSL Code Generation

- [ ] 2.1 Create `ShaderParameterLayout` — maps metadata member names to HLSL register slots (t0, t1, s0, etc.) based on declaration order.
- [ ] 2.2 Generate HLSL cbuffer declaration from metadata — automatic `row_major float4x4`, correct `float4` packing for arrays, matching C++ layout exactly.
- [ ] 2.3 Generate HLSL texture/sampler declarations from metadata — `Texture2D<float4> Name : register(tN)` with auto-assigned slots.
- [ ] 2.4 Implement as either: (a) compile-time header generation, or (b) runtime string injection via shader defines. Choose based on complexity.

## 3. RHI Integration

- [ ] 3.1 Add `SetComputeTexture(uint32_t slot, RHITextureHandle)` to `IRHICommandList` — DX11: `CSSetShaderResources`, DX12: descriptor table, Vulkan: descriptor set update.
- [ ] 3.2 Add `SetComputeUAV(uint32_t slot, RHITextureHandle)` to `IRHICommandList` — DX11: `CSSetUnorderedAccessViews`, DX12: descriptor table, Vulkan: descriptor set.
- [ ] 3.3 Add `GetBufferUAVIndex` implementation for DX11 and Vulkan (currently stubs returning UINT32_MAX).
- [ ] 3.4 Ensure `GetTextureUAVIndex` returns valid sentinel on DX11/Vulkan (partially done, verify).

## 4. Migration: PerFrameCB

- [ ] 4.1 Convert `PerFrameCB` from manual struct to `UZ_BEGIN_SHADER_PARAMETERS(PerFrameParams)` with all existing members.
- [ ] 4.2 Add IBL texture members: `UZ_SHADER_PARAMETER_TEXTURE(TextureCube, IrradianceCubemap)`, `UZ_SHADER_PARAMETER_TEXTURE(TextureCube, PrefilteredSpecular)`, `UZ_SHADER_PARAMETER_TEXTURE(Texture2D, BrdfLut)`.
- [ ] 4.3 Update all passes that upload PerFrameCB to use `ShaderParameterBinder::Bind` instead of manual `memcpy + SetRootCBV`.
- [ ] 4.4 Update `deferred_lighting_ps.hlsl` to use generated declarations — remove `#ifdef UZ_BINDLESS` for IBL texture access, use the same texture names on all backends.
- [ ] 4.5 Update `mesh_ps.hlsl` PerFrameCB declaration to match.

## 5. Migration: ShadowDataCB

- [ ] 5.1 Convert `ShadowDataCB` to shader parameter struct — eliminates `float4 cascadeSplitsPacked[2]` hack, uses proper `UZ_SHADER_PARAMETER_ARRAY(float, CascadeSplits, 8)`.
- [ ] 5.2 Remove `#define cascadeSplits(i)` accessor macros from HLSL — direct array access works when layout is managed by the system.
- [ ] 5.3 Add shadow atlas as texture member: `UZ_SHADER_PARAMETER_TEXTURE(Texture2D, ShadowAtlas)` — eliminates manual SRV index passing.
- [ ] 5.4 Update `shadow_common.hlsli` to use generated declarations.

## 6. Migration: Deferred Lighting Pass

- [ ] 6.1 Create `DeferredLightingParams` shader parameter struct with all G-Buffer textures, shadow atlas, IBL textures, and lighting CBV data.
- [ ] 6.2 Update `AddLightingPass` to fill `DeferredLightingParams` and call `ShaderParameterBinder::Bind`.
- [ ] 6.3 Remove all manual `SetFragmentTexture`, `SetRootCBV`, `SetDescriptorIndex` calls from the lighting pass.
- [ ] 6.4 Verify DX11 deferred lighting renders IBL correctly (irradiance + prefiltered + BRDF LUT).
- [ ] 6.5 Verify Vulkan deferred lighting renders IBL correctly.

## 7. Migration: GBuffer + Shadow Passes

- [ ] 7.1 Convert GBuffer pass to use shader parameter struct for per-object and per-frame bindings.
- [ ] 7.2 Convert Shadow pass to use shader parameter struct.
- [ ] 7.3 Convert PBR forward pass to use shader parameter struct.

## 8. Testing

- [ ] 8.1 Unit test: `ShaderParameterMetadata` correctly separates scalar vs texture vs sampler members.
- [ ] 8.2 Unit test: CB layout matches expected size with 16-byte alignment padding.
- [ ] 8.3 Unit test: `ShaderParameterBinder::Bind` produces correct CB data blob (compare byte-for-byte with manual memcpy).
- [ ] 8.4 Visual test: DX12 renders identically before and after migration (no regression).
- [ ] 8.5 Visual test: DX11 renders sphere with IBL lighting (irradiance + specular + BRDF LUT) — should match DX12.
- [ ] 8.6 Visual test: Vulkan renders sphere with IBL lighting — should match DX12.
- [ ] 8.7 Visual test: Compose 3-backend comparison grid and verify visual parity via `--compose-grid`.
- [ ] 8.8 Visual test: Shadow tile in buffer viz grid shows shadows on all 3 backends.
- [ ] 8.9 Verify 95%+ coverage on new shader parameter code.

## 9. Documentation

- [ ] 9.1 Create `docs/engine/03-rhi/shader-parameter-system.md` — architecture, macro reference, migration guide.
- [ ] 9.2 Update `docs/architecture/coding-standards.md` with shader parameter rules (always use macros, never manual register assignment).
- [ ] 9.3 Update CHANGELOG with uniform buffer system entry.
