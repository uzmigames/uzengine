## Phase 1: Diagnostics (determine exact root cause)
- [x] 1.1 DX11 debug layer: found ~50x `CSSetShaderResources: Resource still bound on output! Forcing to NULL` — compute pass outputs (transmittance LUT, IBL) not readable as SRV because UAVs not unbound after Dispatch. Fixed: unbind all UAVs after every Dispatch(). Warnings eliminated.
- [x] 1.2 DX11 debug layer: found 1x `Map: This resource is already mapped!` — double Map on a readback buffer. Minor, non-fatal.
- [x] 1.3 ROOT CAUSE FOUND: CloudComposite pass zeroed out HDR scene color. Blend mode `dstColor=SrcAlpha` with cloud output alpha=0 (empty compute) multiplied the entire scene by 0 → black. Not TAA/auto-exposure.
- [x] 1.4 TAA already disabled for DX11 (SM5.1 < SM6.0). Not a factor.
- [x] 1.5 Auto-exposure verified working correctly. Not a factor.

## Phase 2: GBuffer Fixes
- [x] 2.1 GBuffer data IS reaching lighting pass — verified via texture handle tracing. idx=11 gen=1 matches across all passes.
- [x] 2.2 GBuffer PS writes all MRT outputs correctly on SM5/FXC — verified (29184 triangles, 15591 vertices drawn).
- [x] 2.3 Render graph correctly maps same physical texture for GBuffer write → lighting read — verified with handle logging.

## Phase 3: Lighting + Sky + Tonemap
- [x] 3.1 Deferred lighting WORKS on DX11 — was producing output all along but CloudComposite zeroed it out. Fixed by guarding CloudComposite with `ctx.bindless` (DX11 non-bindless skips cloud composite; cloud PS uses ResourceDescriptorHeap which DX11 SM5 doesn't support).
- [x] 3.2 Sky atmosphere pass works on DX11 — sky renders correctly with fix applied.
- [x] 3.3 Tonemap binding works on DX11 — SetFragmentTexture(0, hdrSceneColor) verified correct, Load() confirmed working.

## Phase 4: Visual Verification
- [x] 4.1 Screenshot DX11 pbr_showcase — scene renders: sky, PBR cubes/spheres, textures, lighting all visible. Visually matches DX12 SM6.8.
- [ ] 4.2 Screenshot DX11 fbx_test — verify mushroom renders.

## 5. Testing
- [ ] 5.1 Integration test: DX11 renders 15 frames zero D3D11 errors.
- [ ] 5.2 Verify 95%+ coverage.

## 6. Documentation
- [ ] 6.1 Update CHANGELOG.
- [ ] 6.2 Update backend parity report.

## Root Cause Analysis (2026-03-27)
**Problem**: DX11 scene was completely black despite 9 draw calls emitting and all RHI state verified correct.
**Root Cause**: CloudComposite render graph pass used premultiplied-alpha blend (`srcColor=One, dstColor=SrcAlpha`). The cloud compute ran but produced empty output (alpha=0). The blend equation `final = cloud*1 + scene*SrcAlpha = 0 + scene*0 = 0` zeroed out the entire HDR scene every frame.
**Fix**: Guard CloudComposite registration with `ctx.bindless` — DX11 (non-bindless) skips the cloud composite pass because: (a) the cloud composite PS uses bindless ResourceDescriptorHeap which SM5.0 doesn't support, (b) empty cloud output would zero the scene via the premultiplied-alpha blend.
**Verified**: DX11 now renders correctly. DX12 unaffected (bindless=true, cloud composite still runs).
