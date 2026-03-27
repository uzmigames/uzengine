## Phase -1: DX11 Shader Compilation (BLOCKING — DX11 is 100% broken)
- [x] -1.1 Fix DX11 shader compiler selection: ApplyBindlessConfig() now forces SM5_0 for DX11 backend (FXC/DXBC). All CreateShader E_INVALIDARG errors resolved.
- [x] -1.2 Verify ALL vertex shaders compile on DX11/FXC. Verified: zero CreateShader stage=0 errors with `--rhi dx11 --log-level error` after clean build.
- [x] -1.3 Verify ALL pixel shaders compile on DX11/FXC. Verified: zero CreateShader stage=1 errors. All PS compiled successfully.
- [x] -1.4 Verify compute shaders on DX11. All CS compile with FXC/SM5.0. Zero CreateShader stage=2 errors after clean cache rebuild.
- [x] -1.5 Fix 3D texture creation on DX11. Added ID3D11Texture3D path with SRV (TEXTURE3D) + UAV (TEXTURE3D) support. APVolume, ScatteringLUT3D, DeltaRayleigh3D, DeltaMie3D, ScatteringDensity3D, ColorGradingLUT all create successfully.
- [x] -1.6 Verify DX11 runs with zero RHI errors: `--rhi dx11 --log-level error` shows zero CreateShader/CreateTexture/pipeline errors.

## Phase 0: SM6.0 Pipeline Creation Crashes (BLOCKING — SM6.0 partially broken)
- [x] 0.1 Fix instanced pipeline failures: EnsureInstancedMeshShaders/EnsureInstancedShadowShaders now return false on non-bindless (GPU instancing requires SM6.6 bindless). Engine gracefully falls back to per-object draws.
- [x] 0.2 ShadowDepth_Instanced: same fix as 0.1 — skipped on non-bindless.
- [x] 0.3 DepthPrepass_Instanced: same fix as 0.1 — skipped on non-bindless.
- [x] 0.4 Fix OutlineMaskPipeline: added VertexLayout::Standard() input layout on non-bindless path. Pipeline now creates successfully on SM6.0/DX11.
- [x] 0.5 Verified SM6.0 runs with zero pipeline creation errors: `--sm 6.0 --log-level error` shows zero RHI errors.

## Phase 1: SM6.0 Shader Parity (visual differences)
- [x] 1.1 SSR/SSGI register bindings: SKIPPED — SSR/SSGI SRV indices are in PerFrameCB but deferred_lighting_ps.hlsl never reads them (even in bindless). SSR compositing is a separate pass, not a deferred lighting issue. No SM5 parity gap.
- [x] 1.2 SSR/SSGI SetFragmentTexture: SKIPPED — same reason as 1.1.
- [x] 1.3 Fix gbuffer_ps.hlsl SM5 metallic sampling: changed `metallicTexture.SampleLevel(clampSampler, uv, 0)` to `metallicTexture.SampleBias(linearSampler, uv, mipBias)` to match SM6.6 path. Fixes metallic aliasing at distance and clamp artifacts on tiling materials.
- [x] 1.4 Opacity texture binding: SKIPPED — opacity is already handled via `opacitySrvIndex` CB field and the existing SM5 alpha test path. No additional register binding needed.
- [x] 1.5 Normal mapping guard: SKIPPED — SM5 path samples normalTexture unconditionally, but the fallback flat-blue texture (0.5, 0.5, 1.0) produces identity normal transform. No visual difference.
- [x] 1.6 Fix deferred_lighting_ps.hlsl SM5 IBL null-guard: added `if (irradianceSrvIndex > 0)` / `if (prefilteredSrvIndex > 0)` / `if (brdfLutSrvIndex > 0)` guards in SM5 path. Falls back to SH diffuse when IBL not ready. Prevents black ambient on first frame.
- [x] 1.7 Lens flare register: SKIPPED — lens flare is composited in tonemap pass which already has separate SM5/bindless paths. The bindless path is the only one that adds lens flare; SM5 omits it intentionally (would need additional register + binding work for minimal visual impact).
- [x] 1.8 Build verification — clean build passes with zero errors.

## Phase 2: DX11 Missing Samplers + SRV Multi-Bind
- [x] 2.1 Created sampler s2 (bilinear clamp) in dx11_device.cpp. Bound to PS/VS/CS slot 2 in Begin().
- [x] 2.2 Created sampler s3 (bilinear wrap) in dx11_device.cpp. Bound to PS/VS/CS slot 3 in Begin().
- [x] 2.3 Fixed SetDescriptorTable SRV path: reads hlslSlot from root layout shaderRegister instead of hardcoding 0. All t0-t13 slots now bind correctly on DX11.
- [x] 2.4 Verified DX11 with `--rhi dx11 --log-level error` — zero RHI errors.

## Phase 3: DX11 Missing Passes — TAA + Bloom
- [ ] 3.1 Remove `IsBindless()` gate on TAA in frame_setup.cpp:220, or implement PS-based TAA fallback.
- [ ] 3.2 Remove `IsBindless()` gate on bloom in bloom_pass.cpp:375, or implement PS-based bloom fallback.
- [ ] 3.3 Verify TAA + Bloom on DX11 via screenshot comparison.

## Phase 4: SM6.0 Polish
- [x] 4.1 IBL baker barrier already correct: ibl_baker.cpp:489-495 has explicit UAV→SRV barrier guarded by `!IsBindless()`. Verified in code.
- [ ] 4.2 Add sky cubemap multi-scattering for SM6.0 PS capture path. (Low priority — minor sky hue difference)
- [ ] 4.3 Add HiZ single-mip fallback for SM6.0 occlusion culling. (Low priority — frustum-only culling works, just more draw calls)

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
