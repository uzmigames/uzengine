## 1. Render Graph First-Frame States (frame_setup.cpp)
- [x] 1.1 Import HDR_SceneColor as Common on frameIndex==0, RenderTarget on subsequent frames.
- [x] 1.2 Import GBuffer textures (Albedo, Normal, Depth, Position, Emissive, Velocity) as Common on frameIndex==0.
- [x] 1.3 Import ShadowAtlas as Common on frameIndex==0, ShaderResource on subsequent frames. Registry supersedes the frame-index claim: `frame_setup.cpp:495` imports tracked; `shadow_atlas.cpp:31` creates a depth-stencil texture, so its correct creation state is DepthStencilWrite, NOT Common (`dx12_texture_creation.cpp:166-167`). Later frames use the actual last state.
- [x] 1.4 Import DBuffer textures as Common on frameIndex==0. Already tracked in `frame_setup.cpp:508-510`; `RgStateRegistry::CreationState` (`render_graph_states.cpp:31`) supplies Common until first use, including decals enabled after frame zero.
- [x] 1.5 Verify GBuffer_Depth uses DepthStencilWrite (not ShaderResource) on frame 1+ since GBuffer pass clears+writes first. `gbuffer_pass.cpp:48` declares DepthStencilWrite before clear/write; `frame_setup.cpp:491` imports the actual previous state. ShaderResource is a valid import state after lighting; the graph transitions it to DepthStencilWrite before GBuffer, rather than falsely claiming it is already writable.

## 2. IBL BRDF LUT Bake (ibl_baker.cpp)
- [x] 2.1 Add explicit Common→UAV barrier before compute dispatch in BakeBRDFLut(). Already present in `ibl_baker.cpp:333-342`; no duplicate barrier added.
- [x] 2.2 Add UAV→ShaderResource barrier after dispatch (already exists — verify it works). Existing `ibl_baker.cpp:376-382`; headful GBV log `uzengine_2026-10-14_14-16-22.log` reports successful bake at 14:16:23.372 without a BRDF resource-state error.
- [x] 2.3 Log error + return early if bake pipeline (m_brdfLutPipeline) is invalid. `EnsureBRDFLutPipeline` checks shader and pipeline validity and logs failures (`ibl_baker.cpp:155-193`); `BakeBRDFLut` returns before texture creation/dispatch on failure (`:293-295`).
- [x] 2.4 Verify BRDF LUT bake command list executes successfully (check HRESULT from Execute). Bake completes its fence wait and logs success in the same GBV run at 14:16:23.372. `IRHICommandList::Execute` and D3D12 `ExecuteCommandLists` return void, NOT HRESULT; `DX12CommandList::End` checks Close's HRESULT (`dx12_command_list.cpp:233-238`) and Execute drains debug messages (`:284-320`). No interface change justified.

## 3. Atmosphere Precompute (atmosphere_renderer.cpp)
- [x] 3.1 Import TransmittanceLUT as Common on first precompute, ShaderResource on subsequent.
- [x] 3.2 Import MultiScatterLUT as Common on first precompute.
- [x] 3.3 Import Delta Rayleigh/Mie/Density/Irradiance 3D textures as Common on first precompute.
- [x] 3.4 Import Aerial Perspective Volume as Common on first dispatch.
- [x] 3.5 Import Sky Cubemap as Common on first capture.
- [x] 3.6 Verify Bruneton scattering precompute LUT barriers (inline dispatch on init command list). Superseded: shaders removed and all Bruneton dispatches compiled out under `#if 0` (`atmosphere_renderer.cpp:387-678`). No active inline init precompute exists to fix.
- [x] 3.7 Add Common→UAV barriers before first-frame inline atmosphere compute dispatches. Active transmittance/AP/cubemap computes are graph passes with tracked imports (`atmosphere_renderer.cpp:98,243,690`), not inline init dispatches. Their UAV writes make the graph emit the creation-state transition (`transmittance_lut_pass.cpp:49`); adding a raw Common barrier would duplicate or contradict that transition.

## 4. VFX Barriers (frame_vfx_inline.cpp, gpu_particle_buffer.cpp)
- [x] 4.1 Add UAV→CopyDst barrier before aliveCount per-frame reset CopyBuffer.
- [x] 4.2 Keep CopyDst→UAV barrier after aliveCount reset CopyBuffer.
- [x] 4.3 Store VFX_Zero_Staging buffer handle for deferred destruction (was use-after-free).
- [x] 4.4 Add zeroStagingBuffer to DestroyStaging() cleanup.

## 5. Testing & Verification
- [ ] 5.1 Run vfx_viewport_app with DX12 debug layer -> zero D3D12 errors. **BLOCKED (toolchain, not crash-fix)**: demo build fails — PATH Zig 0.16 breaks build_common.zig:201/439 (0.15.2-only); vendored 0.15.2 then fails link `__hash_memory` (runtime.lib built with 0.16 libcxx vs 0.15.2 demo). Needs a consistent-toolchain rebuild (separate build-system task). Crash fix verified on the main uzengine binary instead (GBV 120f, zero barrier errors). See verification.md.
- [x] 5.2 Run vfx_viewport_app headless for 120 frames — clean exit, no FATAL errors. User-approved main-binary substitute: headful `uzengine.exe --project scripts/rendering/sky-rebake-reference.uzproject --frames 120 --verbose --gpu-validation --screenshot runtime/build/first-frame-state-sky80.png --screenshot-frame 80`, bounded to 180 seconds, exits 0. Log `uzengine_2026-10-14_14-31-56.log` reports screenshot saved at 14:33:30.744 and `Render loop finished: 124 frames` at 14:33:30.900 (120 + four preload frames), zero Close failures/barrier errors/FATAL. This is rendered headful verification, NOT the main binary's device-only headless mode.
- [x] 5.3 Capture screenshot at frame 80 — sky atmosphere + VFX particles visible. User-directed main-binary substitute: `sample/projects/vfx_showcase.uzproject`, `--frames 120 --editor --camera-pos 0,-650,250 --camera-look 0,0,350 --screenshot-frame 80`; exit 0 under a 180-second bound. Log `uzengine_2026-10-14_14-40-56.log` saves `runtime/build/first-frame-state-vfx80.png` at 14:40:57.309 and reports 125 total frames at 14:40:57.699. Image inspected: sky plus a large particle billboard plume. Same-camera `r.Show.Particles=0` comparison (`first-frame-state-vfx80-no-particles.png`, exit 0) removes the plume, verifying it is VFX rather than atmosphere. No shader compile failure.
- [ ] 5.4 Run standalone vfx_viewport demo (regression check). **BLOCKED (same toolchain issue as 5.1)**: see verification.md 5.1/5.4 root cause.

## 6. Documentation
- [x] 6.1 Document first-frame resource state convention in render graph comments. `render_graph_builder.h` documents creation states, actual previous state versus first-access target, and avoiding duplicate compute barriers.
- [x] 6.2 Update atmosphere_renderer.h with first-frame state notes. `AddAtmospherePasses` notes distinguish content flags from states, tracked first-use transitions, raw registry transitions, and inactive Bruneton code.

## 7. Tail (mandatory — enforced by rulebook v5.3.0)
- [x] 7.1 Update or create documentation covering the implementation. `verification.md` records the registry audit, exact failing debug-layer lines, mixed FrameContext field mapping, coherent rebuild, and remaining demo/initialization blockers.
- [x] 7.2 Write tests covering the new behavior. Added `RenderGraphStates.ShadowAndGBufferDepthTransitionFromActualPreviousState` and `RenderGraphStates.DBufferFirstUseAfterSkippedFramesStartsInCommon` beside existing creation-state and compute-volume tests.
- [x] 7.3 Run tests and confirm they pass. Requested `GTEST_FILTER="-RuntimeViewportTest.*" zig build test -Doptimize=ReleaseFast --summary all`: 7675 tests, 7655 passed, 17 skipped, ONLY the three pre-existing VfxEditorSlate failures; exit 1 reflects those failures. Comparable full-path run from repo root (`E:/UzmiEngine/uzengine/runtime/build/bin/test_runtime.exe --gtest_filter="-RuntimeViewportTest.*"`): 7659 passed, 13 skipped, the SAME three failures (WorkbenchHasTwoColumns, TitleBarReflectsEmitterName, StatusBarBindingsReflectTelemetry), 30.486 seconds. Delta +2 versus 7657 baseline, both new state tests passed; RenderGraphStates 16/16. Four SplineUzfx sample tests skip only under runtime CWD (`test_spline_data_interface.cpp:72-76` asset-root candidates).

## 8. Main-binary crash acceptance (user-directed substitute for the blocked demos)
- [x] 8.1 Diagnose and remove the actual frame-zero Close failure. Baseline log `uzengine_2026-10-14_14-16-22.log:439-446` shows Close `0x80070057`, RT barriers on depth resources, depth-write barriers on colour resources, and Debug_DirectLight cleared in SRV state; `:459,508` then show `0x80004005`. Existing user additions `scene_renderer_frame.h:122-123` shift IDs by eight bytes: old albedo becomes new depth, old ShadowAtlas becomes new emissive. A coherent rebuild restores matching setup/consumer layouts; no extra barriers or changes to the user's fields. `zig build -Doptimize=ReleaseFast -j8 --summary all`: exit 0, 47/47 steps succeeded, newly linked main binary.
- [x] 8.2 Re-run the SAME bounded headful GBV command. `uzengine.exe --project scripts/rendering/sky-rebake-reference.uzproject --frames 3 --verbose --gpu-validation`: exit 0, no Close failures, no barrier errors (ids 527/538/607/942), log `uzengine_2026-10-14_14-28-42.log:18-19,567`. The loop reports seven total frames including preload; GBV reports eight distinct occurrences of non-barrier initialization error id 1422, documented rather than suppressed.
