## 1. Render Graph First-Frame States (frame_setup.cpp)
- [x] 1.1 Import HDR_SceneColor as Common on frameIndex==0, RenderTarget on subsequent frames.
- [x] 1.2 Import GBuffer textures (Albedo, Normal, Depth, Position, Emissive, Velocity) as Common on frameIndex==0.
- [ ] 1.3 Import ShadowAtlas as Common on frameIndex==0, ShaderResource on subsequent frames.
- [ ] 1.4 Import DBuffer textures as Common on frameIndex==0.
- [ ] 1.5 Verify GBuffer_Depth uses DepthStencilWrite (not ShaderResource) on frame 1+ since GBuffer pass clears+writes first.

## 2. IBL BRDF LUT Bake (ibl_baker.cpp)
- [ ] 2.1 Add explicit Common→UAV barrier before compute dispatch in BakeBRDFLut().
- [ ] 2.2 Add UAV→ShaderResource barrier after dispatch (already exists — verify it works).
- [ ] 2.3 Log error + return early if bake pipeline (m_brdfLutPipeline) is invalid.
- [ ] 2.4 Verify BRDF LUT bake command list executes successfully (check HRESULT from Execute).

## 3. Atmosphere Precompute (atmosphere_renderer.cpp)
- [x] 3.1 Import TransmittanceLUT as Common on first precompute, ShaderResource on subsequent.
- [x] 3.2 Import MultiScatterLUT as Common on first precompute.
- [x] 3.3 Import Delta Rayleigh/Mie/Density/Irradiance 3D textures as Common on first precompute.
- [x] 3.4 Import Aerial Perspective Volume as Common on first dispatch.
- [x] 3.5 Import Sky Cubemap as Common on first capture.
- [ ] 3.6 Verify Bruneton scattering precompute LUT barriers (inline dispatch on init command list).
- [ ] 3.7 Add Common→UAV barriers before first-frame inline atmosphere compute dispatches.

## 4. VFX Barriers (frame_vfx_inline.cpp, gpu_particle_buffer.cpp)
- [x] 4.1 Add UAV→CopyDst barrier before aliveCount per-frame reset CopyBuffer.
- [x] 4.2 Keep CopyDst→UAV barrier after aliveCount reset CopyBuffer.
- [x] 4.3 Store VFX_Zero_Staging buffer handle for deferred destruction (was use-after-free).
- [x] 4.4 Add zeroStagingBuffer to DestroyStaging() cleanup.

## 5. Testing & Verification
- [ ] 5.1 Run vfx_viewport_app with DX12 debug layer enabled — zero D3D12 Debug errors.
- [ ] 5.2 Run vfx_viewport_app headless for 120 frames — clean exit, no FATAL errors.
- [ ] 5.3 Capture screenshot at frame 80 — sky atmosphere + VFX particles visible.
- [ ] 5.4 Run standalone vfx_viewport demo — still works correctly (regression check).

## 6. Documentation
- [ ] 6.1 Document first-frame resource state convention in render graph comments.
- [ ] 6.2 Update atmosphere_renderer.h with first-frame state notes.

## 7. Tail (mandatory — enforced by rulebook v5.3.0)
- [ ] 7.1 Update or create documentation covering the implementation
- [ ] 7.2 Write tests covering the new behavior
- [ ] 7.3 Run tests and confirm they pass
