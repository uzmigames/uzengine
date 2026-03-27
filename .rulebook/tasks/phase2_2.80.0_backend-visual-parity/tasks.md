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
- [x] 1.3 Fix gbuffer_ps.hlsl SM5 metallic sampling: changed `metallicTexture.SampleLevel(clampSampler, uv, 0)` to `metallicTexture.SampleBias(linearSampler, uv, mipBias)` to match SM6.6 path.
- [x] 1.4 Opacity texture binding: SKIPPED — opacity is already handled via `opacitySrvIndex` CB field and the existing SM5 alpha test path.
- [x] 1.5 Normal mapping guard: SKIPPED — SM5 path samples normalTexture unconditionally, but the fallback flat-blue texture produces identity normal transform.
- [x] 1.6 Fix deferred_lighting_ps.hlsl SM5 IBL null-guard: added `if (irradianceSrvIndex > 0)` / `if (prefilteredSrvIndex > 0)` / `if (brdfLutSrvIndex > 0)` guards in SM5 path.
- [x] 1.7 Lens flare register: SKIPPED — bindless-only, minimal visual impact.
- [x] 1.8 Build verification — clean build passes with zero errors.

## Phase 2: DX11 Missing Samplers + SRV Multi-Bind
- [x] 2.1 Created sampler s2 (bilinear clamp) in dx11_device.cpp. Bound to PS/VS/CS slot 2 in Begin().
- [x] 2.2 Created sampler s3 (bilinear wrap) in dx11_device.cpp. Bound to PS/VS/CS slot 3 in Begin().
- [x] 2.3 Fixed SetDescriptorTable SRV path: reads hlslSlot from root layout shaderRegister instead of hardcoding 0. All t0-t13 slots now bind correctly on DX11.
- [x] 2.4 Verified DX11 with `--rhi dx11 --log-level error` — zero RHI errors.

## Phase 3: DX11/SM6.0 Missing Passes — TAA + Bloom
- [x] 3.1 TAA: removed IsBindless() gate. Added non-bindless texture binds for t0-t3.
- [x] 3.2 Bloom: removed IsBindless() early-exit. Added SetDescriptorTable(5/4) per dispatch.
- [ ] 3.3 Verify TAA + Bloom on all backends via screenshot comparison.

## Phase 4: SM6.0 Descriptor Management (ROOT CAUSE of black sky + orange tint)
- [x] 4.1 Replaced shared 16-descriptor staging block with per-frame ring allocator (4096 desc/frame × 3 frames). Eliminates cross-frame GPU race condition.
- [x] 4.2 Added `InvalidateDescriptorBlock()` API for per-draw descriptor isolation. GBuffer pass calls it before each draw's texture binding.
- [x] 4.3 Fixed tonemap t1 stale descriptor: bind bloom result (or black fallback) at t1 on non-bindless. Stale transmittance LUT was being read as bloom.
- [x] 4.4 Fixed text renderer font atlas binding: use `m_bindless` flag, not `SetDescriptorTable(4)` which targeted UAV table.
- [x] 4.5 Set `multiScatteringFactor=0` on non-bindless (LUT not precomputed on SM6.0).
- [x] 4.6 Skip SkyCubemapCapturePS pipeline on bindless (register(t0) conflicts with DIRECTLY_INDEXED root sig).
- [x] 4.7 IBL baker barrier already correct: ibl_baker.cpp:489-495 verified.

## Phase 5: Vulkan Parity
- [x] 5.1 Fix CopyBufferToTexture division by zero on BC compressed textures. BytesPerTexel returns 0 for BC1-BC7; now computes from block size.
- [x] 5.2 Add immutable samplers s2 (bilinear clamp) + s3 (bilinear wrap) to Vulkan descriptor set layout. Missing s2 caused black IBL ambient.
- [ ] 5.3 Verify Vulkan visual output matches DX12 SM6.8 reference.

## Phase 6: SM6.0 Polish (Low Priority)
- [ ] 6.1 Add sky cubemap multi-scattering for SM6.0 PS capture path. (Minor sky hue difference)
- [ ] 6.2 Add HiZ single-mip fallback for SM6.0 occlusion culling. (Performance only)

## 7. Visual Verification
- [ ] 7.1 Screenshot all 4 backends with pbr_showcase at same camera position.
- [ ] 7.2 Compare all 4 — verify identical visual output (PSNR > 35 dB).
- [ ] 7.3 Test with fbx_test scene (mushroom monster).
- [ ] 7.4 Test edge cases: masked material, metallic tiling, IBL on first frame.

## 8. Testing
- [x] 8.1 Integration test: all 4 backends render frames with zero RHI errors.
- [ ] 8.2 Verify 95%+ coverage on all new/modified code.

## 9. Documentation
- [x] 9.1 CHANGELOG.md v0.2.56 updated with all 24 fixes across DX11/SM6.0/Vulkan/TAA/Bloom/CLI.
- [x] 9.2 Research docs created: `docs/analysis/dx12-descriptors/` (UE5 + Godot patterns, 6 docs), `docs/analysis/backend-parity/` (2 docs).
- [ ] 9.3 Update docs/guides/getting-started.md with backend selection flags.
