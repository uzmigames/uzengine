# Proposal: phase0_sponza-followup-06-firefly-noise-indirect

## Why

Opening the Sponza scene interactively (2026-09-08, after the parity port and follow-ups 01–05) the
user saw dense white specks on a shadowed wall next to a barred window, viewed close and at a grazing
angle. The default project ran every stochastic pass — SDF reflections (4 GGX rays/pixel), the
world-space final gather (8 rays, half-res), GTAO (2 directions × 8 taps) — with **`r.TAA` off**
(engine default `false`, project did not set it), so nothing accumulated their per-pixel noise.

Reproduced deterministically: `scripts/sponza-temporal-camera.uzproject` + `r.GI.FinalGather=1`,
Camera A, the wall beside the window: **892** isolated bright pixels static, **996** mid-sweep,
**145** with `r.TAA=1` (metric: luminance ≥ 120 and ≥ 80 above the 4-neighbour mean, 1080p,
2-px stride). Zooms in `.screenshots/ff-zoom-fg1-sweep.png` match the user's screenshot.

Attribution by elimination (all at a fixed grazing pose, static frame 300): the specks survive
sun intensity 0, `r.GI.FinalGather=0`, `r.GTAO=0`, `r.GI.Reflections.Steps=16` (rays exhaust →
black) and evaluating the probe SH with the geometric normal; they scale with `r.GI.Intensity`;
they drop 84 % with `r.GI.Reflections=0`; more rays help little (16 rays: 708 → 600). What fits
every observation is **reflection rays that escape through the window opening within a few steps and
return sky radiance**: with 4 rays per pixel a pixel gets 0 or 1 such ray — binary noise on a dark
wall — and there is no spatial or temporal filter on the SDF reflection path.

Along the way three genuine estimator defects in the indirect paths were found and fixed, each
verified in the binary (the shader embed does not recompile its C++ TU on a `.hlsli`-only change —
see notes) and measured:

1. `GatherSdfReflections` averaged rays with a plain mean → Karis `1/(1+luma)` weight (same as the
   SSR resolve).
2. Rays below the shading horizon were dropped **and** the result renormalised over the survivors →
   a single surviving sun-lit ray became the pixel with 4× inflation. Their weight now stays in the
   denominator.
3. The radiance-cache consumer selected probes with the **normal-mapped** shading normal → texels
   admitted different probe sets, one bright atrium probe alone lit isolated texels. Probe
   selection now uses the geometric normal; `EvalSH_L2` keeps the shading normal.
   Isolated pixels in the radiance-cache-only configuration: 1333 → 893; gather-off pose: 707 → 365.

## What Changes

- Shader fixes 1–3 (`sdf_reflections.hlsli`, `deferred_lighting_ps.hlsl`), guarded by
  `scripts/test_sdf_reflection_firefly.ps1` (15 source-level + CPU-model checks).
- `sample/projects/sponza.uzproject`: `r.TAA: 1` — the stochastic pipeline needs temporal
  accumulation; the SponzaTest max-quality preset already sets it. Camera A isolated pixels
  944 → 244, luminance 53.0 → 54.1 (SponzaTest 53.1).
- `fly_camera.lua`: **P** logs the camera pose so an artifact location can be reported exactly.
- Two follow-up reports fixed in the same task: the whole scene flashing while flying (radiance
  cache readiness gate dropping every indirect path on a probe-cell crossing, item 4.4) and a blink
  when rising through z ≈ 267 cm (the recentre published the new origin with last frame's buffer and
  retraced without history, item 4.5) — the recentre now scrolls the probe history through an
  integer grid shift and lighting indexes the completed buffer with its own origin. Repro scenes
  `scripts/sponza-longsweep-camera.*`, `scripts/sponza-vertical-camera.*`; guard
  `scripts/test_rc_recentre_scroll.ps1`.
- Open (this task's remaining items): a spatial/temporal filter for the SDF reflection path, and
  the debug views `shadows`, `lighting`, `lightingonly`, `ao` which render magenta / black / flat
  and could not be used for attribution.
  *Correction (2026-10-08): both closed. The four debug views were fixed and verified on
  2026-09-16 (tasks.md 4.2). No filter was built: 4.1 closed by decision on 2026-09-23 (ledger
  H-68). Since block B the trace constants are 464 bytes (0a9cc732), and the DDGI mirror
  listed below is `ddgi_probe_common.hlsli` (edfd7942); see notes.md, "Closure review
  (2026-10-08)".*

## Impact
- Affected specs: none
- Affected code: `runtime/shaders/runtime/gi/sdf_reflections.hlsli`,
  `runtime/shaders/runtime/lighting/deferred_lighting_ps.hlsl`, `sample/projects/sponza.uzproject`,
  `sample/content/scripts/fly_camera.lua`, `scripts/test_sdf_reflection_firefly.ps1`,
  `runtime/engine/renderer/gi/radiance_cache.{h,cpp}`, `runtime/shaders/runtime/gi/radiance_cache_trace_cs.hlsl`,
  `runtime/shaders/runtime/gi/ddgi_probe_update_cs.hlsl` (CB mirror row), `runtime/engine/renderer/frame/frame_constants.cpp`,
  `runtime/tests/engine/renderer/test_gi_gpu.cpp`, `scripts/test_rc_recentre_scroll.ps1`, `scripts/sponza-*-camera.*`
- Breaking change: NO
- User benefit: no sparkle on dark walls; two estimator bugs that inflated indirect light removed
