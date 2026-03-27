## Phase -1: DX11 Shader Compilation (BLOCKING — DX11 is 100% broken)
- [x] -1.1 Fix DX11 shader compiler selection: ApplyBindlessConfig() now forces SM5_0 for DX11 backend (FXC/DXBC). All CreateShader E_INVALIDARG errors resolved.
- [x] -1.2 Verify ALL vertex shaders compile on DX11/FXC. Verified: zero CreateShader stage=0 errors with `--rhi dx11 --log-level error` after clean build.
- [x] -1.3 Verify ALL pixel shaders compile on DX11/FXC. Verified: zero CreateShader stage=1 errors. All PS compiled successfully.
- [x] -1.4 Verify compute shaders on DX11. All CS compile with FXC/SM5.0. Zero CreateShader stage=2 errors after clean cache rebuild.
- [x] -1.5 Fix 3D texture creation on DX11. Added ID3D11Texture3D path with SRV (TEXTURE3D) + UAV (TEXTURE3D) support. APVolume, ScatteringLUT3D, DeltaRayleigh3D, DeltaMie3D, ScatteringDensity3D, ColorGradingLUT all create successfully.
- [x] -1.6 Verify DX11 runs with zero RHI errors: `--rhi dx11 --log-level error` shows zero CreateShader/CreateTexture/pipeline errors.

## Phase 0: SM6.0 Pipeline Creation Crashes (BLOCKING — SM6.0 partially broken)
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

## Phase 3: DX11 Missing Passes — TAA + Bloom
- [ ] 3.1 Remove `IsBindless()` gate on TAA in frame_setup.cpp:220, or implement PS-based TAA fallback.
- [ ] 3.2 Remove `IsBindless()` gate on bloom in bloom_pass.cpp:375, or implement PS-based bloom fallback.
- [ ] 3.3 Verify TAA + Bloom on DX11 via screenshot comparison.

## Phase 4: SM6.0 Polish
- [ ] 4.1 Fix IBL baker barrier: ensure UAV→SRV transition in ibl_baker.cpp executes correctly on SM6.0 path.
- [ ] 4.2 Add sky cubemap multi-scattering for SM6.0 PS capture path.
- [ ] 4.3 Add HiZ single-mip fallback for SM6.0 occlusion culling (bind mip 0 only at t1).

## 5. Visual Verification
- [ ] 5.1 Screenshot DX12 SM6.6 with fbx_test: `--editor --screenshot dx12_sm66.png`.
- [ ] 5.2 Screenshot DX12 SM6.0 with fbx_test: `--editor --sm 6.0 --screenshot dx12_sm60.png`.
- [ ] 5.3 Screenshot DX11 with fbx_test: `--editor --rhi dx11 --screenshot dx11.png`.
- [ ] 5.4 Screenshot Vulkan with fbx_test: `--editor --rhi vulkan --screenshot vulkan.png`.
- [ ] 5.5 Compare all 4 screenshots — verify identical visual output (PSNR > 35 dB).
- [ ] 5.6 Test edge cases: masked material, metallic tiling, IBL on first frame.

## 6. Testing
- [ ] 6.1 Integration test: render 15 frames on each backend with zero errors in log.
- [ ] 6.2 Verify 95%+ coverage on all new/modified code.

## 7. Documentation
- [ ] 7.1 Update docs/analysis/dx11-sm6-parity-report.md with fix status.
- [ ] 7.2 Update CHANGELOG.md with backend parity fixes.
- [ ] 7.3 Update docs/guides/getting-started.md with backend selection flags.
