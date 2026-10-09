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

## Closure review (2026-10-08)

A static review for closing the task, against HEAD `4501fa74` (the sources it cites are unchanged at
`8246ab5c`, and at `5fcd62e5`, where `runtime/tools/uzengine.cpp` changed outside the parts cited;
re-checked 2026-10-09). Nothing was built or run apart from `rulebook task validate`; every
prediction below comes from reading the sources.

### Correction (2026-10-08): the record of item 1.4

* "No call site moved" (2026-09-16 section) was overtaken the same day by the frame-boundary fix.
  The runner pumps window messages before `RenderFrame`, and `BeginFrame()` in `frame_setup.cpp`
  dropped every edge before anything read it. `frame_setup.cpp:234` now calls `PrepareFrame()`, and
  `frame_overlay.cpp:396` calls `EndFrame()` after `ClearTypedChars()`. It was committed with the port
  in `7eea6b95` (2026-10-04).
* "The two SponzaTest files live in the reference repo, not here" (2026-09-16 section and
  `.metadata.json`) is wrong. `scripts/test_editor_navigation.{cpp,ps1}` have been here since
  2026-09-08 (the 2026-09-15 diagnosis ran them), were committed in `7eea6b95`, and as committed match
  `origin/test/windows-sponza-parity` apart from line endings (the `.ps1` now also differs by the
  guard change below).
* `NormalizeWindowKey` came with merge `31b5c14a` (2026-10-05) from SponzaTest `97acd374`
  (`platform_window.cpp:28`, called at `:112` and `:119`). The harness now extracts it.

### Item 1.4 residue: what the navigation harness still needs

An emulation of the `.ps1` extraction on today's sources (Python, CRLF kept as `Get-Content -Raw`
keeps it) extracts `$prepare`, `$toggle`, `$fly`, `$speed`, `$keyExpression` and `$keyNormalizer`.
`$end` comes back empty: this branch has no `m_input->EndFrame()` in `scene_render_frame.cpp`, because
merge `31b5c14a` dropped SponzaTest's call there as a duplicate of `frame_overlay.cpp:396`. The guard
did not check `$end`, so the harness would have compiled a `Frame()` that never ends the input frame.
Reading the sources predicts that 5 of the 9 `EditorNavigation` tests pass and these 4 fail:

| test | why it fails |
|---|---|
| `InjectedModifiersLatchQuickTransitions` | `SetModifierState` (`input.cpp:561-576`) writes `m_keyCurrent` and records no edge. SponzaTest routes it through `OnKeyDown`/`OnKeyUp` (its `input.cpp:542-561`). It has no production caller here (`runtime.cpp:1231-1236`). |
| `PumpedF5ActivatesOnceAndRestoresCamera` | The harnessed frame never calls `EndFrame`, so F5 toggles again on the second frame. With that fixed, the test still fails at `test_editor_navigation.cpp:74`. Nothing calls `InitFromCamera` on the push or the pop (SponzaTest `frame_setup.cpp:258`, `:266`), so after the pop the controller re-aims camera 0 from its default yaw and pitch. |
| `FreeCameraMovesAtSceneScaleAndMouseMovesSameFrame` | F5 toggles again (no `EndFrame`). The controller runs only for camera id 0 (`frame_setup.cpp:339`; SponzaTest's `:300-303` also runs it for the free camera). The editor speed is 5 cm/s (`scene_renderer_init.cpp:177`; SponzaTest `:169` sets 500). |
| `QuickF5TapBeforeFrameTogglesOnce` | F5 toggles again on the second frame (no `EndFrame`). |

Five SponzaTest pieces are missing: the unconditional `EndFrame()` at the end of `RenderFrame`,
`InitFromCamera` on the push and the pop, the free-camera gating, the 500 cm/s speed and the latching
`SetModifierState`. They are engine changes and are listed for this task's closure below. With the
first four applied to a scratch copy, the extraction succeeds and the generated
`navigation_frame.generated.h` is identical to the one SponzaTest's sources produce. With all five in,
all 9 tests are expected to pass (`InjectedModifiersLatchQuickTransitions` needs the fifth), but this
has not been measured.

Three of the gaps are visible to users even without the harness:

* The F5 free camera cannot be moved. The fly controller runs only for camera id 0, and
  `fly_camera.lua` moves only the ECS camera. `docs/analysis/rendering/sponza-test-handoff.md:45`
  promises F5 with WASD.
* With `r.ShadowDebug` set to 1, the OverlayPass is not registered (`frame_overlay.cpp:188`; the mode
  is re-read every frame, `scene_renderer_init.cpp:1095`), so `EndFrame()` never runs. Press edges and
  scroll and raw deltas are never cleared, so every `IsKeyJustPressed` toggle fires again on every
  frame: F5, F, G, the number keys and Lua `uz.input.is_key_just_pressed`.
* The standalone `uzengine.exe` never calls `SetMoveSpeed`, so its C++ fly controller moves at 5 cm/s;
  so does the runtime C API viewport of the VS Code scene editor (`runtime/engine/app/runtime.cpp`).
  The `Application` host already applies `AppConfig::cameraMoveSpeed` of 500 (`application.cpp:174`,
  `app_config.h:112`).

**Harness change (2026-10-08).** Line 15 of `scripts/test_editor_navigation.ps1` now also throws when
`$end` is empty. This is the only difference from the SponzaTest file. What the script extracts and
asserts is unchanged, so the rule of 2026-09-15 (no test rewritten until it passes) still holds. In the
emulation (Python, and again with Windows PowerShell's own regex engine), the guard throws on today's
sources with its generic message, `$end` being the only empty extraction. It passes once change 1
below (the `EndFrame` call) is in; with changes 1-4 applied, the generated header equals the one from
SponzaTest's sources apart from line endings (re-checked 2026-10-09).

### Hand-offs to this task that were not picked up

* Parity-03 (notes.md:30-32, :42) handed this task SponzaTest's editor-navigation hunks in
  `frame_setup.cpp`. `PrepareFrame` is done and `InitFromCamera` is covered above. The third hunk is
  overlay shutdown on failure: when the deferred `Init` of the stats overlay, the VFX editor overlay or
  the viewport toolbar fails, SponzaTest calls `Shutdown()` and `reset()`. This branch only logs
  (`frame_setup.cpp:79`, `:165`, `:188`), so a failed `Init` is retried every frame with
  `End`/`Execute`/`WaitIdle`/`Begin`, and the stats block re-uploads its fallback textures each time.
  The fix is queued as an engine change.
* Parity-03 tasks.md 1.3: `test_sdf_window.ps1 -BvhCompare` has never run. There is no
  `window_mesh_128_bvh.r16` in `scripts/test_sdf_dynamic_gpu_artifacts/`; only the brute-force field
  from 2026-09-08 is there. Reading the sources shows the fixture cannot run unchanged any more.
  `scripts/test_sdf_window.cpp` and `scripts/test_sdf_gpu.cpp` declare 9 root DWORDs and one static
  sampler at `s2`. Today `global_sdf_composite_cs.hlsl` takes 13 root DWORDs (`ce2caa5e`, `0a9cc732`),
  `mesh_sdf_bake_cs.hlsl` takes 9 (`8c4d14b6`), and `sdf_reflections.hlsli` samples through a WRAP
  sampler at `s3` (`ce2caa5e`). A separate task was proposed for this, together with the other SDF GPU
  fixtures, and created on 2026-10-09: `phase0_sdf-gpu-fixture-refresh`.
* Parity-01 (notes.md:130-150) asked this task to add the one-line stub shaders to known-issues. Item
  2.2 put the whole-tree count (32 files, 26 referenced) under `runtime/shaders/runtime/gi/`, where
  only the bake and composite shaders were stubs, and did not add the list. Known-issues still
  describes `r.MotionBlur` as working correctly and `r.DOF`, `r.LensFlare`, `r.TSR`, `r.FSR` and
  `r.SMAA` as implemented, although their shaders are one-line stubs (29 remain today). The
  corrections were applied to known-issues on 2026-10-08 (the `r.RadianceCache` paragraph, the
  `r.TSR`, `r.FSR`, `r.SMAA`, `r.DOF`, `r.LensFlare`, `r.MotionBlur` and Distortion entries, and the
  new "One-line stub shaders" entry).

### Corrections to other items

* Item 2.3: the stale "Exposure 3.0" comment is still at `frame_constants.cpp:127-129`. Its removal is
  queued as an engine change.
* Item 3.3 and the Runs table: the DXR 77.1 against SDF 52.0 comparison was confounded and is not an
  energy finding. `run_sponza_dxr.bat` sets `r.GI.FinalGather=0`, which brings back the additive SSGI
  term, while the SDF capture ran with the gather on (followup-01 notes.md:7). "No task yet" is
  stale: `phase0_sponza-followup-01-dxr-ddgi-energy-parity` was opened from these notes the same day.
  The DDGI level is still open there and in `docs/analysis/lighting-block-b-2026-10.md` ("Still open
  after the block", B.6). Followup-01's 2026-10-08 close-out passes the writer-parity re-measurement to
  `phase0_lighting-ddgi-sdf-writer-parity-head` (created 2026-10-09).
* Item 3.2: "stable stationary convergence" compared the whole-image means of frames 120 and 640
  (+0.15 %), while the shaded column still moved +20.9 %. That is convergence drift, not
  frame-to-frame stability. Static-camera flicker was measured later by `phase0_sponza-followup-09..14`
  and is open in known-issues.
* The pre-existing gtest failures ("Not ported / open", 5.3) belong to no task:
  * `MaterialGraphSerialization.RoundTrip_NodeCount_Connections_ParamValues` failed in parity-02's
    run (material graph suites 38/39) and passed in followup-05's (`*MaterialGraph*` 39/39, notes.md:62).
    Parity-02 notes.md:80-86 blamed an expression id 0 colliding with the `IsConnected()` sentinel
    (`material_expression.h:55`), but expression ids start at 2 (`material_graph.h:195`). From reading
    the code (not run): the test keeps `scalar`, a reference to `expressions_.back()` of a
    `std::vector` (`material_graph.cpp:73-77`), across the next `AddExpression`
    (`test_material_graph.cpp:58`, `:62`), which reallocates the vector, so the `scalar.id` printed at
    `:93-94` is read from freed memory. The test and the graph code are unchanged since `7eea6b95`.
  * `MaterialGraphHlslCodegen.Add_/Multiply_ExplicitVariables_*` hold `AddExpression` references the
    same way (`test_material_graph.cpp:500-506`, `:544-550`); `Add_` is recorded as depending on the
    test order (followup-05 notes.md:62).
  * The fog and sky default-value tests.

  A task was proposed for all of these and created on 2026-10-09: `phase0_preexisting-test-failures`.
  Its proposal and item 1.1 still give parity-02's sentinel diagnosis, which the first bullet above
  corrects. `VfxSceneSDFDI.*` belongs to the VFX work.
* The `rt_uv_test_lib.hlsl` `[raypayload]` error accepted in 3.1 is a one-line shader fix
  (`docs/analysis/rendering/sponza-ddgi-cbuffer-layout-2026-09-07.md:54`). It is queued as an engine
  change. The qualifier that line gives does not compile; change 8 below has one that does.

### Item 4.2 done

`rulebook task validate` (CLI 5.4.1, read-only; `git status` was the same before and after) ran on the
six tasks. Parity-05 and 06 were valid. Parity-01..04 failed only the v5.3.0 mandatory-tail check,
because their tail lines read "Documentation:" and "Tests:" (and, in parity-02, "Tests run") instead of
the labels the CLI matches. Those labels were reworded in place, with the content unchanged: 01 items
10.1-10.2, 02 items 6.1-6.3, 03 items 7.1-7.2 and 04 items 6.1-6.2. A second run reported all six
valid.

### What closes this task

Engine changes:

1. `scene_render_frame.cpp`: `#include "input.h"`, and `m_input->EndFrame();` as the last statement of
   `RenderFrame` (SponzaTest `:29`, `:384`). The call at `frame_overlay.cpp:396` then becomes
   redundant, and the comment at `frame_setup.cpp:228-233` (edges dropped "at the end of the overlay
   pass") is updated to the new boundary.
2. `frame_setup.cpp`: `m_cameraCtrl->InitFromCamera(*freeCam)` after `:294`, and
   `m_cameraCtrl->InitFromCamera(m_cameraManager->GetActiveCamera())` after `:301`.
3. `frame_setup.cpp:339`: SponzaTest's free-camera gating (its `:300-303`), landing together with 2.
4. `scene_renderer_init.cpp:177`: `SetMoveSpeed(500.f)`.
5. `input.cpp:561-576`: `SetModifierState` through `OnKeyDown`/`OnKeyUp`.
6. `frame_setup.cpp:78-80`, `:164-166`, `:187-189`: `Shutdown()` and `reset()` on a failed `Init`. The
   VFX-overlay hunk needs the user's agreement, because that overlay's work is in flight.
7. Delete `frame_constants.cpp:127-129`.
8. `rt_uv_test_lib.hlsl:17-19`: `struct [raypayload] RayPayload { float4 color : read(caller) :
   write(closesthit, miss); };`. The qualifier in `sponza-ddgi-cbuffer-layout-2026-09-07.md:54`,
   `read(caller, closesthit, miss)`, fails to compile: the bundled dxc 1.9.2602 (the same
   `dxcompiler.dll` the engine loads) reports "field 'color' is qualified 'read' for shader stage
   'miss' but has no valid producer" (and the same for `closesthit`) at `lib_6_8` and `lib_6_9`. The
   form above compiles at `lib_6_7` to `lib_6_9` without warnings, and at `lib_6_6` with a warning that
   the qualifiers are ignored. The unchanged file fails at `lib_6_8` with "type 'RayPayload' used as
   payload requires that it is annotated with the [raypayload] attribute" (checked 2026-10-09 with the
   bundled dxc, output to the scratchpad; no engine build).

After that, `scripts/test_editor_navigation.ps1` (CPU only) must pass all 9 `EditorNavigation` tests
together with the input suite it compiles; that ticks 1.4.

## Close-out runs (2026-10-09)

Engine changes E1-E8 in 8fb13fa1 (E6 without the VFX editor overlay hunk, left out while the user's VFX work is
in flight). Runs on the Debug build:

* R1 `scripts/test_editor_navigation.ps1`: 75/75 (9 EditorNavigation + 66 input), exit 0. Item 1.4 holds.
* R2 `test_runtime --gtest_filter=InputTest.*:InputLifecycle.*:GamepadDeadzone.*` 66/66. The full run: 89 of
  7649 failing, all in the known pre-existing set (VFX work in progress, ShaderCrossBackend,
  fog / sky / loader / Lua / viewport defaults) plus two unlisted ones, recorded in
  phase0_preexisting-test-failures: ShaderPipelineMetal.* (the fixture sets no DXC compiler) and
  AverageRootConstants.SizeIs32Bytes, stale since a13003d8 and fixed in 8fb13fa1.
* R4 `dxc -T lib_6_8 rt_uv_test_lib.hlsl`: exit 0 with `write(closesthit, miss) : read(caller)`. The first
  version (`read(caller, closesthit, miss) : write(closesthit, miss)`) failed with "qualified 'read' for shader
  stage 'miss' but has no valid producer" and, built into the engine, made the RT UV test pass skip its
  resources, which moved three references by one code value (render-graph state harness, 2026-10-09).
* R3 (the F key under r.ShadowDebug 1) and R5 (owned by phase0_sdf-gpu-fixture-refresh) were not run.

The task closes.
