# Notes — phase0_sponza-parity-01-core-fixes

## Section 1 — baselines captured 2026-09-07 (pre-port binary)

Binary: `runtime/build/bin/uzengine.exe`, built 2026-05-06 16:57 (no source change yet).
Command shape: `--project sample/projects/sponza.uzproject --screenshot <path> --screenshot-frame 600 --frames 700 --log-level info`.

| Capture | Data | File |
|---|---|---|
| Manual calibration (backup restored, `git status` clean vs `a29168da`) | 33 scene CVars, exposure 2.0, sun `[-72,30,0]`, HDRI skylight, roughness x0.55 / metallic x0.0 / warm tint | `.screenshots/sponza-baseline-manual-calibration.png` |
| Neutral (SponzaTest data, current working tree) | 7 display CVars, auto-exposure, sun `[-56,79,0]`, procedural sky, neutral materials | `.screenshots/sponza-neutral-preport.png` |
| Discarded first attempt — incomplete scene, kept as task-05 evidence | manual calibration, `--frames 240`, no `--screenshot-frame` | `.screenshots/sponza-baseline-manual-240f-incomplete.png` |

**Viewpoints differ**: the manual scene has a single `Camera` at `[36,-611,102]` FOV 75; the neutral
scene has `Camera_A` at `[86,881,159]` FOV 58.51 with `look_at`. The comparison is pipeline+data
level, not pixel level. A pixel-level A/B needs the same camera in both — add one if task 06 needs it.

Visual read of the two: the manual baseline is very dark with a warm/red cast on the stone, no sun
shafts, flat contrast (matches `docs/analysis/rendering/sponza-lighting-rootcause.md`). The neutral
capture is brighter and colour-neutral thanks to auto-exposure and pass-through material params, but
still has no sun shafts and no bounce light — expected, since the sun is at ~9 deg elevation
(FromEuler bug, section 2) and the SDF GI shaders are stubs (task 03).

## Why `--screenshot-frame 600` was needed

The first attempt captured an incomplete scene. Evidence from that run:

```
14:29:33.841  Pre-load complete, all textures loaded
14:29:40.168  Screenshot saved
14:29:41.201  SceneRenderer: loaded mesh (last)   <-- after the capture
```

`runtime/tools/uzengine.cpp:810` triggers on `IsSceneFullyLoaded() || frameIndex >= screenshotFrame + 120`,
and readiness reports true while budget-deferred meshes are still pending — the defect task 05 fixes.
Forcing `--screenshot-frame 600` moves the capture past the load. Both final captures loaded all
**138** meshes (the prefab references 138; the 146 files in `meshes/sponza/` include 8 unreferenced).

## Findings that corrected other task items

1. `GetCascadeFadeStart` and `ShadowDataParams::cascadeFadeStarts[8]` already exist here
   (`shadow.hlsli:52`, `shadow_data_params.h:83`) and `shadow_pcss.hlsli:210` already uses them.
   Item 5.2 is shader-only; no producer port. Corrected in `tasks.md`.
2. The `shadow_renderer.cpp` / `frame_shadows.cpp` / `shadow_pass.cpp` hunks in SponzaTest `97acd374`
   are translucent shadow-caster exclusion (`materialFlags & 0x3u >= 2u`), not bias work. Moved to
   task 02 item 2.3.
3. `--log-level`, `--width`, `--height`, `--adapter`, `--screenshot-frame` all work through
   `runtime/tools/uzengine.cpp`'s generic `Config`; project-vs-CLI precedence is already correct
   (`uzengine.cpp:108` and `:952`). Task 05 section 4 rewritten accordingly.

## Section 2 — rotation audit (item 2.3), read-only pass done 2026-09-07

`Quat::FromEuler(pitch, yaw, roll)` maps pitch->Y, yaw->Z, roll->X (`quaternion.cpp:29-31`).
`scene_loader.cpp:206` passes `rotation[0]`->pitch, `[1]`->yaw, `[2]`->roll.
`lua_scene_api.cpp:222` (`uz.scene.set_rotation`) uses the same order.

**32 of 34 scenes** author a rotation with two or more non-zero axes, so all of them change direction
when the composition order is fixed:

- 28 scenes: `Sun [-30,0,60]` or `[-30,0,50]` (pitch+roll) — `bloom_test`, `camera_test`,
  `chrome_test`, `cloud_test`, `cornell_box`, `darkmetal_test`, `decal_test`, `deferred_test`,
  `demo`, `fbx_test`, `format_test`, `glass_test`, `hdri_test`, `ibl_test`, `instancing_test`,
  `lighting_reflection_test`, `local_shadows_test`, `material_test`, `mesh_test`, `minimal_test`,
  `multi_camera_demo`, `pbr_showcase`, `postprocess_test`, `roughmetal_test`, `test_obj_bunny`,
  `test_obj_suzanne`, `test_stl_teapot`, `textured_quad`.
- `scifi_helmet`: `Sun [-40,0,55]`.
- `damaged_helmet`: `Sun [-30,0,60]`, `DamagedHelmet [90,180,180]`, `MainCamera [-90,180,180]`
  — the two 3-axis ones look risky but are provably unchanged (see the direction table below).
- `material_showcase`: **`MainCamera [68.2,-90,0]`**, `Sun [-30,0,50]`.
- `sponza`: `Sun [-72,30,0]` in the backup; the working tree now authors the post-fix `[-56,79,0]`.

Lua callers with two or more axes: `cornell_box_setup.lua:77` (`-30,0,50`),
`day_night_cycle.lua:126` (`elev,15,0`). Single-axis and therefore safe: `cube_rotator.lua:34`,
`decal_setup.lua:32,41`, `mushroom_controller.lua:183` (`set_rotation_yaw`). `hello.lua:10` feeds
back its own Euler angles each frame — re-check after the fix.

Note `593c6304` ("remap all 34 scene rotations to ZYX Euler convention") and `f035ec31` authored
these values against the current (incorrect) composition, so most of them are compensations.

## Direction change per authored rotation (computed 2026-09-07, before/after the fix)

Forward axis is +X (`light_renderer.cpp:51`). Old composition applied yaw → pitch → roll;
the fixed one applies roll → pitch → yaw. Elevation is `asin(z/|v|)`.

| Authored `[pitch,yaw,roll]` | Before | Elev | After | Elev | |
|---|---|---|---|---|---|
| `[-30,0,60]` (23 scenes) | (0.866, -0.433, 0.250) | 14.5° | (0.866, 0.000, 0.500) | 30.0° | changes |
| `[-30,0,50]` (5 scenes) | (0.866, -0.383, 0.321) | 18.7° | (0.866, 0.000, 0.500) | 30.0° | changes |
| `[-40,0,55]` (scifi_helmet) | (0.766, -0.527, 0.369) | 21.6° | (0.766, 0.000, 0.643) | 40.0° | changes |
| `[-72,30,0]` (Sponza backup) | (0.268, 0.500, 0.824) | 55.5° | (0.268, 0.155, 0.951) | 72.0° | changes |
| `[-56,79,0]` (Sponza now) | (0.107, 0.982, 0.158) | 9.1° | (0.107, 0.549, 0.829) | 56.0° | changes |
| `[90,180,180]` (helmet mesh) | (0, 0, -1) | -90° | (0, 0, -1) | -90° | **unchanged** |
| `[-90,180,180]` (helmet cam) | (0, 0, 1) | 90° | (0, 0, 1) | 90° | **unchanged** |
| `[68.2,-90,0]` (showcase cam) | (0, -1, 0) | 0° | (0, -0.371, -0.928) | -68.2° | changes |

The `[-56,79,0]` row reproduces SponzaTest's evidence exactly (9.1° → 56.0°), which validates the
implementation independently of their code.

**Decision for item 2.3: do not re-author the sample scenes.** Roll is a rotation about the light's
own forward axis, so under the documented semantics ("Pitch = elevation, Yaw = bearing, Roll = barrel
roll") it must not change a directional light's direction at all. The old order applied roll *after*
pitch about the fixed world X axis, which is exactly why `[-30,0,60]` produced a 14.5° sun instead of
the authored 30°. After the fix every one of those scenes gets the elevation its file states, with
azimuth East. That is a correction, not a regression, and re-authoring would re-encode the bug.

Two entries still need a human look because they were dialled in by eye rather than by semantics:
`material_showcase.uzscene` `MainCamera [68.2,-90,0]` (horizontal → pitched 68.2° down) and
`day_night_cycle.lua:126` (`elev,15,0`, animated). `damaged_helmet.uzscene`'s two 3-axis rotations
are provably unchanged, so that scene is safe.

## Shader validation with DXC (2026-09-07)

`runtime/thirdparty/dxc/bin/x64/dxc.exe` compiles the four edited shaders standalone, which catches
HLSL errors without waiting for an engine run:

```
dxc -T cs_6_6 -E main -D UZ_BINDLESS=1 runtime/postprocess/ssr_resolve_cs.hlsl   -Fo nul   OK
dxc -T cs_6_6 -E main -D UZ_BINDLESS=1 runtime/atmosphere/sky_cubemap_cs.hlsl    -Fo nul   OK
dxc -T cs_6_6 -E main -D UZ_BINDLESS=1 runtime/gi/ddgi_probe_update_cs.hlsl      -Fo nul   OK
dxc -T ps_6_6 -E main -D UZ_BINDLESS=1 runtime/lighting/deferred_lighting_ps.hlsl -Fo nul  OK
dxc -T ps_6_6 -E main -D UZ_BINDLESS=1 -D UZ_SHADOW_PCSS=1 ... deferred_lighting_ps.hlsl  OK
```

**Pre-existing, not caused by this task**: the *non-bindless* variant of `deferred_lighting_ps.hlsl`
does not compile — `shadow.hlsli:173` uses `shadowAtlasTexture`, which our shader never declares.
`git show HEAD:...` fails identically, so it predates these edits. SponzaTest declares it (plus the
IBL cube/LUT textures) at `t3`/`t5`/`t6`/`t7`; port that with their non-bindless work if we ever
build that path. The engine uses the bindless variant on DX12, which is why this never surfaced.

## Cross-cutting finding: 32 one-line stub shaders (audited 2026-09-07)

`mesh_sdf_bake_cs.hlsl` and `global_sdf_composite_cs.hlsl` are not isolated cases. A sweep of
`runtime/shaders` for files of two lines or fewer returns **32 stubs**, of which **26 are referenced
from C++** (pipeline library / pass code, not just a manifest). Only two have a working alternative
implementation next to them (`sky_cubemap_ps` -> `sky_cubemap_cs`, `bloom_downsample_ps` ->
`bloom_downsample_cs`), so those two are dead leftovers of a PS-era pipeline.

Referenced stubs with no alternative — features that look implemented but produce nothing:

`gpu_cull_cs` (GPU frustum + HiZ culling), `motion_blur_cs`, `motionblur_tilemax_cs`,
`motionblur_neighbormax_cs`, `dof_setup_cs`, `dof_blur_cs`, `bloom_blur_h_ps`, `bloom_blur_v_ps`,
`bloom_composite_ps`, `color_grading_lut_cs`, `fsr_easu_cs`, `fsr_rcas_cs`, `tsr_resolve_cs`,
`smaa_edge_ps`, `smaa_blend_ps`, `smaa_resolve_ps`, `lensflare_cs`, `dfs_cone_trace_cs`,
`dfs_cull_objects_cs`, `dfs_upsample_cs`, `pixel_debug_cs`, `vis_fullscreen_ps`, `gbuffer_debug_ps`,
plus the two GI ones this port implements in task 03.

The same 30 shared stubs exist in SponzaTest; the only difference is which two each side filled in
(they did the GI pair, we did `particle_mesh_ps/vs`). Out of scope here — recorded so task 06 can put
it in `known-issues-and-partial-implementations.md`, and so nobody reads "the pass is wired up" as
"the pass works". Repro:

```bash
find runtime/shaders -name '*.hlsl' | while read f; do [ "$(wc -l < "$f")" -le 2 ] && echo "$f"; done
```
