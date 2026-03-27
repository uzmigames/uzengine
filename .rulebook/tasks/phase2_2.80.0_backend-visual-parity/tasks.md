## Phase 0: Pipeline Creation Crashes (BLOCKING — SM6.0 is unusable without these)
- [ ] 0.1 Fix `GBuffer_Instanced` pipeline failure (E_INVALIDARG): instanced vertex shader uses StructuredBuffer vertex fetch without `#ifndef UZ_BINDLESS` fallback with Input Assembler VSInput. Add SM5 instanced VS path.
- [ ] 0.2 Fix `ShadowDepth_Instanced` pipeline failure: same root cause as 0.1 — instanced shadow depth VS needs non-bindless path.
- [ ] 0.3 Fix `DepthPrepass_Instanced` pipeline failure: same root cause as 0.1.
- [ ] 0.4 Fix `OutlineMaskPipeline` failure: outline mask uses `mesh_vs.hlsl` which in non-bindless mode uses Input Assembler, but the pipeline may have wrong input layout or root signature mismatch. Verify input element desc matches VSInput struct.
- [ ] 0.5 Verify SM6.0 runs without any pipeline creation errors: `--sm 6.0 --log-level error` shows zero RHI errors.

## Phase 1: SM6.0 Shader Parity (visual differences)
- [ ] 1.1 Fix deferred_lighting_ps.hlsl: add `register(t12)` for SSR texture and `register(t13)` for SSGI texture in `#ifndef UZ_BINDLESS` block. Wire sampling with null-guard (`if ssrEnabled`).
- [ ] 1.2 Fix lighting_pass.cpp: add `SetFragmentTexture(12, desc.ssrTexture)` and `SetFragmentTexture(13, desc.ssgiTexture)` in SM6.0 path when textures are valid.
- [ ] 1.3 Fix gbuffer_ps.hlsl SM5 metallic sampling: change `metallicTexture.SampleLevel(clampSampler, uv, 0)` to `metallicTexture.SampleBias(linearSampler, uv, mipBias)` to match SM6.6 path.
- [ ] 1.4 Fix gbuffer_pass.cpp: bind opacity texture at `t6` via `SetFragmentTexture(6, mat.opacityTexture)` when available in SM5 path.
- [ ] 1.5 Fix gbuffer_ps.hlsl SM5 normal mapping: gate normal map application on `normalTexture` being non-default (check if sampled value is flat blue 0.5,0.5,1.0 or add a CB flag).
- [ ] 1.6 Fix deferred_lighting_ps.hlsl SM5 IBL null-guard: wrap `iblIrradiance.Sample()` in `if (irradianceSrvIndex > 0)` check (read index from PerFrameCB even in SM5 path).
- [ ] 1.7 Fix tonemap_ps.hlsl: add `register(t3)` for lens flare texture in SM5 block, bind in frame_postprocess.cpp.
- [ ] 1.8 Build verification — `cd runtime && zig build` zero errors.

## Phase 2: DX11 Missing Samplers + SRV Multi-Bind
- [ ] 2.1 Create sampler s2 (bilinear clamp) in dx11_device.cpp — `D3D11_FILTER_MIN_MAG_LINEAR_MIP_POINT`, `ADDRESS_CLAMP`. Bind to PS slot 2 at device init.
- [ ] 2.2 Create sampler s3 (bilinear wrap) in dx11_device.cpp — `D3D11_FILTER_MIN_MAG_LINEAR_MIP_POINT`, `ADDRESS_WRAP`. Bind to PS slot 3 at device init.
- [ ] 2.3 Fix dx11_command_list.cpp `SetFragmentTexture`: support binding to arbitrary SRV slots (not just slot 0). Must call `PSSetShaderResources(slot, 1, &srv)` with the correct slot index.
- [ ] 2.4 Build + verify DX11 backend: `uzengine.exe --rhi dx11 --project ... --editor --screenshot`.

## Phase 3: DX11 Missing Passes — TAA
- [ ] 3.1 Remove `IsBindless()` gate on TAA in frame_setup.cpp:220. TAA should work on any backend that has compute shaders or use a PS-based resolve fallback.
- [ ] 3.2 If TAA compute shader requires bindless: implement PS-based TAA resolve (fullscreen triangle, sample history + current with neighborhood clamping).
- [ ] 3.3 Verify TAA works on DX11: screenshot comparison at multiple frames (temporal stability).

## Phase 4: DX11 Missing Passes — Bloom
- [ ] 4.1 Remove `IsBindless()` gate on bloom in bloom_pass.cpp:375. If bloom compute requires bindless for texture access, add register-based SM5 fallback in the compute shader.
- [ ] 4.2 Alternative: implement PS-based bloom (13-tap Kawase blur chain) as DX11 fallback.
- [ ] 4.3 Verify bloom on DX11: screenshot with bright light source, verify glow visible.

## Phase 5: DX11 3D Texture + Color Grading
- [ ] 5.1 Add 3D texture creation support in dx11_resource_creation.cpp — use `ID3D11Device::CreateTexture3D()`.
- [ ] 5.2 Add 3D SRV creation for `D3D11_SRV_DIMENSION_TEXTURE3D`.
- [ ] 5.3 Verify color grading LUT works on DX11: tonemap output uses correct color grade.

## Phase 6: SM6.0 Polish
- [ ] 6.1 Fix IBL baker barrier: ensure UAV→SRV transition in ibl_baker.cpp executes correctly on SM6.0 path.
- [ ] 6.2 Add sky cubemap multi-scattering: pass scattering 3D LUT to the PS sky capture path (requires 3D SRV at register, or bake multi-scattering into transmittance LUT).
- [ ] 6.3 Add HiZ single-mip fallback for SM6.0 occlusion culling (bind mip 0 only at t1).

## 7. Visual Verification
- [ ] 7.1 Screenshot DX12 SM6.6 with pbr_showcase: `--editor --screenshot dx12_sm66.png`.
- [ ] 7.2 Screenshot DX12 SM6.0 with pbr_showcase: `--editor --force-sm 6.0 --screenshot dx12_sm60.png`.
- [ ] 7.3 Screenshot DX11 with pbr_showcase: `--editor --rhi dx11 --screenshot dx11.png`.
- [ ] 7.4 Compare all 3 screenshots — verify identical visual output (PSNR > 35 dB between all pairs).
- [ ] 7.5 Test with fbx_test scene (mushroom monster): verify textures, shadows, reflections match across backends.
- [ ] 7.6 Test edge cases: masked material (opacity texture), metallic tiling material, IBL on first frame.

## 8. Testing
- [ ] 8.1 Unit test: DX11 sampler creation (s0-s3 all valid).
- [ ] 8.2 Unit test: DX11 SRV multi-bind (verify slots 0-13 all bindable).
- [ ] 8.3 Integration test: render frame on DX11, verify no D3D11 errors in debug layer.
- [ ] 8.4 Integration test: render frame on DX12 SM6.0, verify no D3D12 errors in debug layer.
- [ ] 8.5 Verify 95%+ coverage on all new/modified code.

## 9. Documentation
- [ ] 9.1 Update docs/analysis/dx11-sm6-parity-report.md with fix status for each issue.
- [ ] 9.2 Update CHANGELOG.md with backend parity fixes.
- [ ] 9.3 Update docs/guides/getting-started.md with backend selection flags (--rhi, --force-sm).
