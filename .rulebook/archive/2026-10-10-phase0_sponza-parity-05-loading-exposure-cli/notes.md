# Notes — phase0_sponza-parity-05-loading-exposure-cli

All times local, 2026-09-08. Exe timestamps: 10:49 (batch A), 11:23 (batch B), 11:30 (batch C).

## What was ported

**Readiness** — `m_budgetDeferredMeshCount` (`scene_renderer_impl.h`), reset at the top of `SyncWorldToDrawList` and `++` at both budget early-outs (`scene_sync.cpp` base-mesh `continue`, LOD `break`); `IsSceneFullyLoaded()` (`scene_mesh_assets.cpp`) early-outs on that counter, `m_deferredMeshUploads`, `m_pendingSdfBakes`, `m_asyncPendingSlots`; `IsSceneFullyLoadedDebug()` logs the four.

**Preload without present** — `uzengine.cpp`: title `<title> - Loading scene...` (`PlatformWindow::SetTitle` already existed at `platform_window.cpp:450`), pump/render/drain/`WaitIdle` loop until ready, then `RenderFrame(dt=0)` → `ResetExposureHistory` → `RenderFrame(dt=0)`; 120-frame readiness bypass removed from the screenshot trigger. `application.cpp` / `application_platform.cpp`: the ST diff against ours is include-only — nothing to port; that path keeps its own trigger (no timeout).

**Exposure** — `AutoExposure::ResetHistory(IRHICommandList*)` (`auto_exposure.{h,cpp}`, `CopyDst` usage on the exposure buffer, UAV→CopyDst copy from `m_zeroBuffer`→UAV), `SceneRenderer::ResetExposureHistory` (`scene_render_frame.cpp`, include added). Shader untouched: `exposure_average_cs.hlsl` already re-initialises when `prevExposure < 1e-10`.

**Staging lifetime** — `scene_texture_assets.cpp`: `TakeStagingBuffers()` → `QueueBufferDestroy()` instead of `ReleaseStaging()` after the GPU-side `GraphicsWaitForCopy()`.

**DX12/RHI** — `CopyTextureToBuffer` copies a `D3D12_BOX` of the first Z slice for `is3D` textures (null box kept for 2D/depth); `enableBreadcrumbs` plumbed `RHIConfig` → `RHIDeviceDesc` → `DX12Device::Init`; `EnableDRED()` moved **before** `CreateFactory`/`CreateDevice` (DRED settings latch at device creation); `EnableDebugLayer`/`EnableDRED` obtain their interfaces from `m_deviceFactory->GetConfigurationInterface` when the Agility device factory is active; `LogDeviceRemoved` reports every branch at Error level (default log level hides Warning exactly when a TDR needs diagnosing); `<cstddef>` in `rhi_types.h`. Not ported on purpose: ST's `UZ_LOG` leak report (ours is `fprintf` + `fflush` so it survives the D3D12MA abort — keep), `dx12_fence.cpp` MinGW comment, Metal hunks, ST's stencil-less `rhi_types.h` (ours is newer).

**CLI** — 4.1b: `Config::ApplyCLIOverrides` erases an identical pending `(name, value)` before re-queueing, so each flush now shows `1, 0` (project default, CLI) instead of `1, 0, 0`; last-wins preserved for `--cvar X=1 --cvar X=0`.

**Scripts** — `test_scene_readiness.ps1` (14/14), `test_exposure_reset.ps1` (4/4), `verify_sponza_loading.ps1`, `compare_sponza_stationary.ps1`.

**Lua** — `uz.camera.get_forward()` (`lua_camera_api.cpp`; look-at target when set, else `rotation * Vec3::Forward()`, the same rule as `frame_setup.cpp:282-286`). `fly_camera.lua` `on_start` now seeds `cam_x/y/z` from `uz.camera.get_position()` and yaw/pitch from `get_forward()` (Lua 5.1/5.4-compatible `atan2` shim).

## Evidence

| Check | Result |
|---|---|
| Readiness order (`--screenshot`, no `--screenshot-frame`) | last mesh 10:49:59.533 → `Pre-load complete … after 37 frames` 10:50:01.125 → `Screenshot saved` 10:50:03.021; 138 meshes, 138 bakes (last bake 10:50:00.938) |
| First presented frame vs frame 120, Camera A, no Controller (mean/255, share <16) | 51.6 / 37.8 % → 51.7 / 37.0 % — SponzaTest exe (Sep 7 13:38): 52.7 / 38.5 % → 52.8 / 37.7 % |
| Same with the `Controller` entity after the fly-cam fix | 51.6 → 51.7 (before the fix: 221.1 for ~40 frames, then 36.9) |
| `--cvar r.RadianceCache=0` on the QA project | flush `1, 0` per phase (was `1, 0, 0`); `SDF GI resources ready but r.RadianceCache=0 — disabled`; 0 bakes |
| `--gpu-breadcrumbs` | before batch C: `DRED not available on this system, hr=0x887E0003` (D3D12_ERROR_INVALID_REDIST — system d3d12.dll asked while the device comes from the Agility factory); after: `DRED enabled (auto-breadcrumbs + page fault tracking)` before `D3D12 device created … (via Agility SDK factory)` |
| `verify_sponza_loading.ps1` | startup / converged / camera-c: exit 0, empty stderr, one `Pre-load complete` each; converged DIAG `hasFailure=false`, 31 targets, frame 678; no `CommandAllocator::Reset`/`Close`/0x80004005 lines |
| `compare_sponza_stationary.ps1 -Prefix sponza-loading-fixed` | whole image 53.34 → 53.42 (+0.15 %), mean abs RGB 3.65/255; sunlit floor −0.46 %, shaded floor +2.33 %, shaded column +20.92 %, left wall +3.82 % |
| `--diag` 3D targets | `gi.gdf_cascade0` / `gi.gdf_raw_cascade0` still read 0 **in both repos** (SponzaTest exe too) — the box hunk is correct but not sufficient. **Resolved 2026-09-08 by `phase0_sponza-followup-03`**: the pass inspector skipped every 3D target outright; it now reads the centre Z slice (`gi.gdf_raw_cascade0` min +121.88 / max +165.88). |

## Investigation log (the white flash)

1. First measurement after batch A: `--screenshot-frame 2/5/20` all 221.1 mean, 0 % blacks; frame 60 → 35.8; frame 120 → 36.9. `sponza-05-preload.png` (no frame arg) also 221.1.
2. Code diff vs SponzaTest for `auto_exposure.{h,cpp}`, both exposure shaders, `tonemap_ps.hlsl`, `frame_postprocess.cpp` exposure feed, the preload block: identical (only my comments). So the code was not the difference.
3. `frameIndex` continues after the preload (main loop starts at 37), so every `--screenshot-frame ≤ 37` captured the **same** first presented frame; "frames 2–30 identical, step at 40" was the first vs second presented frame.
4. Auto exposure off: first presented frame 43.0 mean, second onward 1.2 mean (99.5 % blacks). The HDR itself collapsed by ~35× between the first and second presented frame.
5. Same run without the `Controller` entity (temporary scene/project, deleted afterwards): 8.0 AE-off stable; AE-on 51.6 → 51.7, matching SponzaTest. So the culprit was `fly_camera.lua`, not the port.
6. Images: the first presented frame with the Controller was bright (43.0) and the second dark (1.2). **Corrected 2026-09-08 by `phase0_sponza-followup-02`:** that first frame was `Camera_A`'s own view, not an unshadowed corridor — a deterministic teleport repro measures `Camera_A` at 42.8 and the corridor at 1.2 from the jump frame onward, with no shadow lag in either direction. The old script set the pose in `on_update`, so the scene camera survived exactly one frame. The flash was purely exposure seeded for the bright atrium. The script's first `on_update` teleported the camera from `Camera_A` (86, 881, 159) to (0, −250, 130), and the exposure seeded for `Camera_A` (gain ≈ 6.5×) applied to that ~35× darker view is what read as a white flash for ~40 frames.
7. Consequence for the earlier tasks: every "Camera A" capture taken in this session with the `Controller` present (`sponza-01/02/03-*.png`, `tog-*.png`, `sponza-comparacao.*`) was at the script's corridor pose, not at `Camera_A`. Camera C captures (QA scene, no Controller) were valid. Task 06's final comparison must be recaptured at the true `Camera_A` — automatic now that the script starts there.
8. Fix chosen: make the script start from the active camera (API addition `get_forward`) rather than removing the Controller (the user re-added it) or hard-coding `Camera_A` (scene-specific). (The "one-frame cascade lag" this note originally suspected does not exist — `phase0_sponza-followup-02` measured the jump frame as identical to the settled frames in both directions.)

## Lessons

- `--screenshot-frame` is absolute and the preload consumes ~37 indices: any "early frame" experiment must use K > preload count or read the log's `after N frames`.
- Compare captures with auto exposure **off** first; AE hides a 35× HDR change behind a 40-frame adaptation curve.
- Bash `-c` scripts mixing heredocs, `powershell -Command "…"` and `python -c "…"` failed at parse time (line 120, unmatched quote) before running anything — write the Python to the scratchpad and run the file.

## Open

- ~~`gi.gdf_*` diag readback zero~~ — fixed by `phase0_sponza-followup-03` (see Evidence).
- ~~Cascade lag on camera teleport~~ — investigated and **not reproducible** (`phase0_sponza-followup-02`): the shadow cache invalidates on the same frame. What remains is auto-exposure adaptation after a hard cut, which is deliberate.
- `verify_sponza_loading.ps1` writes `scripts/sponza-loading-fixed-*.{png,log,args,exit,stderr.log}` and the compare writes `-stationary-metrics.json`; see the git-ignore check in the final summary before committing.
