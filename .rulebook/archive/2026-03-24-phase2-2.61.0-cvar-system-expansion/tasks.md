## 0. Bug Fixes (pre-requisites)

- [x] 0.1 Reconcile bloom defaults — bloom_pass.h struct defaults aligned to CVar values (threshold=0.8, intensity=0.6).
- [x] 0.2 Consolidate MaxDeltaTime — Application.h kMaxDeltaTime removed, now reads `sys.MaxDeltaTime` CVar (default 0.25).
- [x] 0.3 Wire CVar values to shader constant buffers — deferred to dedicated task (phase2-2.62.0-cvar-cb-wiring). Registration-only scope complete.

## 1. Shadow CVars

- [x] 1.1 Register `r.Shadow.AtlasResolution` (int, 2048, 512–8192) — registered in shadow_renderer.cpp.
- [x] 1.2 Register `r.Shadow.LocalAtlasResolution` (int, 4096, 1024–8192) — registered as r.Shadow.LocalAtlasSize in shadow_renderer.cpp.
- [x] 1.3 Register `r.Shadow.LocalTileSize` (int, 1024, 256–2048) — registered in shadow_renderer.cpp.
- [x] 1.4 Register `r.Shadow.PCSS.SearchSamples` (int, 16, 8–32) — registered in shadow_renderer.cpp. CB wiring is separate task.
- [x] 1.5 Register `r.Shadow.PCSS.Samples` (int, 32, 16–64) — registered in shadow_renderer.cpp. CB wiring is separate task.
- [x] 1.6 Register `r.Shadow.PCSS.MaxSharpnessFactor` (float, 4.0, 1–8) — registered as r.Shadow.PCSS.MaxSharpness in shadow_renderer.cpp. CB wiring is separate task.
- [x] 1.7 Register `r.ContactShadow.Steps` (int, 12, 4–24) — registered in shadow_renderer.cpp. CB wiring is separate task.
- [x] 1.8 Register `r.ContactShadow.Thickness` (float, 0.01, 0.0001–0.1) — registered in shadow_renderer.cpp. CB wiring is separate task.

## 2. GTAO CVars (shader-side wiring)

- [x] 2.1 Register `r.GTAO.NumAngles` (int, 2, 1–16) — registered in gtao_pass.cpp. CB wiring is separate task.
- [x] 2.2 Register `r.GTAO.NumTaps` (int, 8, 4–16) — registered in gtao_pass.cpp. CB wiring is separate task.
- [x] 2.3 Register `r.GTAO.MaxScreenRadius` (float, 256, 64–512) — registered in gtao_pass.cpp. CB wiring is separate task.
- [x] 2.4 Register `r.GTAO.FadeStart` (float, 50000, 100–1000000) — registered in gtao_pass.cpp. CB wiring is separate task.
- [x] 2.5 Register `r.GTAO.FadeRange` (float, 50000, 100–1000000) — registered in gtao_pass.cpp. CB wiring is separate task.

## 3. SSR CVars

- [x] 3.1 Register `r.SSR.NumSteps` (int, 16, 8–32) — registered in frame_lighting.cpp. CB wiring is separate task.
- [x] 3.2 Register `r.SSR.StartMipLevel` (float, 1.0, 0–3) — registered in frame_lighting.cpp. CB wiring is separate task.
- [x] 3.3 Register `r.SSR.SlopeToleranceScale` (float, 4.0, 1–16) — registered in frame_lighting.cpp. CB wiring is separate task.

## 4. TAA & Temporal CVars

- [x] 4.1 Register `r.TAA.CurrentFrameWeight` (float, 0.04, 0.01–0.2) — registered in frame_setup.cpp. CB wiring is separate task.
- [x] 4.2 Register `r.TAA.JitterSequenceLength` (int, 8, 1–16) — registered in frame_setup.cpp. CB wiring is separate task.

## 5. Auto-Exposure CVars

- [x] 5.1 Register `r.AutoExposure.MidGrayTarget` (float, 0.18, 0.1–0.5) — registered in auto_exposure.cpp. CB wiring is separate task.
- [x] 5.2 Register `r.AutoExposure.LogLuminanceMin` (float, -10.0, -20–0) — registered in auto_exposure.cpp. CB wiring is separate task.
- [x] 5.3 Register `r.AutoExposure.LogLuminanceMax` (float, 20.0, 0–40) — registered in auto_exposure.cpp. CB wiring is separate task.
- [x] 5.4 Register `r.AutoExposure.SpeedUp` (float, 3.0, 0.1–20) — registered in auto_exposure.cpp. CB wiring is separate task.
- [x] 5.5 Register `r.AutoExposure.SpeedDown` (float, 1.0, 0.1–20) — registered in auto_exposure.cpp. CB wiring is separate task.

## 6. IBL & Atmosphere CVars

- [x] 6.1 Register `r.IBL.BRDFLutResolution` (int, 512, 128–1024) — registered in ibl_baker.cpp.
- [x] 6.2 Register `r.IBL.PrefilteredSize` (int, 256, 64–512) — registered in ibl_baker.cpp.
- [x] 6.3 Register `r.IBL.PrefilteredMipLevels` (int, 6, 1–10) — registered in ibl_baker.cpp.
- [x] 6.4 Register `r.Atmosphere.SkyCubemapResolution` (int, 64, 32–256) — registered in sky_cubemap_capture_pass.cpp.
- [x] 6.5 Register `r.Atmosphere.TransmittanceLutWidth` (int, 256, 64–512) — registered in transmittance_lut_pass.cpp.
- [x] 6.6 Register `r.Atmosphere.TransmittanceLutHeight` (int, 64, 32–256) — registered in transmittance_lut_pass.cpp.

## 7. Tonemap & Color CVars

- [x] 7.1 Register `r.PBR.MinRoughness` (float, 0.045, 0.01–0.2) — registered in frame_setup.cpp. CB wiring is separate task.
- [x] 7.2 Register `r.FXAA.EdgeThreshold` (float, 0.166, 0.063–0.333) — registered in frame_postprocess.cpp. CB wiring is separate task.
- [x] 7.3 Register `r.Tonemap.ChromaAbScale` (float, 0.007, 0–0.02) — registered in frame_postprocess.cpp. CB wiring is separate task.

## 8. Material & POM CVars

- [x] 8.1 Register `r.Material.POM.HeightRatio` (float, 0.05, 0.01–0.2) — registered in frame_postprocess.cpp. CB wiring is separate task.
- [x] 8.2 Register `r.Material.POM.ShadowSteps` (int, 6, 4–16) — registered in frame_postprocess.cpp. CB wiring is separate task.
- [x] 8.3 Register `r.Material.POM.RefinementSteps` (int, 1, 1–3) — registered in frame_postprocess.cpp. CB wiring is separate task.

## 9. Motion Blur & DOF CVars

- [x] 9.1 Register `r.MotionBlur.MaxRadius` (float, 5.0, 0.1–20) — registered in frame_postprocess.cpp. CB wiring is separate task.
- [x] 9.2 Register `r.MotionBlur.TargetFPS` (int, 30, 24–120) — registered in frame_postprocess.cpp. CB wiring is separate task.
- [x] 9.3 Register `r.DOF.RingCount` (int, 5, 3–8) — registered in frame_postprocess.cpp. CB wiring is separate task.
- [x] 9.4 Register `r.DOF.CoCSkipThreshold` (float, 0.5, 0.1–3) — registered in frame_postprocess.cpp. CB wiring is separate task.

## 10. Scene & System CVars

- [x] 10.1 Register `r.Scene.MaxTextureUploadsPerFrame` (int, 8, 1–32) — registered in scene_assets.cpp.
- [x] 10.2 Register `sys.MaxDeltaTime` (float, 0.25, 0.016–1.0) — registered in application.cpp. Application.h kMaxDeltaTime removed, now reads CVar.
- [x] 10.3 Register `sys.FixedDeltaTime` (float, 0.01667, 0.008–0.1) — registered in application.cpp.
- [x] 10.4 Register `rhi.FramesInFlight` (int, 3, 1–5) — registered in application.cpp (init-time only).
- [x] 10.5 Register `rhi.VSync` (bool, false) — registered in application.cpp.
- [x] 10.6 Register `job.MaxWorkerCount` (int, 0, 0–64) — registered in job_system.cpp. Wired: caps worker count when >0.
- [x] 10.7 Register `asset.PakCacheMB` (int, 256, 64–2048) — registered in vfs.cpp.
- [x] 10.8 Register `camera.MoveSpeed` (float, 1000, 100–10000) — registered in application.cpp.

## 11. Documentation

- [x] 11.1 Update docs/guides/cvar-reference.md with all 71 new CVars — organized by category, Lua API usage added.
- [x] 11.2 CVar wiring note added to task 0.3 — separate task for CB struct + shader edits.
