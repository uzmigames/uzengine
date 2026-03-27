## Phase 1: Diagnostics (determine exact root cause)
- [ ] 1.1 Run DX11 with debug layer (`--rhi dx11 --gpu-validation`): capture D3D11 warnings about SRV-RTV conflicts, dimension mismatches, null bindings.
- [ ] 1.2 Add first-frame diagnostic logging in deferred lighting pass: log texture handle validity on DX11.
- [ ] 1.3 Replace deferred_lighting_ps with flat red output on DX11 to isolate GBuffer vs pipeline issue.
- [ ] 1.4 Verify `m_hdrSceneColor` has valid RTV on DX11.
- [ ] 1.5 Verify tonemap reads same physical texture lighting wrote to.

## Phase 2: GBuffer Fixes
- [ ] 2.1 Fix GBuffer data not reaching lighting pass (based on Phase 1 findings).
- [ ] 2.2 Verify GBuffer PS writes all 4 MRT outputs on SM5/FXC.
- [ ] 2.3 Verify render graph maps same physical texture for GBuffer write → lighting read.

## Phase 3: Lighting + Sky + Tonemap
- [ ] 3.1 Fix deferred lighting to produce non-zero output on DX11.
- [ ] 3.2 Fix sky atmosphere pass on DX11.
- [ ] 3.3 Fix tonemap binding on DX11.

## Phase 4: Visual Verification
- [ ] 4.1 Screenshot DX11 pbr_showcase — verify matches DX12 SM6.8.
- [ ] 4.2 Screenshot DX11 fbx_test — verify mushroom renders.

## 5. Testing
- [ ] 5.1 Integration test: DX11 renders 15 frames zero D3D11 errors.
- [ ] 5.2 Verify 95%+ coverage.

## 6. Documentation
- [ ] 6.1 Update CHANGELOG.
- [ ] 6.2 Update backend parity report.

## Investigation Summary (RHI layer verified correct)
- SetFragmentTexture → PSSetShaderResources(slot) — correct
- BeginRenderPass → clears SRVs, binds RTVs — correct
- EndRenderPass → unbinds RTVs — correct
- CB binding → b0/b1/b2/b4 mapping — correct
- Depth SRV → R32_TYPELESS + R32_FLOAT SRV — correct
- Root causes: GBuffer content zero, HDR RT missing, or FXC shader diff
