# Proposal: phase0_sponza-parity-05-loading-exposure-cli

## Why

`sponza-loading-exposure-2026-09-07.md` (SponzaTest commit `97acd374`) documents that our runtime
presents frames while the scene is still streaming, and the auto-exposure adapts to that incomplete
HDR:

- The default budget loads four meshes per frame. `IsSceneFullyLoaded()` checked upload queues but
  meshes deferred by the budget lived only in ECS components; the app logged "load complete" at
  02:15:04 while the ceiling was still loading at 02:16:19. Intermediate frames received light
  through missing geometry.
- The old preload drew/presented a single frame and drained only the textures requested by the first
  meshes. Screenshot capture also stopped waiting for readiness after a 120-frame timeout
  (`uzengine.cpp:811`).

Reproduced on our own binary on 2026-09-07 while capturing the task-01 baseline: the run logged
`Pre-load complete, all textures loaded` at 14:29:33.841, saved the screenshot at 14:29:40.168, and
was still loading meshes at 14:29:41.201 — 138 of 146 meshes at process exit (240 frames). The
capture therefore showed an incomplete scene. Workaround used for the baselines:
`--screenshot-frame 600 --frames 700`.
- The first frame after a proper load showed exposure adapted to incomplete HDR. The fix renders full
  HDR, zeroes the exposure history with CopyDst/UAV transitions, and prepares the GPU result before
  the first present. No shader formula changed.
- Project CVars overrode explicit command-line options; the order was corrected.
- SponzaTest presets use `--width`, `--height`, `--adapter`, `--log-level`, which our
  `application_args.cpp` does not parse.

With the manual calibration removed, the scene now relies on `r.AutoExposure=1`,
`r.AutoExposure.MinBrightness=0.001`, `r.Exposure=0.5`, `r.ColorContrast=0.65`,
`r.ColorSaturation=1.0` — all display-side; a wrong first-frame exposure would be very visible.

## What Changes

- `scene_mesh_assets.cpp` (+25), `scene_texture_assets.cpp` (+22), `scene_renderer_impl.h`,
  `scene_renderer.h`: count deferred base/LOD meshes, async slots, mesh uploads and pending SDF bakes in readiness.
- `runtime.cpp`, `application.cpp`, `application_platform.cpp`: preload loop processes batches and
  window events until ready without presenting; Windows title `Loading scene...`; capture honours readiness.
- `auto_exposure.{h,cpp}`: buffer gets `CopyDst`; `ResetHistory(cmdList)`; called once after full load.
- `frame_postprocess.cpp` / `scene_render_frame.cpp`: full-HDR pre-render + reset ordering.
- CLI: **no fix needed** — verified 2026-09-07 that `runtime/tools/uzengine.cpp` already re-applies
  CLI overrides after the project file (`:108`) and appends `--cvar` after the project's `cvars`
  block (`:952`), and that `--width`/`--height`/`--adapter`/`--log-level`/`--screenshot-frame` all
  resolve through its generic `Config`. What remains is documenting the split between that parser and
  the demo-side `application_args.cpp`, which sees none of them.
- `dx12_device.cpp` (+87), `dx12_command_list.cpp`, `dx12_fence.cpp`, `rhi_config.cpp`,
  `rhi_backend.h`, `rhi_types.h`: adapter selection + stability hunks — review each; skip the
  Metal-only files (`metal_device.h`, `metal_resource_creation.cpp`).
- Port `test_scene_readiness.ps1` (14/14), `test_exposure_reset.ps1` (4/4), `verify_sponza_loading.ps1`.

## Impact
- Affected specs: application startup sequence; CLI contract (four new flags)
- Affected code: app/runtime, scene asset streaming, auto-exposure, DX12 device
- Breaking change: NO (startup takes longer before the first present; behaviour otherwise identical)
- User benefit: the first visible frame is the loaded scene at steady-state exposure; presets and
  compare scripts from task 06 run unmodified
