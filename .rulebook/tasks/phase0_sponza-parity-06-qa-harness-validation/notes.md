# Notes — phase0_sponza-parity-06-qa-harness-validation

2026-09-08. Exe: `runtime/build/bin/uzengine.exe` 11:46 (all six tasks landed). Reference: SponzaTest exe Sep 7 13:38 (`test/windows-sponza-parity @ 25cd0c5f`), run side by side on the same machine (RTX 5080, DXR Tier 1.2, SM6.9, Agility 619 via device factory).

## Decision (item 3.4): KEEP the neutral ported pipeline

Criteria from the proposal, measured on the default project at 1920×1080 unless noted (mean luminance /255, share of samples below 16):

| Criterion | Evidence | Verdict |
|---|---|---|
| Sun shafts on the floor | Camera A: sunlit floor region 238.2, shaded floor 35.2 (`compare_sponza_stationary`, converged) | ✓ |
| Sun-lit / shadow contrast ≥ 3:1 (post-tonemap) | 238.2 / 35.2 = **6.8 : 1** | ✓ |
| No white decal patch | floor decal at Camera A/C is dark (visible in `sponza-06-cameraA-f640.png`, `sponza-06-cameraC-f640.png`); `dirt_decal` is Masked with specular 0 | ✓ |
| No broad bright wall/floor contact lines | none in the six inspected captures (`sponza-06-paridade.jpg`) | ✓ |
| No light leaks at doorway / arcade | doorway and arcade shadows intact at Camera A; Camera C matches SponzaTest pixel-for-pixel by eye | ✓ |
| Stable stationary convergence 120→640 | whole image 53.34 → 53.42 (+0.15 %), mean abs RGB 3.65/255; shaded column +20.9 % (GI still converging, same behaviour as SponzaTest) | ✓ |
| Parity with SponzaTest | Camera A f640: **52.0 / 36.5 %** vs ST **53.1 / 37.2 %**; Camera C f640: **90.1 / 15.7 %** vs ST **90.0 / 15.7 %** | ✓ |

Against the task-01 baselines (both captured at the fly-cam corridor pose, so the ported pipeline was re-captured at that pose — `sponza-06-corridor-f640.png`, montage `sponza-06-decisao-corredor.jpg`):

| Capture | mean | <16 | Look |
|---|---|---|---|
| `sponza-baseline-manual-calibration.png` (hand calibration, old Euler order) | 27.7 | 15.6 % | dark, reddish tint, flat — no visible bounce, windows dull |
| `sponza-neutral-preport.png` (neutral materials, pre-port renderer) | 68.7 | 2.0 % | washed out, no shadow definition on the floor, far wall blown |
| `sponza-06-corridor-f640.png` (ported pipeline) | 37.4 | 28.2 % | dark corridor lit by the far wall bounce and the sky through the windows; the lion relief and floor tiles read; cool sky tint |

The ported result is darker than the hand calibration in that corridor but it is the physically motivated one (no artificial ambient, GI from the radiance cache, neutral materials) and it matches the reference branch at both QA cameras. The hand calibration is **not** restored; it stays under `sample/reference/sponza-manual-calibration/` (its README documents the sun-angle/Euler caveat). If the user later wants the warmer look, the knobs are scene-side (`r.Exposure`, `r.ColorContrast`, skylight HDRI), not renderer changes.

## Runs (item 3.1 / 3.3)

| Run | Result |
|---|---|
| Project default, Camera A, f640 | exit 0; 52.0 / 36.5 % |
| Camera C QA project, f640 | exit 0; 90.1 / 15.7 % |
| Temporal camera QA (`scripts/sponza-temporal-camera.uzproject`), f250 | exit 0; Lua `TemporalCameraQA started base_frame=37 sweep=158 return=248 settled=337`, phases stationary → sweep → shifted → return logged; 53.8 / 39.0 % |
| `run_sponza_max_quality.bat` CVars headless + `--pass-dump --diag`, f640 | exit 0; 0 Error lines besides `rt_uv_test_lib.hlsl`; DIAG frame 687, 31 targets, `hasFailure=false`; 57.2 / 29.5 % |
| `verify_sponza_loading.ps1` (task 05) | startup / converged / camera-c exit 0, empty stderr |
| `run_sponza_dxr.bat` CVars headless (`r.RayTracing=1`, `r.RayTracing.GI=1`), f300 | exit 0; `Compute pipeline created (name='DDGI_ProbeUpdate')`, `DDGI atlas ready`, `TLASManager: Initialized with max 4096 instances`; 77.1 / 1.0 % — **much brighter than the SDF path**, warm ceiling, shadows flattened. Pipeline creation is the acceptance gate and it passes; the energy mismatch is a separate finding (no task yet). |
| `sponza_dxr_capabilities.cpp` (`zig c++ … -ld3d12 -ldxgi`) | `Adapter 0: NVIDIA GeForce RTX 5080`, `RaytracingTier=12`, DXR 1.0 and 1.1 inline supported |

Only Error-level log lines anywhere: the pre-existing `rt_uv_test_lib.hlsl` `[raypayload]` compile failure (3 lines per run, RT UV test pass, unrelated).

## Ported in this task

- QA content: `scripts/sponza-temporal-camera.{uzproject,uzscene,lua}`, `scripts/test_sponza_temporal_camera.lua` (scene/QA project from task 03/05 already present).
- Presets/tools: `run_sponza_max_quality.bat`, `run_sponza_dxr.bat`, `sponza_dxr_capabilities.cpp`, `test_editor_navigation.{cpp,ps1}` (files only — see below), `compare_sponza_stationary.ps1` + `verify_sponza_loading.ps1` (task 05).
- `fly_camera_controller.cpp`: Right Ctrl / Right Shift accepted alongside the left keys.
- `.gitignore`: Sponza capture/log/hash artifacts, `*_artifacts/`, `*.generated.h`, Agility download dir.
- Docs: ten SponzaTest reports with origin headers + index; known-issues rewrite (RadianceCache / GlobalSDF / DDGI + unresolved list); hardcoded 01/08 re-audit blocks.
- Closed from earlier tasks: 01/0.2 `vcpkgSrcDir()` in `build.zig` (3 hardcoded `.clean` paths gone); 02/7.2 `lightSourceAngle` default 0.5357°; 03/7.3, 04/3.3, 04/5.3, 04/6.3.
- Archived `phase1_fix-sponza-lighting-calibration` → `.rulebook/archive/2026-09-08-phase1_fix-sponza-lighting-calibration/`; tasks index regenerated.

## Not ported / open

- `test_editor_navigation.ps1` needs `NormalizeWindowKey` (`platform_window.cpp`, left/right modifier + WM_SYSKEY normalisation) and `InputSystem::EndFrame` (`scene_render_frame.cpp`), i.e. SponzaTest's `PrepareFrame`/`EndFrame` input refactor. Our `frame_setup.cpp` carries the VFX overlay's pre-`BeginFrame` scroll capture in that exact spot (in-flight user work), so the refactor was not ported. Our `input.cpp` already mirrors VK_CONTROL to both left/right slots, so the controller hunk works without it.
- `rulebook_task_validate` for the six tasks: MCP server not connected.
- DXR DDGI energy mismatch (see runs).
- Pre-existing gtest failures, identical in SponzaTest or from in-flight VFX work: `MaterialGraphHlslCodegen.Add_/Multiply_ExplicitVariables_*`, `MaterialGraphSerialization.RoundTrip_*` (the `tests/material` directory was never compiled before task 02), `RenderingParity.HeightFogDefaults_{PrimaryLayer,DirectionalInscattering}` (component and test byte-identical to SponzaTest), `VfxSceneSDFDI.RegistryComposeProducesAllFragments`.
- Every "Camera A" capture taken before task 05's fly-cam fix (`sponza-01/02/03-*.png`, `tog-*.png`, `sponza-comparacao.*`) was at the script's corridor pose; the new `sponza-06-*` captures supersede them for Camera A.

## Files for the user's eyes

- `.screenshots/sponza-06-paridade.jpg` — Camera A / Camera C ours vs SponzaTest, temporal f250, DXR f300.
- `.screenshots/sponza-06-decisao-corredor.jpg` — hand calibration vs neutral pre-port vs ported pipeline at the same corridor pose.
- `.screenshots/sponza-05-exposure-cmp.jpg` — the white-flash investigation (task 05).

## Item 1.4 diagnosed: it is not the VFX collision (2026-09-15)

The record said `test_editor_navigation.ps1` could not run here because SponzaTest's input refactor
collides with the in-flight VFX overlay input handling. Running it shows that is not what blocks it.

**The VFX part is already accommodated.** The harness extracts the fly-camera block with the pattern
`if (!cameraFromECS && !vfxOverlayWantsMouse`, which is this codebase's VFX-aware form, and that
extraction succeeds.

**What actually blocks it** is that the ported C++ calls `InputSystem::PrepareFrame()` and
`EndFrame()`, which this engine does not have — 11 errors, all of that shape. And the difference is
not cosmetic. SponzaTest **latches**: events accumulate, `PrepareFrame()` publishes them, `EndFrame()`
clears them, so a key pressed and released inside one frame still reports both edges — which is
precisely what `QuickKeyboardAndMouseEdgesSurviveUntilEndFrame` and `QuickF5TapBeforeFrameTogglesOnce`
assert. This engine **snapshots**: `InputSystem::BeginFrame` is `memcpy(m_keyPrevious, m_keyCurrent)`
and nothing else.

**So the engine loses fast taps**, and that is a user-visible defect rather than a test-harness
detail: a key that goes down and up between two `BeginFrame()` calls ends with previous == current ==
up, so `IsKeyJustPressed` and `IsKeyJustReleased` are both false and the press does nothing. A quick
F5 tap does not toggle the editor camera.

**Why it stays deferred.** Making the test pass means implementing the latching, which is the input
refactor itself — `PrepareFrame`/`EndFrame` in `InputSystem` plus moving the call sites in
`frame_setup.cpp` and `scene_render_frame.cpp`. `frame_setup.cpp` is where the VFX overlay input
handling is in flight, so that is where the original collision note points, and it is a real reason to
wait. Shimming the test onto `BeginFrame()` was considered and rejected: it would either assert
something weaker than the ported test does, or fail — and failing would be *correct*, since the
behaviour genuinely is not there. A test rewritten until it passes would hide the defect it found.

**What this changes in the record:** item 1.4 is blocked by a missing engine behaviour, now measured
and named, not by an unspecified collision. The two test files stay as ported so they can be run
unchanged once the refactor lands.

## Item 1.4 implemented: the engine latches input edges (2026-09-16)

The diagnosis said this was blocked by a missing engine behaviour: `BeginFrame()` was
`memcpy(previous, current)`, so a key pressed AND released between two calls ended with
previous == current == up and reported neither edge — a quick F5 tap did nothing.

**What changed.** The event handlers record the edge as it arrives:

* `OnKeyDown` sets a pressed edge, guarded so WM_KEYDOWN auto-repeat reports one press, not one per
  repeat; `OnKeyUp` sets a released edge; `OnMouseButton` does both.
* `IsKeyJustPressed` / `IsKeyJustReleased` (and the mouse pair) report the recorded edge OR the old
  snapshot transition, so a key held across frames behaves exactly as before.
* `PrepareFrame()` publishes (snapshot, mouse delta, gamepads) and `EndFrame()` clears (edges, scroll
  and raw accumulators). `BeginFrame()` is now `EndFrame()` then `PrepareFrame()`.

**What did not change.** The frame boundary. `BeginFrame()` still drops the previous frame's
accumulators and takes the snapshot in one call, so `frame_setup.cpp` — where the VFX overlay input
is in flight — keeps working untouched: the scroll delta captured before `BeginFrame()` is still
there, and the overlay still reads the mouse after it. No call site moved, which is what the record
said to wait for.

**Verification** (`zig build test`, `runtime/tests/platform/test_input.cpp`), seven new tests, all
passing:

| test | what it pins |
|---|---|
| `QuickTapWithinOneFrame_ReportsBothKeyEdges` | the defect: press + release inside one frame reports both edges |
| `QuickTapWithinOneFrame_ReportsBothMouseEdges` | the same for a mouse button |
| `QuickTapEdges_AreGoneNextFrame` | an edge belongs to its own frame |
| `HeldKeyAcrossFrames_ReportsOnePress` | the old behaviour for held keys is unchanged |
| `AutoRepeatDown_DoesNotRepeatThePressEdge` | auto-repeat is not a new press |
| `PrepareAndEndFrame_KeepTheEdgeUntilEndFrame` | only `EndFrame()` ends an edge |
| `ReInit_ClearsPendingEdges` | a fresh `Init()` reports no stale edge |

The whole suite: 7 635 tests, 7 480 passed, 85 failed, **none in `InputTest`**. The failures are 26
shader-cross-backend cases and VFX editor/component suites (`VfxEditorSlate`, `VFXLodEvaluator`,
`HeightFogComponent`, ...), the pre-existing set this port has carried; two were read directly and
neither touches this change (a `PerFrameCB` size expectation of 768 against an actual 1 024, and
backend compile expectations).

**Still not done:** the two SponzaTest files (`QuickKeyboardAndMouseEdgesSurviveUntilEndFrame`,
`QuickF5TapBeforeFrameTogglesOnce`) are in the reference repo, not here, so they were not run. The
behaviour they assert is implemented and tested above; running those files unchanged still needs
`test_editor_navigation.ps1`, which also wants `NormalizeWindowKey` in `platform_window.cpp`.
